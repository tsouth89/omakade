#include "sources/heroic/HeroicScanner.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

#include <utility>

namespace {
constexpr qint64 kMaximumJsonBytes = 32 * 1024 * 1024;
constexpr qint64 kMaximumGogInfoBytes = 2 * 1024 * 1024;

bool validGogId(const QString& appId) {
  static const QRegularExpression valid(QStringLiteral("^[0-9]{1,20}$"));
  return valid.match(appId).hasMatch();
}

QString confinedPath(const QString& root, QString relative) {
  relative.replace(QLatin1Char('\\'), QLatin1Char('/'));
  if (relative.isEmpty() || QDir::isAbsolutePath(relative) ||
      QRegularExpression(QStringLiteral("^[A-Za-z]:")).match(relative).hasMatch()) {
    return {};
  }
  const QString cleanRelative = QDir::cleanPath(relative);
  if (cleanRelative == QStringLiteral("..") || cleanRelative.startsWith(QStringLiteral("../"))) {
    return {};
  }
  const QString cleanRoot = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
  const QString resolved = QDir::cleanPath(QDir(cleanRoot).absoluteFilePath(cleanRelative));
  if (resolved != cleanRoot && !resolved.startsWith(cleanRoot + QLatin1Char('/'))) {
    return {};
  }
  const QString canonicalRoot = QFileInfo(cleanRoot).canonicalFilePath();
  const QString canonicalResolved = QFileInfo(resolved).canonicalFilePath();
  if (!canonicalResolved.isEmpty() &&
      (canonicalRoot.isEmpty() ||
       (canonicalResolved != canonicalRoot &&
        !canonicalResolved.startsWith(canonicalRoot + QLatin1Char('/'))))) {
    return {};
  }
  return resolved;
}

std::optional<GogLaunchTask> readGogLaunchTask(const QString& installPath,
                                               const QString& appId) {
  if (!validGogId(appId)) {
    return std::nullopt;
  }
  QFile file(QDir(installPath).filePath(QStringLiteral("goggame-%1.info").arg(appId)));
  if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > kMaximumGogInfoBytes) {
    return std::nullopt;
  }
  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    return std::nullopt;
  }
  const QJsonArray tasks = document.object().value(QStringLiteral("playTasks")).toArray();
  QJsonObject selected;
  for (const QJsonValue& value : tasks) {
    const QJsonObject task = value.toObject();
    if (task.value(QStringLiteral("type")).toString() != QStringLiteral("FileTask")) {
      continue;
    }
    if (selected.isEmpty() || task.value(QStringLiteral("isPrimary")).toBool()) {
      selected = task;
    }
    if (task.value(QStringLiteral("isPrimary")).toBool()) {
      break;
    }
  }
  const QString executable =
      confinedPath(installPath, selected.value(QStringLiteral("path")).toString());
  if (executable.isEmpty() || !QFileInfo(executable).isFile()) {
    return std::nullopt;
  }
  QString workingDirectory = installPath;
  const QString configuredWorkingDirectory =
      selected.value(QStringLiteral("workingDir")).toString();
  if (!configuredWorkingDirectory.isEmpty()) {
    workingDirectory = confinedPath(installPath, configuredWorkingDirectory);
    if (workingDirectory.isEmpty() || !QFileInfo(workingDirectory).isDir()) {
      return std::nullopt;
    }
  }
  const QString suffix = QFileInfo(executable).suffix().toLower();
  return GogLaunchTask{.executablePath = executable,
                       .arguments = QProcess::splitCommand(
                           selected.value(QStringLiteral("arguments")).toString()),
                       .workingDirectory = workingDirectory,
                       .windows = suffix == QStringLiteral("exe") ||
                                  suffix == QStringLiteral("com") ||
                                  suffix == QStringLiteral("bat") ||
                                  suffix == QStringLiteral("lnk")};
}

struct Metadata {
  QString title;
  QString coverUrl;
  QString heroUrl;
};

struct Activity {
  int playtimeMinutes = 0;
  qint64 lastPlayed = 0;
};

