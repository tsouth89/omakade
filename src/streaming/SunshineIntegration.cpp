#include "streaming/SunshineIntegration.h"

#include "app/AppSettings.h"
#include "launch/PlayRequest.h"
#include "library/GameRoles.h"
#include "library/UnifiedGameModel.h"
#include "sources/FlatpakInstall.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QJsonDocument>
#include <QPainter>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>
#include <QtConcurrent>

namespace {
constexpr auto kMarker = "omakade";
constexpr auto kFlatpakId = "dev.lizardbyte.app.Sunshine";
constexpr int kBoxArtWidth = 600;
constexpr int kBoxArtHeight = 800;

QString localPath(const QString& value) {
  const QUrl url(value);
  return url.isLocalFile() ? url.toLocalFile() : value;
}

QByteArray sha1(const QByteArray& contents) {
  return QCryptographicHash::hash(contents, QCryptographicHash::Sha1).toHex();
}

QString writtenListMarker(const QString& imageRoot) {
  return imageRoot + QStringLiteral("/apps.sha1");
}
} // namespace

SunshineIntegration::SunshineIntegration(UnifiedGameModel* games, AppSettings* settings,
                                         const QString& appsPath, const QString& imageRoot,
                                         QObject* parent)
    : QObject(parent), m_games(games), m_settings(settings), m_appsPath(appsPath),
      m_imageRoot(imageRoot) {
  if (m_imageRoot.isEmpty()) {
    m_imageRoot = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
                  QStringLiteral("/omakade/sunshine");
  }
  if (m_appsPath.isEmpty()) {
    detect();
  }
  m_syncTimer.setSingleShot(true);
  m_syncTimer.setInterval(1500);
  connect(&m_syncTimer, &QTimer::timeout, this, [this] { sync(); });
  connect(&m_syncWatcher, &QFutureWatcher<SyncResult>::finished, this,
          &SunshineIntegration::finishSync);
  if (m_games != nullptr) {
    connect(m_games, &QAbstractItemModel::modelReset, this, &SunshineIntegration::scheduleSync);
    connect(m_games, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex&, const QModelIndex&, const QList<int>& roles) {
              // Hiding a game or changing its cover changes what Moonlight should show.
              if (roles.isEmpty() || roles.contains(GameRoles::Hidden) ||
                  roles.contains(GameRoles::CoverPath) || roles.contains(GameRoles::CustomCover)) {
                scheduleSync();
              }
            });
  }
  if (m_settings != nullptr) {
    connect(m_settings, &AppSettings::sunshineChanged, this, [this] { sync(); });
  }
  if (!detected()) {
    setStatus(QStringLiteral("Sunshine was not found. Install it from Omarchy's menu to stream "
                             "with Moonlight."));
  } else if (m_settings != nullptr &&
             (m_settings->sunshineOmakadeApp() || m_settings->sunshineGameApps())) {
    scheduleSync();
  } else {
    setStatus(QStringLiteral("Sunshine detected. Nothing is exported yet."));
  }
  if (detected()) {
    refreshRestartState();
  }
}

SunshineIntegration::~SunshineIntegration() {
  if (m_syncWatcher.isRunning()) {
    m_syncWatcher.waitForFinished();
  }
}

bool SunshineIntegration::streaming() { return qEnvironmentVariableIsSet("SUNSHINE_APP_ID"); }

QString SunshineIntegration::configuredOutputName(const QString& configPath) {
  QString path = configPath;
  if (path.isEmpty()) {
    const QString native = QDir::homePath() + QStringLiteral("/.config/sunshine/sunshine.conf");
    const QString flatpak = QDir::homePath() + QStringLiteral("/.var/app/") +
                            QLatin1String(kFlatpakId) + QStringLiteral("/config/sunshine.conf");
    path = QFileInfo::exists(native) ? native : flatpak;
  }
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return {};
  }
  const QRegularExpression expression(
      QStringLiteral("(?m)^\\s*output_name\\s*=\\s*([^#\\r\\n]+)"));
  const QRegularExpressionMatch match = expression.match(QString::fromUtf8(file.readAll()));
  if (!match.hasMatch()) {
    return {};
  }
  QString output = match.captured(1).trimmed();
  if (output.size() >= 2 &&
      ((output.startsWith(QLatin1Char('"')) && output.endsWith(QLatin1Char('"'))) ||
       (output.startsWith(QLatin1Char('\'')) && output.endsWith(QLatin1Char('\''))))) {
    output = output.mid(1, output.size() - 2).trimmed();
  }
  return output;
}

