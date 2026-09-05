#include "launch/SteamLauncher.h"

#include <QDesktopServices>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

#include "sources/FlatpakInstall.h"

namespace {
bool validAppId(const QString& appId) {
  static const QRegularExpression digits(QStringLiteral("^[1-9][0-9]*$"));
  return digits.match(appId).hasMatch();
}

QString launchTargetId(const QString& appId) {
  if (!validAppId(appId)) {
    return {};
  }
  bool numeric = false;
  const quint64 id = appId.toULongLong(&numeric);
  if (!numeric || id == 0) {
    return {};
  }
  // Non-Steam shortcuts store a 32-bit ID with the high bit set. Steam launches
  // them with the 64-bit Big Picture ID: (appid << 32) | 0x02000000.
  if (id <= 0xFFFFFFFFull && (id & 0x80000000ull) != 0) {
    return QString::number((id << 32) | 0x02000000ull);
  }
  return appId;
}
} // namespace

SteamLauncher::SteamLauncher(QObject* parent) : QObject(parent) {}

QString SteamLauncher::lastError() const { return m_lastError; }

QUrl SteamLauncher::launchUrl(const QString& appId) {
  const QString target = launchTargetId(appId);
  return target.isEmpty() ? QUrl{} : QUrl(QStringLiteral("steam://rungameid/%1").arg(target));
}

QUrl SteamLauncher::manageUrl(const QString& appId) {
  return validAppId(appId) ? QUrl(QStringLiteral("steam://nav/games/details/%1").arg(appId))
                           : QUrl{};
}

QUrl SteamLauncher::installUrl(const QString& appId) {
  return validAppId(appId) ? QUrl(QStringLiteral("steam://install/%1").arg(appId)) : QUrl{};
}

QList<LaunchCommand> SteamLauncher::steamCommands(const QUrl& url, const QString& steamExecutable,
                                                  bool flatpakSteamInstalled) {
  if (!url.isValid() || url.isEmpty() || url.scheme() != QStringLiteral("steam")) {
    return {};
  }
  const QString target = url.toString(QUrl::FullyEncoded);
  QList<LaunchCommand> commands;
  if (!steamExecutable.isEmpty()) {
    commands.append(LaunchCommand{steamExecutable, {target}});
  }
  if (flatpakSteamInstalled) {
    commands.append(
        LaunchCommand{QStringLiteral("flatpak"),
                      {QStringLiteral("run"), QStringLiteral("com.valvesoftware.Steam"), target}});
  }
  return commands;
}

bool SteamLauncher::openUrl(const QUrl& url) {
  if (!url.isValid() || url.isEmpty()) {
    return false;
  }
  const QList<LaunchCommand> commands =
      steamCommands(url, QStandardPaths::findExecutable(QStringLiteral("steam")),
                    flatpakAppInstalled(QStringLiteral("com.valvesoftware.Steam")));
  for (const LaunchCommand& command : commands) {
    if (QProcess::startDetached(command.program, command.arguments)) {
      return true;
    }
  }
  return QDesktopServices::openUrl(url);
}

bool SteamLauncher::launch(const QString& appId) { return open(launchUrl(appId)); }

bool SteamLauncher::manage(const QString& appId) { return open(manageUrl(appId)); }

bool SteamLauncher::install(const QString& appId) { return open(installUrl(appId)); }

bool SteamLauncher::open(const QUrl& url) {
  QString error;
  if (!url.isValid() || url.isEmpty()) {
    error = QStringLiteral("This game has an invalid Steam App ID.");
  } else if (!openUrl(url)) {
    error = QStringLiteral("Steam could not open the game. Check that Steam is installed.");
  }
  if (m_lastError != error) {
    m_lastError = error;
    emit lastErrorChanged();
  }
  return error.isEmpty();
}