QJsonDocument readJson(const QString& path, HeroicScanResult* result, bool required = true,
                       bool managedGogInventory = false) {
  QFile file(path);
  if (!file.exists()) {
    return {};
  }
  if (!file.open(QIODevice::ReadOnly) || file.size() > kMaximumJsonBytes) {
    if (required) {
      result->incomplete = true;
      result->managedGogIncomplete = result->managedGogIncomplete || managedGogInventory;
    }
    result->warnings.append(QStringLiteral("Could not read %1").arg(path));
    return {};
  }
  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
  if (error.error != QJsonParseError::NoError) {
    if (required) {
      result->incomplete = true;
      result->managedGogIncomplete = result->managedGogIncomplete || managedGogInventory;
    }
    result->warnings.append(
        QStringLiteral("Could not parse %1: %2").arg(path, error.errorString()));
    return {};
  }
  return document;
}

QHash<QString, Metadata> readMetadata(const QString& root, const QString& filename,
                                      const QString& key, HeroicScanResult* result) {
  QHash<QString, Metadata> metadata;
  const QJsonDocument document =
      readJson(root + QStringLiteral("/store_cache/") + filename, result, false);
  const QJsonArray games = document.object().value(key).toArray();
  for (const QJsonValue& value : games) {
    const QJsonObject game = value.toObject();
    const QString appId = game.value(QStringLiteral("app_name")).toVariant().toString();
    if (appId.isEmpty()) {
      continue;
    }
    metadata.insert(appId,
                    {.title = game.value(QStringLiteral("title")).toString(),
                     .coverUrl = game.value(QStringLiteral("art_square")).toString(),
                     .heroUrl = game.value(QStringLiteral("art_background"))
                                    .toString(game.value(QStringLiteral("art_cover")).toString())});
  }
  return metadata;
}

// Heroic records play sessions in store/timestamp.json as
// { "<appName>": { "firstPlayed": ISO, "lastPlayed": ISO, "totalPlayed": minutes } }.
QHash<QString, Activity> readActivity(const QString& root, HeroicScanResult* result) {
  QHash<QString, Activity> activity;
  const QJsonObject entries =
      readJson(root + QStringLiteral("/store/timestamp.json"), result, false).object();
  for (auto iterator = entries.begin(); iterator != entries.end(); ++iterator) {
    const QJsonObject entry = iterator.value().toObject();
    Activity value;
    const double minutes = entry.value(QStringLiteral("totalPlayed")).toDouble();
    value.playtimeMinutes =
        minutes > 0 && minutes < 60.0 * 24 * 365 * 200 ? static_cast<int>(minutes) : 0;
    const QString played = entry.value(QStringLiteral("lastPlayed")).toString();
    QDateTime lastPlayed = QDateTime::fromString(played, Qt::ISODateWithMs);
    if (!lastPlayed.isValid()) {
      lastPlayed = QDateTime::fromString(played, Qt::ISODate);
    }
    if (lastPlayed.isValid()) {
      value.lastPlayed = lastPlayed.toSecsSinceEpoch();
    }
    activity.insert(iterator.key(), value);
  }
  return activity;
}

QString cachedArtwork(const QString& root, const QString& appId, const QString& url) {
  if (!appId.isEmpty()) {
    const QString iconBase = root + QStringLiteral("/icons/") + appId;
    for (const QString& extension : {QStringLiteral(".jpg"), QStringLiteral(".png")}) {
      if (QFileInfo(iconBase + extension).isFile()) {
        return iconBase + extension;
      }
    }
  }
  if (url.startsWith(QStringLiteral("http://")) || url.startsWith(QStringLiteral("https://"))) {
    const QString digest = QString::fromLatin1(
        QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha256).toHex());
    const QString cached = root + QStringLiteral("/images-cache/") + digest;
    if (QFileInfo(cached).isFile()) {
      return cached;
    }
  }
  return {};
}

void appendGame(HeroicScanResult* result, QSet<QString>* keys, const QString& root,
                const QString& runner, const QString& appId, const QString& fallbackTitle,
                const QString& installPath, const Metadata& metadata, const Activity& activity,
                bool flatpak) {
  const QString key = runner + QLatin1Char(':') + appId;
  const QString title = metadata.title.isEmpty() ? fallbackTitle : metadata.title;
  if (appId.isEmpty() || title.trimmed().isEmpty() || keys->contains(key)) {
    return;
  }
  result->games.append({.key = key,
                        .appId = appId,
                        .runner = runner,
                        .title = title.trimmed(),
                        .installPath = installPath,
                        .coverPath = cachedArtwork(root, appId, metadata.coverUrl),
                        .heroPath = cachedArtwork(root, QString{}, metadata.heroUrl),
                        .playtimeMinutes = activity.playtimeMinutes,
                        .lastPlayed = activity.lastPlayed,
                        .flatpak = flatpak});
  keys->insert(key);
}