int SunshineIntegration::outputScreenIndex(const QString& configuredOutput,
                                            const QStringList& screenNames) {
  if (screenNames.isEmpty()) {
    return -1;
  }
  bool numeric = false;
  const int index = configuredOutput.toInt(&numeric);
  if (numeric && index >= 0 && index < screenNames.size()) {
    return index;
  }
  const qsizetype named = screenNames.indexOf(configuredOutput);
  return named >= 0 ? static_cast<int>(named) : 0;
}

void SunshineIntegration::detect() {
  const QString nativeDir = QDir::homePath() + QStringLiteral("/.config/sunshine");
  const QString flatpakDir =
      QDir::homePath() + QStringLiteral("/.var/app/") + QLatin1String(kFlatpakId) +
      QStringLiteral("/config/sunshine");
  if (!QStandardPaths::findExecutable(QStringLiteral("sunshine")).isEmpty() ||
      QFileInfo::exists(nativeDir + QStringLiteral("/apps.json"))) {
    m_appsPath = nativeDir + QStringLiteral("/apps.json");
    m_flatpak = false;
  } else if (flatpakAppInstalled(QLatin1String(kFlatpakId))) {
    m_appsPath = flatpakDir + QStringLiteral("/apps.json");
    m_flatpak = true;
  }
}

void SunshineIntegration::scheduleSync() {
  if (detected() && m_settings != nullptr &&
      (m_settings->sunshineOmakadeApp() || m_settings->sunshineGameApps())) {
    m_syncTimer.start();
  }
}

QString SunshineIntegration::serviceUnit() {
  // The Arch package installs app-dev.lizardbyte.app.Sunshine.service with a
  // sunshine.service alias that only exists once the unit is enabled, so prefer the real
  // unit name whenever its file is present.
  QString packaged = QStringLiteral("app-dev.lizardbyte.app.Sunshine.service");
  for (const QString& directory :
       {QStringLiteral("/usr/lib/systemd/user/"), QStringLiteral("/etc/systemd/user/"),
        QDir::homePath() + QStringLiteral("/.config/systemd/user/"),
        QDir::homePath() + QStringLiteral("/.local/share/systemd/user/")}) {
    if (QFileInfo::exists(directory + packaged)) {
      return packaged;
    }
  }
  return QStringLiteral("sunshine.service");
}

QString SunshineIntegration::shellQuote(const QString& value) {
  QString quoted = value;
  quoted.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
  return QLatin1Char('\'') + quoted + QLatin1Char('\'');
}

QString SunshineIntegration::commandPrefix(bool flatpakSunshine) {
  // Flatpak Sunshine runs commands inside its sandbox, so reach the host binary explicitly.
  if (flatpakSunshine) {
    return QStringLiteral("flatpak-spawn --host omakade");
  }
  const QString executable = QStandardPaths::findExecutable(QStringLiteral("omakade"));
  return executable.isEmpty() ? QStringLiteral("omakade") : shellQuote(executable);
}

bool SunshineIntegration::isOmakadeEntry(const QJsonObject& entry) {
  return entry.contains(QLatin1String(kMarker));
}

QJsonObject SunshineIntegration::omakadeEntry(const QString& prefix, const QString& imagePath) {
  // An empty cmd keeps the desktop stream alive like the stock Steam Big Picture entry, and
  // the undo step closes the window when the Moonlight session ends.
  QJsonObject entry;
  entry.insert(QStringLiteral("name"), QStringLiteral("Omakade"));
  entry.insert(QStringLiteral("cmd"), QStringLiteral(""));
  entry.insert(QStringLiteral("detached"), QJsonArray{prefix});
  QJsonObject undo;
  undo.insert(QStringLiteral("do"), QStringLiteral(""));
  undo.insert(QStringLiteral("undo"), prefix + QStringLiteral(" --quit"));
  entry.insert(QStringLiteral("prep-cmd"), QJsonArray{undo});
  if (!imagePath.isEmpty()) {
    entry.insert(QStringLiteral("image-path"), imagePath);
  }
  entry.insert(QLatin1String(kMarker), QStringLiteral("app"));
  return entry;
}

QJsonObject SunshineIntegration::gameEntry(const QString& title, const QString& launchKey,
                                           const QString& prefix, const QString& imagePath) {
  QJsonObject entry;
  entry.insert(QStringLiteral("name"), title);
  entry.insert(QStringLiteral("cmd"), QStringLiteral(""));
  entry.insert(QStringLiteral("detached"),
               QJsonArray{prefix + QStringLiteral(" --play ") + shellQuote(launchKey)});
  if (!imagePath.isEmpty()) {
    entry.insert(QStringLiteral("image-path"), imagePath);
  }
  entry.insert(QLatin1String(kMarker), launchKey);
  return entry;
}

QJsonObject SunshineIntegration::mergeEntries(const QJsonObject& existing,
                                              const QJsonArray& ours) {
  QJsonObject result = existing;
  QJsonArray apps;
  for (const auto& value : existing.value(QStringLiteral("apps")).toArray()) {
    if (!value.isObject() || !isOmakadeEntry(value.toObject())) {
      apps.append(value);
    }
  }
  for (const auto& value : ours) {
    apps.append(value);
  }
  result.insert(QStringLiteral("apps"), apps);
  if (!result.contains(QStringLiteral("env"))) {
    result.insert(QStringLiteral("env"), QJsonObject{});
  }
  return result;
}

QString SunshineIntegration::exportImage(const QString& imageRoot, const QString& sourcePath,
                                         const QString& name) {
  const QString source = localPath(sourcePath);
  if (source.isEmpty() || !QFileInfo::exists(source)) {
    return {};
  }
  // The source path is part of the name so a swapped cover gets fresh box art even when
  // the new file is older than the previous export.
  const QString hash = QString::fromLatin1(sha1((name + QChar::Null + source).toUtf8()).left(16));
  QString target = imageRoot + QLatin1Char('/') + hash + QStringLiteral(".png");
  const QFileInfo targetInfo(target);
  if (targetInfo.exists() &&
      targetInfo.lastModified() >= QFileInfo(source).lastModified()) {
    return target;
  }
  const bool vector = source.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive);
  QImageReader reader(source);
  reader.setAutoTransform(true);
  if (vector) {
    reader.setScaledSize(QSize(kBoxArtWidth * 2 / 3, kBoxArtWidth * 2 / 3));
  }
  const QImage image = reader.read();
  if (image.isNull()) {
    return {};
  }
  QDir().mkpath(imageRoot);
  QImage boxArt(kBoxArtWidth, kBoxArtHeight, QImage::Format_ARGB32_Premultiplied);
  boxArt.fill(QColor(24, 26, 30));
  QPainter painter(&boxArt);
  painter.setRenderHint(QPainter::SmoothPixmapTransform);
  const QImage scaled = vector ? image
                               : image.scaled(kBoxArtWidth, kBoxArtHeight,
                                              Qt::KeepAspectRatioByExpanding,
                                              Qt::SmoothTransformation);
  painter.drawImage((kBoxArtWidth - scaled.width()) / 2, (kBoxArtHeight - scaled.height()) / 2,
                    scaled);
  painter.end();
  return boxArt.save(target, "PNG") ? target : QString{};
}