void scanLegendary(const QString& root, bool flatpak, const QHash<QString, Activity>& activity,
                   HeroicScanResult* result, QSet<QString>* keys) {
  QString path = root + QStringLiteral("/legendaryConfig/legendary/installed.json");
  if (!QFileInfo(path).isFile()) {
    path = QFileInfo(root).dir().absoluteFilePath(QStringLiteral("legendary/installed.json"));
  }
  if (!QFileInfo(path).isFile()) {
    return;
  }
  const auto metadata = readMetadata(root, QStringLiteral("legendary_library.json"),
                                     QStringLiteral("library"), result);
  const QJsonObject games = readJson(path, result).object();
  for (auto iterator = games.begin(); iterator != games.end(); ++iterator) {
    const QJsonObject game = iterator.value().toObject();
    if (game.value(QStringLiteral("is_dlc")).toBool()) {
      continue;
    }
    const QString appId = game.value(QStringLiteral("app_name")).toString(iterator.key());
    appendGame(result, keys, root, QStringLiteral("legendary"), appId,
               game.value(QStringLiteral("title")).toString(appId),
               game.value(QStringLiteral("install_path")).toString(), metadata.value(appId),
               activity.value(appId), flatpak);
  }
}

void scanGog(const QString& root, bool flatpak, const QHash<QString, Activity>& activity,
             HeroicScanResult* result, QSet<QString>* keys) {
  const QString path = root + QStringLiteral("/gog_store/installed.json");
  if (!QFileInfo(path).isFile()) {
    return;
  }
  const auto metadata =
      readMetadata(root, QStringLiteral("gog_library.json"), QStringLiteral("games"), result);
  const QJsonArray games = readJson(path, result, true, true)
                               .object()
                               .value(QStringLiteral("installed"))
                               .toArray();
  for (const QJsonValue& value : games) {
    const QJsonObject game = value.toObject();
    if (game.value(QStringLiteral("is_dlc")).toBool()) {
      continue;
    }
    const QString appId = game.value(QStringLiteral("appName")).toVariant().toString();
    const QString installPath = game.value(QStringLiteral("install_path")).toString();
    QString title = appId;
    const QJsonDocument info = readJson(
        installPath + QStringLiteral("/goggame-") + appId + QStringLiteral(".info"), result, false);
    if (info.isObject()) {
      title = info.object().value(QStringLiteral("name")).toString(title);
    }
    appendGame(result, keys, root, QStringLiteral("gog"), appId, title, installPath,
               metadata.value(appId), activity.value(appId), flatpak);
  }
}

void scanNile(const QString& root, bool flatpak, const QHash<QString, Activity>& activity,
              HeroicScanResult* result, QSet<QString>* keys) {
  const QString base = root + QStringLiteral("/nile_config/nile");
  const QString path = base + QStringLiteral("/installed.json");
  if (!QFileInfo(path).isFile()) {
    return;
  }
  QHash<QString, Metadata> metadata;
  const QJsonArray library =
      readJson(base + QStringLiteral("/library.json"), result, false).array();
  for (const QJsonValue& value : library) {
    const QJsonObject product = value.toObject().value(QStringLiteral("product")).toObject();
    const QString appId = product.value(QStringLiteral("id")).toVariant().toString();
    const QJsonObject detail = product.value(QStringLiteral("productDetail")).toObject();
    const QJsonObject details = detail.value(QStringLiteral("details")).toObject();
    metadata.insert(
        appId,
        {.title = product.value(QStringLiteral("title")).toString(),
         .coverUrl = detail.value(QStringLiteral("iconUrl")).toString(),
         .heroUrl = details.value(QStringLiteral("backgroundUrl1"))
                        .toString(details.value(QStringLiteral("backgroundUrl2")).toString())});
  }
  const QJsonArray installed = readJson(path, result).array();
  for (const QJsonValue& value : installed) {
    const QJsonObject game = value.toObject();
    const QString appId = game.value(QStringLiteral("id")).toVariant().toString();
    appendGame(result, keys, root, QStringLiteral("nile"), appId, appId,
               game.value(QStringLiteral("path")).toString(), metadata.value(appId),
               activity.value(appId), flatpak);
  }
}