bool SunshineIntegration::sync() {
  if (!detected() || m_games == nullptr || m_settings == nullptr) {
    return false;
  }
  if (m_busy) {
    // Finish the running export first, then run again with the latest library.
    m_syncPending = true;
    return true;
  }
  QVector<GameEntry> games;
  if (m_settings->sunshineGameApps()) {
    for (int row = 0; row < m_games->rowCount(); ++row) {
      const QModelIndex game = m_games->index(row, 0);
      if (game.data(GameRoles::Hidden).toBool() || !game.data(GameRoles::Installed).toBool()) {
        continue;
      }
      const QVariantMap installation = m_games->preferredInstallation(row);
      games.append({.title = game.data(GameRoles::Title).toString(),
                    .launchKey = LaunchKey{
                        .source = installation.value(QStringLiteral("source")).toString(),
                        .runner = installation.value(QStringLiteral("runner")).toString(),
                        .appId = installation.value(QStringLiteral("appId")).toString()}
                                     .toString(),
                    .coverSource = game.data(GameRoles::CoverPath).toString()});
    }
  }
  m_busy = true;
  emit stateChanged();
  m_syncWatcher.setFuture(QtConcurrent::run(
      [appsPath = m_appsPath, imageRoot = m_imageRoot, prefix = commandPrefix(m_flatpak),
       includeOmakade = m_settings->sunshineOmakadeApp(), iconSource = m_iconSource, games] {
        return runSync(appsPath, imageRoot, prefix, includeOmakade, iconSource, games);
      }));
  return true;
}

SunshineIntegration::SyncResult SunshineIntegration::runSync(
    const QString& appsPath, const QString& imageRoot, const QString& prefix,
    bool includeOmakade, const QString& iconSource, const QVector<GameEntry>& games) {
  SyncResult result;
  QFile file(appsPath);
  if (!file.open(QIODevice::ReadOnly)) {
    // Sunshine writes its default list on first start; never invent one for it.
    result.status = QStringLiteral("Start Sunshine once so it creates %1").arg(appsPath);
    return result;
  }
  const QByteArray previous = file.readAll();
  file.close();
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(previous, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    result.status =
        QStringLiteral("Could not read %1: %2").arg(appsPath, parseError.errorString());
    return result;
  }

  QJsonArray ours;
  QSet<QString> usedImages;
  if (includeOmakade) {
    const QString image = exportImage(imageRoot, iconSource, QStringLiteral("omakade"));
    usedImages.insert(image);
    ours.append(omakadeEntry(prefix, image));
  }
  QHash<QString, int> titleCounts;
  for (const GameEntry& game : games) {
    ++titleCounts[game.title.toLower()];
  }
  for (const GameEntry& game : games) {
    QString title = game.title;
    if (titleCounts.value(title.toLower()) > 1) {
      // Sunshine identifies apps by name, so two stores' copies need distinct names.
      title += QStringLiteral(" (%1)").arg(LaunchKey::parse(game.launchKey).source);
    }
    const QString image = exportImage(imageRoot, game.coverSource, game.launchKey);
    usedImages.insert(image);
    ours.append(gameEntry(title, game.launchKey, prefix, image));
  }
  result.games = static_cast<int>(games.size());
  result.entries = static_cast<int>(ours.size());

  const QJsonObject merged = mergeEntries(document.object(), ours);
  const bool unchanged = merged == document.object();
  if (!unchanged) {
    const QString backup = appsPath + QStringLiteral(".omakade-backup");
    if (!QFileInfo::exists(backup)) {
      QFile::copy(appsPath, backup);
    }
    const QByteArray contents = QJsonDocument(merged).toJson(QJsonDocument::Indented);
    QSaveFile output(appsPath);
    if (!output.open(QIODevice::WriteOnly) || output.write(contents) != contents.size() ||
        !output.commit()) {
      result.status = QStringLiteral("Could not write %1").arg(appsPath);
      return result;
    }
    QDir().mkpath(imageRoot);
    QSaveFile marker(writtenListMarker(imageRoot));
    if (marker.open(QIODevice::WriteOnly)) {
      marker.write(sha1(contents));
      marker.commit();
    }
    result.wrote = true;
  }
  // Drop box art for games that are no longer exported, now that nothing references it.
  for (const QFileInfo& image :
       QDir(imageRoot).entryInfoList({QStringLiteral("*.png")}, QDir::Files)) {
    if (!usedImages.contains(image.absoluteFilePath())) {
      QFile::remove(image.absoluteFilePath());
    }
  }
  result.okay = true;
  if (!result.wrote) {
    result.status = ours.isEmpty() ? QStringLiteral("Nothing is exported to Sunshine")
                                   : QStringLiteral("Sunshine app list is up to date");
  } else if (ours.isEmpty()) {
    result.status = QStringLiteral("Removed Omakade from Sunshine. Restart Sunshine to apply.");
  } else {
    result.status =
        QStringLiteral("Wrote %1 Sunshine app(s). Restart Sunshine to apply.").arg(ours.size());
  }
  return result;
}