// Games added by hand live in sideload_apps/library.json. Unlike the store files this list
// keeps uninstalled entries, so is_installed must be honoured.
void scanSideload(const QString& root, bool flatpak, const QHash<QString, Activity>& activity,
                  HeroicScanResult* result, QSet<QString>* keys) {
  const QString path = root + QStringLiteral("/sideload_apps/library.json");
  if (!QFileInfo(path).isFile()) {
    return;
  }
  const QJsonArray games =
      readJson(path, result, false).object().value(QStringLiteral("games")).toArray();
  for (const QJsonValue& value : games) {
    const QJsonObject game = value.toObject();
    const QJsonObject install = game.value(QStringLiteral("install")).toObject();
    const QString runner =
        game.value(QStringLiteral("runner")).toString(QStringLiteral("sideload"));
    if (runner != QStringLiteral("sideload") ||
        !game.value(QStringLiteral("is_installed")).toBool() ||
        install.value(QStringLiteral("is_dlc")).toBool()) {
      continue;
    }
    const QString appId = game.value(QStringLiteral("app_name")).toString();
    QString installPath = game.value(QStringLiteral("folder_name")).toString();
    if (installPath.isEmpty()) {
      installPath = QFileInfo(install.value(QStringLiteral("executable")).toString()).path();
    }
    const Metadata metadata{.title = game.value(QStringLiteral("title")).toString(),
                            .coverUrl = game.value(QStringLiteral("art_square")).toString(),
                            .heroUrl = game.value(QStringLiteral("art_cover")).toString()};
    appendGame(result, keys, root, runner, appId, appId, installPath, metadata,
               activity.value(appId), flatpak);
  }
}

bool scanLooseGog(const QString& root, HeroicScanResult* result, QSet<QString>* keys,
                  const QSet<QString>& managedInstallPaths) {
  bool foundManifest = false;
  QStringList directories{root};
  const QDir rootDirectory(root);
  for (const QFileInfo& child :
       rootDirectory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable)) {
    directories.append(child.absoluteFilePath());
  }
  for (const QString& directory : directories) {
    if (managedInstallPaths.contains(QDir::cleanPath(directory))) {
      continue;
    }
    const QDir gameDirectory(directory);
    const QFileInfoList manifests = gameDirectory.entryInfoList(
        {QStringLiteral("goggame-*.info")}, QDir::Files | QDir::Readable, QDir::Name);
    foundManifest = foundManifest || !manifests.isEmpty();
    for (const QFileInfo& manifest : manifests) {
      static const QRegularExpression idPattern(QStringLiteral("^goggame-([0-9]{1,20})\\.info$"));
      const QRegularExpressionMatch match = idPattern.match(manifest.fileName());
      if (!match.hasMatch()) {
        continue;
      }
      const QString appId = match.captured(1);
      const QString key = QStringLiteral("gog-direct:") + appId;
      if (keys->contains(key)) {
        continue;
      }
      QFile file(manifest.absoluteFilePath());
      if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 ||
          file.size() > kMaximumGogInfoBytes) {
        result->gogIncomplete = true;
        result->warnings.append(QStringLiteral("Could not read %1").arg(manifest.absoluteFilePath()));
        continue;
      }
      QJsonParseError error;
      const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
      const QString title = document.object().value(QStringLiteral("name")).toString().trimmed();
      if (error.error != QJsonParseError::NoError || !document.isObject()) {
        result->gogIncomplete = true;
        result->warnings.append(
            QStringLiteral("Could not parse GOG manifest %1").arg(manifest.absoluteFilePath()));
        continue;
      }
      if (title.isEmpty() || !readGogLaunchTask(directory, appId).has_value()) {
        result->warnings.append(
            QStringLiteral("Could not use GOG manifest %1").arg(manifest.absoluteFilePath()));
        continue;
      }
      result->games.append({.key = key,
                            .appId = appId,
                            .runner = QStringLiteral("gog-direct"),
                            .title = title,
                            .installPath = directory,
                            .coverPath = {},
                            .heroPath = {}});
      keys->insert(key);
    }
  }
  return foundManifest;
}
} // namespace