void SunshineIntegration::finishSync() {
  const SyncResult result = m_syncWatcher.result();
  m_busy = false;
  if (result.okay) {
    m_exportedGames = result.games;
    if (result.wrote) {
      m_restartNeeded = true;
    }
  }
  setStatus(result.status);
  if (m_syncPending) {
    m_syncPending = false;
    sync();
  }
}

void SunshineIntegration::refreshRestartState() {
  QFile marker(writtenListMarker(m_imageRoot));
  QFile apps(m_appsPath);
  if (!marker.open(QIODevice::ReadOnly) || !apps.open(QIODevice::ReadOnly)) {
    return;
  }
  if (marker.readAll().trimmed() != sha1(apps.readAll())) {
    // Someone else wrote the list since; Sunshine's own web UI reloads on save.
    return;
  }
  auto* process = new QProcess(this);
  connect(process, &QProcess::finished, this, [this, process](int, QProcess::ExitStatus) {
    const QString output = QString::fromUtf8(process->readAllStandardOutput());
    process->deleteLater();
    qint64 activeSince = -1;
    bool active = false;
    for (const QString& line : output.split(QLatin1Char('\n'))) {
      if (line.startsWith(QStringLiteral("ActiveEnterTimestamp=@"))) {
        activeSince = line.mid(22).trimmed().toLongLong();
      } else if (line.startsWith(QStringLiteral("ActiveState="))) {
        active = line.mid(12).trimmed() == QStringLiteral("active");
      }
    }
    if (activeSince < 0 || m_busy) {
      return;
    }
    // Read the time now rather than before the query, so a write that landed meanwhile counts.
    const qint64 modified = QFileInfo(m_appsPath).lastModified().toSecsSinceEpoch();
    const bool needed = active && modified > activeSince;
    if (needed != m_restartNeeded) {
      m_restartNeeded = needed;
      emit stateChanged();
    }
  });
  connect(process, &QProcess::errorOccurred, process, &QObject::deleteLater);
  process->start(QStringLiteral("systemctl"),
                 {QStringLiteral("--user"), QStringLiteral("show"), QStringLiteral("--timestamp=unix"),
                  QStringLiteral("-p"), QStringLiteral("ActiveEnterTimestamp,ActiveState"),
                  serviceUnit()});
}

void SunshineIntegration::restartSunshine() {
  if (m_busy || streaming()) {
    return;
  }
  m_busy = true;
  emit stateChanged();
  auto* process = new QProcess(this);
  connect(process, &QProcess::finished, this,
          [this, process](int exitCode, QProcess::ExitStatus status) {
            m_busy = false;
            if (status == QProcess::NormalExit && exitCode == 0) {
              m_restartNeeded = false;
              setStatus(QStringLiteral("Sunshine restarted. Moonlight will show the new list."));
            } else {
              setStatus(QStringLiteral("Could not restart Sunshine: %1")
                            .arg(QString::fromUtf8(process->readAllStandardError()).trimmed()));
            }
            process->deleteLater();
          });
  connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError) {
    m_busy = false;
    setStatus(QStringLiteral("Could not run systemctl to restart Sunshine"));
    process->deleteLater();
  });
  process->start(QStringLiteral("systemctl"),
                 {QStringLiteral("--user"), QStringLiteral("restart"), serviceUnit()});
}

void SunshineIntegration::setStatus(const QString& text) {
  m_statusText = text;
  emit stateChanged();
}