QStringList HeroicScanner::discoverRoots(const QStringList& extraGogRoots) {
  const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
  const QString home = QDir::homePath();
  QStringList candidates = {
      config + QStringLiteral("/heroic"),
      home + QStringLiteral("/.var/app/com.heroicgameslauncher.hgl/config/heroic"),
      home + QStringLiteral("/GOG Games"), home + QStringLiteral("/Games/GOG"),
      home + QStringLiteral("/Games/Heroic"), home + QStringLiteral("/Games")};
  QStringList roots;
  for (const QString& root : candidates) {
    if (QFileInfo(root).isDir()) roots.append(root);
  }
  // Explicit paths remain in the scan when a drive is absent, so it can be reported
  // and its cached games retained. Environment paths add to saved and standard roots.
  QStringList configured = extraGogRoots;
  configured.append(qEnvironmentVariable("OMAKADE_GOG_LIBRARY_PATHS")
                        .split(QDir::listSeparator(), Qt::SkipEmptyParts));
  for (const QString& path : configured) {
    if (QDir::isAbsolutePath(path)) roots.append(QDir::cleanPath(path));
  }
  roots.removeDuplicates();
  return roots;
}

HeroicScanResult HeroicScanner::scan(const QStringList& roots) {
  HeroicScanResult result;
  QSet<QString> keys;
  QStringList validRoots;
  for (const QString& root : roots) {
    if (!QFileInfo(root).isDir() || !QDir(root).isReadable()) {
      result.unavailableGogRoots.append(QDir::cleanPath(root));
      result.warnings.append(QStringLiteral("GOG folder unavailable: %1").arg(root));
      continue;
    }
    if (validRoots.contains(root)) continue;
    validRoots.append(root);
    const bool flatpak = root.contains(QStringLiteral("/.var/app/com.heroicgameslauncher.hgl/"));
    const int before = result.games.size();
    const QHash<QString, Activity> activity = readActivity(root, &result);
    scanLegendary(root, flatpak, activity, &result, &keys);
    scanGog(root, flatpak, activity, &result, &keys);
    scanNile(root, flatpak, activity, &result, &keys);
    scanSideload(root, flatpak, activity, &result, &keys);
    const bool hasLegendary =
        QFileInfo(root + QStringLiteral("/legendaryConfig/legendary/installed.json")).isFile() ||
        QFileInfo(root).dir().exists(QStringLiteral("legendary/installed.json"));
    if (result.games.size() > before || hasLegendary ||
        QFileInfo(root + QStringLiteral("/gog_store/installed.json")).isFile() ||
        QFileInfo(root + QStringLiteral("/nile_config/nile/installed.json")).isFile() ||
        QFileInfo(root + QStringLiteral("/sideload_apps/library.json")).isFile()) {
      result.roots.append(root);
    }
  }
  QSet<QString> managedGogInstallPaths;
  for (const HeroicGameRecord& game : std::as_const(result.games)) {
    if (game.runner == QStringLiteral("gog")) {
      managedGogInstallPaths.insert(QDir::cleanPath(game.installPath));
    }
  }
  // Without the managed inventory, ownership of loose manifests is unknown.
  if (result.managedGogIncomplete) {
    result.gogIncomplete = true;
    return result;
  }
  for (const QString& root : std::as_const(validRoots)) {
    if (!QDir(root).isReadable()) {
      result.gogIncomplete = true;
      result.warnings.append(QStringLiteral("Could not read GOG library %1").arg(root));
      continue;
    }
    // An existing empty directory is a completed scan, including after uninstall.
    result.gogRoots.append(root);
    const int before = result.games.size();
    const bool foundManifest =
        scanLooseGog(root, &result, &keys, managedGogInstallPaths);
    if ((foundManifest || result.games.size() > before) && !result.roots.contains(root)) {
      result.roots.append(root);
    }
  }
  return result;
}

std::optional<GogLaunchTask> HeroicScanner::gogLaunchTask(const QString& installPath,
                                                          const QString& appId) {
  return readGogLaunchTask(installPath, appId);
}
