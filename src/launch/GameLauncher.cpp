#include "launch/GameLauncher.h"
#include "library/ManualGameModel.h"

#include "launch/SteamLauncher.h"
#include "sources/FlatpakInstall.h"
#include "sources/battlenet/BattleNetScanner.h"
#include "sources/heroic/HeroicScanner.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrlQuery>

namespace {
bool validLutrisId(const QString& id) {
  static const QRegularExpression digits(QStringLiteral("^[1-9][0-9]*$"));
  return digits.match(id).hasMatch();
}

bool validPcsx2Id(const QString& id) {
  // PCSX2 boots a disc image path; accept any non-empty path: key
  // (existence is checked at launch time), like RetroArch.
  static const QRegularExpression serial(
      QStringLiteral("^[A-Za-z]{4}-[0-9A-Za-z]{5}$"));
  if (serial.match(id).hasMatch()) {
    return true;
  }
  return id.startsWith(QStringLiteral("path:")) && id.size() > 5;
}

bool validRyujinxId(const QString& id) {
  // Ryujinx launches a ROM file path; title ids are display metadata only.
  // QProcess passes this as one argument without a shell, so ordinary filename punctuation is
  // safe. Validate the actual invariant instead: a bounded, non-empty path with a ROM suffix.
  const QString path = id.startsWith(QStringLiteral("path:")) ? id.mid(5) : id;
  if (path.isEmpty() || path.size() > 4096 || path.contains(QChar::Null)) {
    return false;
  }
  const QString suffix = QFileInfo(path).suffix();
  return suffix.compare(QStringLiteral("xci"), Qt::CaseInsensitive) == 0 ||
         suffix.compare(QStringLiteral("nsp"), Qt::CaseInsensitive) == 0 ||
         suffix.compare(QStringLiteral("nro"), Qt::CaseInsensitive) == 0;
}

bool validHeroicTarget(const QString& id, const QString& runner) {
  static const QRegularExpression appId(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,255}$"));
  return appId.match(id).hasMatch() &&
         (runner == QStringLiteral("legendary") || runner == QStringLiteral("gog") ||
          runner == QStringLiteral("nile") || runner == QStringLiteral("sideload"));
}

bool validGogId(const QString& id) {
  static const QRegularExpression appId(QStringLiteral("^[0-9]{1,20}$"));
  return appId.match(id).hasMatch();
}

bool validFaugusId(const QString& id) {
  static const QRegularExpression gameId(
      QStringLiteral("^[\\p{L}\\p{N}_-][\\p{L}\\p{N}_.-]{0,254}$"),
      QRegularExpression::UseUnicodePropertiesOption);
  return gameId.match(id).hasMatch();
}

bool installedTargetExists(const QString& path) {
  if (QFileInfo::exists(path)) {
    return true;
  }
  const qsizetype archiveSeparator = path.indexOf(QLatin1Char('#'));
  return archiveSeparator > 0 && QFileInfo::exists(path.left(archiveSeparator));
}

bool validBattleNetId(const QString& id) {
  static const QRegularExpression product(
      QStringLiteral("^[A-Za-z][A-Za-z0-9._-]{0,63}(@[a-f0-9]{8})?$"));
  return product.match(id).hasMatch();
}

bool validBattleNetPrefix(const QString& prefix) {
  const QString cleaned = QDir::cleanPath(prefix);
  return cleaned.startsWith(QLatin1Char('/')) && !cleaned.contains(QStringLiteral("/../")) &&
         !cleaned.contains(QChar::Null);
}

QString battleNetExecutable(const QString& prefix) {
  const QStringList candidates = {
      prefix + QStringLiteral("/drive_c/Program Files (x86)/Battle.net/Battle.net.exe"),
      prefix + QStringLiteral("/drive_c/Program Files/Battle.net/Battle.net.exe"),
      prefix + QStringLiteral("/drive_c/Program Files (x86)/Battle.net/Battle.net Launcher.exe"),
      prefix + QStringLiteral("/drive_c/Program Files/Battle.net/Battle.net Launcher.exe")};
  for (const QString& candidate : candidates) {
    if (QFileInfo(candidate).isFile()) {
      return candidate;
    }
  }
  return candidates.constFirst();
}

QString wineExecutable() {
  const QString wine = QStandardPaths::findExecutable(QStringLiteral("wine"));
  return wine.isEmpty() ? QStandardPaths::findExecutable(QStringLiteral("wine64")) : wine;
}

QString installedRyujinxFlatpakId() {
  for (const QString& appId : {QStringLiteral("io.github.ryubing.Ryujinx"),
                               QStringLiteral("org.ryujinx.Ryujinx")}) {
    if (flatpakAppInstalled(appId)) {
      return appId;
    }
  }
  return {};
}

QString bottlesBottleName(const QString& prefix) {
  QFile file(prefix + QStringLiteral("/bottle.yml"));
  if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    static const QRegularExpression name(
        QStringLiteral("(?m)^Name:\\s*[\"']?([^\"'\\n]+?)[\"']?\\s*$"));
    const QRegularExpressionMatch match = name.match(QString::fromUtf8(file.readAll()));
    if (match.hasMatch()) {
      const QString value = match.captured(1).trimmed();
      if (!value.isEmpty()) {
        return value;
      }
    }
  }
  return QFileInfo(prefix).fileName();
}
} // namespace

GameLauncher::GameLauncher(QObject* parent) : QObject(parent) {}

QString GameLauncher::lastError() const { return m_lastError; }

LaunchCommand GameLauncher::lutrisCommand(const QString& id, bool flatpak) {
  if (!validLutrisId(id)) {
    return {};
  }
  const QString target = QStringLiteral("lutris:rungameid/%1").arg(id);
  return flatpak
             ? LaunchCommand{QStringLiteral("flatpak"),
                             {QStringLiteral("run"), QStringLiteral("net.lutris.Lutris"), target}}
             : LaunchCommand{QStringLiteral("lutris"), {target}};
}

LaunchCommand GameLauncher::heroicCommand(const QString& id, const QString& runner, bool flatpak) {
  if (!validHeroicTarget(id, runner)) {
    return {};
  }
  QUrl url(QStringLiteral("heroic://launch"));
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("appName"), id);
  query.addQueryItem(QStringLiteral("runner"), runner);
  query.addQueryItem(QStringLiteral("gui"), QStringLiteral("false"));
  url.setQuery(query);
  const QString target = url.toString(QUrl::FullyEncoded);
  return flatpak
             ? LaunchCommand{QStringLiteral("flatpak"),
                             {QStringLiteral("run"), QStringLiteral("com.heroicgameslauncher.hgl"),
                              QStringLiteral("--no-gui"), target}}
             : LaunchCommand{QStringLiteral("heroic"), {QStringLiteral("--no-gui"), target}};
}

LaunchCommand GameLauncher::faugusCommand(const QString& id, bool flatpak) {
  if (!validFaugusId(id)) {
    return {};
  }
  return flatpak
             ? LaunchCommand{QStringLiteral("flatpak"),
                             {QStringLiteral("run"),
                              QStringLiteral("--command=/app/bin/faugus-launcher"),
                              QStringLiteral("io.github.Faugus.faugus-launcher"),
                              QStringLiteral("--game"), id}}
             : LaunchCommand{QStringLiteral("faugus-launcher"),
                             {QStringLiteral("--game"), id}};
}

LaunchCommand GameLauncher::retroArchCommand(const QString& contentPath, const QString& corePath,
                                             bool flatpak) {
  if (contentPath.trimmed().isEmpty() || corePath.trimmed().isEmpty() ||
      corePath == QStringLiteral("DETECT")) {
    return {};
  }
  return flatpak ? LaunchCommand{QStringLiteral("flatpak"),
                                 {QStringLiteral("run"), QStringLiteral("org.libretro.RetroArch"),
                                  QStringLiteral("-L"), corePath, contentPath}}
                 : LaunchCommand{QStringLiteral("retroarch"),
                                 {QStringLiteral("-L"), corePath, contentPath}};
}

LaunchCommand GameLauncher::pcsx2Command(const QString& id, bool isElf, bool flatpak) {
  if (!validPcsx2Id(id)) {
    return {};
  }
  // PCSX2 boots a disc image by passing its path directly; ELF entries need -elf <file>.
  if (!id.startsWith(QStringLiteral("path:"))) {
    return {};
  }
  const QString target = id.mid(5);
  QStringList arguments;
  if (isElf) {
    arguments << QStringLiteral("-elf") << target;
  } else {
    arguments << target;
  }
  if (flatpak) {
    QStringList flatpakArguments{QStringLiteral("run"), QStringLiteral("net.pcsx2.PCSX2"),
                                 QStringLiteral("--")};
    flatpakArguments = flatpakArguments + arguments;
    return LaunchCommand{QStringLiteral("flatpak"), flatpakArguments};
  }
  return LaunchCommand{QStringLiteral("pcsx2-qt"), arguments};
}

LaunchCommand GameLauncher::ryujinxCommand(const QString& id, const QString& nativeExecutable,
                                           const QString& flatpakAppId) {
  if (!validRyujinxId(id)) {
    return {};
  }
  const QString target = id.startsWith(QStringLiteral("path:")) ? id.mid(5) : id;
  if (!nativeExecutable.isEmpty()) {
    return LaunchCommand{nativeExecutable, {target}};
  }
  if (flatpakAppId.isEmpty()) {
    return {};
  }
  return LaunchCommand{QStringLiteral("flatpak"),
                       {QStringLiteral("run"), flatpakAppId, QStringLiteral("--"), target}};
}

LaunchCommand GameLauncher::battleNetCommand(const QString& id, const QString& prefix,
                                             const QString& runner, bool flatpak) {
  if (!validBattleNetId(id) || !validBattleNetPrefix(prefix) ||
      (runner != QStringLiteral("wine") && runner != QStringLiteral("bottles") &&
       runner != QStringLiteral("proton"))) {
    return {};
  }
  const QString launchCode = BattleNetScanner::launchCodeForProduct(id);
  const QString exe = battleNetExecutable(prefix);
  const QString execArg = QStringLiteral("--exec=launch %1").arg(launchCode);
  if (runner == QStringLiteral("bottles")) {
    const QString bottle = bottlesBottleName(prefix);
    if (bottle.isEmpty()) {
      return {};
    }
    return flatpak ? LaunchCommand{QStringLiteral("flatpak"),
                                   {QStringLiteral("run"), QStringLiteral("--command=bottles-cli"),
                                    QStringLiteral("com.usebottles.bottles"), QStringLiteral("run"),
                                    QStringLiteral("-b"), bottle, QStringLiteral("-e"), exe,
                                    QStringLiteral("--"), execArg}}
                   : LaunchCommand{QStringLiteral("bottles-cli"),
                                   {QStringLiteral("run"), QStringLiteral("-b"), bottle,
                                    QStringLiteral("-e"), exe, QStringLiteral("--"), execArg}};
  }
  if (runner == QStringLiteral("proton")) {
    QStringList arguments{QStringLiteral("WINEPREFIX=%1").arg(prefix)};
    if (QFileInfo(prefix + QStringLiteral("/version")).isFile()) {
      arguments.append({QStringLiteral("GAMEID=umu-battlenet"),
                        QStringLiteral("STORE=battlenet"), QStringLiteral("PROTONPATH=GE-Proton"),
                        QStringLiteral("PROTON_VERB=run")});
    }
    arguments.append({QStringLiteral("umu-run"), exe, execArg});
    return {QStringLiteral("env"), arguments};
  }
  return {QStringLiteral("wine"), {exe, execArg}};
}

LaunchCommand GameLauncher::gogCommand(const QString& id, const QString& installPath,
                                       const QString& winePrefix) {
  if (!validGogId(id)) {
    return {};
  }
  const std::optional<GogLaunchTask> task = HeroicScanner::gogLaunchTask(installPath, id);
  if (!task.has_value()) {
    return {};
  }
  if (!task->windows) {
    return {task->executablePath, task->arguments};
  }
  const QString prefix =
      winePrefix.isEmpty()
          ? QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
                QStringLiteral("/omakade/gog-prefixes/") + id
          : winePrefix;
  QStringList arguments{QStringLiteral("WINEPREFIX=%1").arg(prefix),
                        QStringLiteral("GAMEID=umu-default"), QStringLiteral("STORE=gog"),
                        QStringLiteral("umu-run"), task->executablePath};
  arguments.append(task->arguments);
  return {QStringLiteral("env"), arguments};
}

bool GameLauncher::launch(const QString& source, const QString& id, bool flatpak,
                          const QString& runner, const QString& installPath,
                          const QString& launchTarget) {
  if (source.compare(QStringLiteral("Manual"), Qt::CaseInsensitive) == 0) {
    QString program, directory, error;
    QStringList arguments;
    if (!ManualGameModel::validateLaunch(launchTarget, id, &program, &arguments, &directory, &error)) {
      setError(error);
      return false;
    }
    if (!QProcess::startDetached(program, arguments, directory)) {
      setError(QStringLiteral("Could not start this native game. Check its executable and permissions."));
      return false;
    }
    setError({});
    return true;
  }
  if (source.compare(QStringLiteral("Faugus"), Qt::CaseInsensitive) != 0 &&
      !installPath.isEmpty() && !installedTargetExists(installPath)) {
    setError(QStringLiteral(
                 "The installed files are missing. Rescan or repair this game in %1.")
                 .arg(source));
    return false;
  }
  if (source.compare(QStringLiteral("Steam"), Qt::CaseInsensitive) == 0) {
    const QUrl url = SteamLauncher::launchUrl(id);
    if (!url.isValid() || url.isEmpty()) {
      setError(QStringLiteral("This game has an invalid Steam App ID."));
      return false;
    }
    if (!QDesktopServices::openUrl(url)) {
      setError(QStringLiteral("Steam could not open the game. Check that Steam is installed."));
      return false;
    }
    setError({});
    return true;
  }
  if (source.compare(QStringLiteral("Lutris"), Qt::CaseInsensitive) == 0) {
    return launchLutris(id, flatpak, false);
  }
  if (source.compare(QStringLiteral("Heroic"), Qt::CaseInsensitive) == 0) {
    return launchHeroic(id, runner, flatpak, false);
  }
  if (source.compare(QStringLiteral("GOG"), Qt::CaseInsensitive) == 0) {
    return launchGog(id, installPath, false);
  }
  if (source.compare(QStringLiteral("Faugus"), Qt::CaseInsensitive) == 0) {
    return launchFaugus(id, flatpak, false);
  }
  if (source.compare(QStringLiteral("RetroArch"), Qt::CaseInsensitive) == 0) {
    return launchRetroArch(installPath, launchTarget, flatpak, false);
  }
  if (source.compare(QStringLiteral("PCSX2"), Qt::CaseInsensitive) == 0) {
    return launchPcsx2(id, launchTarget == QStringLiteral("elf"), flatpak, false);
  }
  if (source.compare(QStringLiteral("Ryujinx"), Qt::CaseInsensitive) == 0) {
    // Title-id ids must launch by the stored ROM path; path: ids carry it already.
    const bool idIsPath = id.startsWith(QStringLiteral("path:"));
    const QString romTarget =
        idIsPath ? id : (launchTarget.isEmpty() ? id : launchTarget);
    return launchRyujinx(romTarget, flatpak, runner, false);
  }
  if (source.compare(QStringLiteral("Battle.net"), Qt::CaseInsensitive) == 0) {
    return launchBattleNet(id, launchTarget, runner, flatpak, false);
  }
  setError(QStringLiteral("%1 games cannot be launched yet.").arg(source));
  return false;
}

bool GameLauncher::manage(const QString& source, const QString& id, bool flatpak,
                          const QString& runner, const QString& launchTarget) {
  if (source.compare(QStringLiteral("Steam"), Qt::CaseInsensitive) == 0) {
    const QUrl url = SteamLauncher::manageUrl(id);
    if (!url.isValid() || url.isEmpty() || !QDesktopServices::openUrl(url)) {
      setError(QStringLiteral("Steam could not open the game details."));
      return false;
    }
    setError({});
    return true;
  }
  if (source.compare(QStringLiteral("Lutris"), Qt::CaseInsensitive) == 0) {
    return launchLutris(id, flatpak, true);
  }
  if (source.compare(QStringLiteral("Heroic"), Qt::CaseInsensitive) == 0) {
    return launchHeroic(id, runner, flatpak, true);
  }
  if (source.compare(QStringLiteral("GOG"), Qt::CaseInsensitive) == 0) {
    return launchGog(id, {}, true);
  }
  if (source.compare(QStringLiteral("Faugus"), Qt::CaseInsensitive) == 0) {
    return launchFaugus(id, flatpak, true);
  }
  if (source.compare(QStringLiteral("RetroArch"), Qt::CaseInsensitive) == 0) {
    return launchRetroArch({}, {}, flatpak, true);
  }
  if (source.compare(QStringLiteral("PCSX2"), Qt::CaseInsensitive) == 0) {
    return launchPcsx2(id, false, flatpak, true);
  }
  if (source.compare(QStringLiteral("Ryujinx"), Qt::CaseInsensitive) == 0) {
    return launchRyujinx(id, flatpak, runner, true);
  }
  if (source.compare(QStringLiteral("Battle.net"), Qt::CaseInsensitive) == 0) {
    return launchBattleNet(id, launchTarget, runner, flatpak, true);
  }
  setError(QStringLiteral("%1 does not provide game management yet.").arg(source));
  return false;
}

bool GameLauncher::install(const QString& source, const QString& id) {
  if (source.compare(QStringLiteral("Steam"), Qt::CaseInsensitive) != 0) {
    setError(QStringLiteral("%1 games cannot be installed here yet.").arg(source));
    return false;
  }
  const QUrl url = SteamLauncher::installUrl(id);
  if (!url.isValid() || url.isEmpty()) {
    setError(QStringLiteral("This game has an invalid Steam App ID."));
    return false;
  }
  if (!QDesktopServices::openUrl(url)) {
    setError(QStringLiteral("Steam could not start the installation."));
    return false;
  }
  setError({});
  return true;
}

bool GameLauncher::launchLutris(const QString& id, bool flatpak, bool manageOnly) {
  const QString executable = flatpak ? QStringLiteral("flatpak") : QStringLiteral("lutris");
  const bool available = !QStandardPaths::findExecutable(executable).isEmpty();
  LaunchCommand command;
  if (manageOnly) {
    command = flatpak ? LaunchCommand{QStringLiteral("flatpak"),
                                      {QStringLiteral("run"), QStringLiteral("net.lutris.Lutris")}}
                      : LaunchCommand{QStringLiteral("lutris"), {}};
  } else {
    command = lutrisCommand(id, flatpak);
  }
  if (!command.isValid()) {
    setError(QStringLiteral("This game has an invalid Lutris ID."));
    return false;
  }
  if (!available) {
    setError(flatpak ? QStringLiteral("Flatpak is not installed.")
                     : QStringLiteral("Lutris is not installed."));
    return false;
  }
  if (flatpak) {
    const QString error = flatpakError(QStringLiteral("net.lutris.Lutris"),
                                       QStringLiteral("Lutris"));
    if (!error.isEmpty()) {
      setError(error);
      return false;
    }
  }
  if (!QProcess::startDetached(command.program, command.arguments)) {
    setError(QStringLiteral("Lutris could not be started. Open Lutris and try again."));
    return false;
  }
  setError({});
  return true;
}

bool GameLauncher::launchHeroic(const QString& id, const QString& runner, bool flatpak,
                                bool manageOnly) {
  const QString executable = flatpak ? QStringLiteral("flatpak") : QStringLiteral("heroic");
  const bool available = !QStandardPaths::findExecutable(executable).isEmpty();
  LaunchCommand command;
  if (manageOnly) {
    command =
        flatpak
            ? LaunchCommand{QStringLiteral("flatpak"),
                            {QStringLiteral("run"), QStringLiteral("com.heroicgameslauncher.hgl")}}
            : LaunchCommand{QStringLiteral("heroic"), {}};
  } else {
    command = heroicCommand(id, runner, flatpak);
  }
  if (!command.isValid()) {
    setError(QStringLiteral("This game has an invalid Heroic target."));
    return false;
  }
  if (!available) {
    setError(flatpak ? QStringLiteral("Flatpak is not installed.")
                     : QStringLiteral("Heroic is not installed."));
    return false;
  }
  if (flatpak) {
    const QString error = flatpakError(QStringLiteral("com.heroicgameslauncher.hgl"),
                                       QStringLiteral("Heroic"));
    if (!error.isEmpty()) {
      setError(error);
      return false;
    }
  }
  if (!QProcess::startDetached(command.program, command.arguments)) {
    setError(QStringLiteral("Heroic could not be started. Open Heroic and try again."));
    return false;
  }
  setError({});
  return true;
}

bool GameLauncher::launchGog(const QString& id, const QString& installPath, bool manageOnly) {
  if (manageOnly) {
    if (!QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.gog.com/account")))) {
      setError(QStringLiteral("GOG could not open your library."));
      return false;
    }
    setError({});
    return true;
  }
  const LaunchCommand command = gogCommand(id, installPath);
  if (!command.isValid()) {
    setError(QStringLiteral("This GOG installation does not provide a safe launch task."));
    return false;
  }
  if (command.program == QStringLiteral("env") &&
      QStandardPaths::findExecutable(QStringLiteral("umu-run")).isEmpty()) {
    setError(QStringLiteral("Install umu-launcher to run this Windows GOG game."));
    return false;
  }
  if (command.program == QStringLiteral("env")) {
    const QString prefix = command.arguments.constFirst().mid(QStringLiteral("WINEPREFIX=").size());
    if (!QDir().mkpath(prefix)) {
      setError(QStringLiteral("Omakade could not create the GOG Wine prefix."));
      return false;
    }
  }
  const std::optional<GogLaunchTask> task = HeroicScanner::gogLaunchTask(installPath, id);
  if (!task.has_value() ||
      !QProcess::startDetached(command.program, command.arguments, task->workingDirectory)) {
    setError(QStringLiteral("GOG could not start this game."));
    return false;
  }
  setError({});
  return true;
}

bool GameLauncher::launchFaugus(const QString& id, bool flatpak, bool manageOnly) {
  const QString executable = flatpak ? QStringLiteral("flatpak")
                                     : QStringLiteral("faugus-launcher");
  if (QStandardPaths::findExecutable(executable).isEmpty()) {
    setError(flatpak ? QStringLiteral("Flatpak is not installed.")
                     : QStringLiteral("Faugus is not installed."));
    return false;
  }
  if (flatpak) {
    const QString error = flatpakError(QStringLiteral("io.github.Faugus.faugus-launcher"),
                                       QStringLiteral("Faugus"));
    if (!error.isEmpty()) {
      setError(error);
      return false;
    }
  }
  const LaunchCommand command =
      manageOnly
          ? (flatpak
                 ? LaunchCommand{QStringLiteral("flatpak"),
                                 {QStringLiteral("run"),
                                  QStringLiteral("io.github.Faugus.faugus-launcher")}}
                 : LaunchCommand{QStringLiteral("faugus-launcher"), {}})
          : faugusCommand(id, flatpak);
  if (!command.isValid()) {
    setError(QStringLiteral("This game has an invalid Faugus ID."));
    return false;
  }
  if (!QProcess::startDetached(command.program, command.arguments)) {
    setError(QStringLiteral("Faugus could not be started. Open Faugus and try again."));
    return false;
  }
  setError({});
  return true;
}

bool GameLauncher::launchRetroArch(const QString& contentPath, const QString& corePath,
                                   bool flatpak, bool manageOnly) {
  const QString executable = flatpak ? QStringLiteral("flatpak") : QStringLiteral("retroarch");
  if (QStandardPaths::findExecutable(executable).isEmpty()) {
    setError(flatpak ? QStringLiteral("Flatpak is not installed.")
                     : QStringLiteral("RetroArch is not installed."));
    return false;
  }
  if (flatpak) {
    const QString error =
        flatpakError(QStringLiteral("org.libretro.RetroArch"), QStringLiteral("RetroArch"));
    if (!error.isEmpty()) {
      setError(error);
      return false;
    }
  }
  const LaunchCommand command =
      manageOnly
          ? (flatpak
                 ? LaunchCommand{QStringLiteral("flatpak"),
                                 {QStringLiteral("run"), QStringLiteral("org.libretro.RetroArch")}}
                 : LaunchCommand{QStringLiteral("retroarch"), {}})
          : retroArchCommand(contentPath, corePath, flatpak);
  if (!command.isValid()) {
    setError(QStringLiteral("Set a core association for this game in RetroArch, then rescan."));
    return false;
  }
  if (!QProcess::startDetached(command.program, command.arguments)) {
    setError(QStringLiteral("RetroArch could not be started. Open RetroArch and try again."));
    return false;
  }
  setError({});
  return true;
}

bool GameLauncher::launchPcsx2(const QString& id, bool isElf, bool flatpak, bool manageOnly) {
  const QString executable = flatpak ? QStringLiteral("flatpak") : QStringLiteral("pcsx2-qt");
  if (QStandardPaths::findExecutable(executable).isEmpty()) {
    setError(flatpak ? QStringLiteral("Flatpak is not installed.")
                     : QStringLiteral("PCSX2 is not installed."));
    return false;
  }
  if (flatpak) {
    const QString error =
        flatpakError(QStringLiteral("net.pcsx2.PCSX2"), QStringLiteral("PCSX2"));
    if (!error.isEmpty()) {
      setError(error);
      return false;
    }
  }
  const LaunchCommand command =
      manageOnly
          ? (flatpak ? LaunchCommand{QStringLiteral("flatpak"),
                                     {QStringLiteral("run"), QStringLiteral("net.pcsx2.PCSX2")}}
                     : LaunchCommand{QStringLiteral("pcsx2-qt"), {}})
          : pcsx2Command(id, isElf, flatpak);
  if (!command.isValid()) {
    setError(QStringLiteral("This game has an invalid PCSX2 target."));
    return false;
  }
  if (!QProcess::startDetached(command.program, command.arguments)) {
    setError(QStringLiteral("PCSX2 could not be started. Open PCSX2 and try again."));
    return false;
  }
  setError({});
  return true;
}

bool GameLauncher::launchRyujinx(const QString& id, bool flatpak, const QString& configuredAppId,
                                 bool manageOnly) {
  QString nativeExecutable;
  QString flatpakAppId;
  if (!flatpak) {
    // Native installs ship the binary under different names depending on the package.
    for (const QString& candidate :
         {QStringLiteral("ryujinx-wrapper"), QStringLiteral("Ryujinx"),
          QStringLiteral("ryujinx")}) {
      if (!QStandardPaths::findExecutable(candidate).isEmpty()) {
        nativeExecutable = candidate;
        break;
      }
    }
    if (nativeExecutable.isEmpty()) {
      setError(QStringLiteral("Ryujinx is not installed."));
      return false;
    }
  }
  if (flatpak) {
    flatpakAppId = configuredAppId;
    if (flatpakAppId != QStringLiteral("io.github.ryubing.Ryujinx") &&
        flatpakAppId != QStringLiteral("org.ryujinx.Ryujinx")) {
      flatpakAppId = installedRyujinxFlatpakId();
    }
    if (flatpakAppId.isEmpty()) {
      setError(QStringLiteral("The Ryujinx Flatpak is not installed."));
      return false;
    }
    const QString error = flatpakError(flatpakAppId, QStringLiteral("Ryujinx"));
    if (!error.isEmpty()) {
      setError(error);
      return false;
    }
  }
  const LaunchCommand command =
      manageOnly
          ? (flatpak ? LaunchCommand{QStringLiteral("flatpak"),
                                     {QStringLiteral("run"), flatpakAppId}}
                     : LaunchCommand{nativeExecutable, {}})
          : ryujinxCommand(id, nativeExecutable, flatpakAppId);
  if (!command.isValid()) {
    setError(QStringLiteral("This game has an invalid Ryujinx target."));
    return false;
  }
  if (!QProcess::startDetached(command.program, command.arguments)) {
    setError(QStringLiteral("Ryujinx could not be started. Open Ryujinx and try again."));
    return false;
  }
  setError({});
  return true;
}

bool GameLauncher::launchBattleNet(const QString& id, const QString& prefix, const QString& runner,
                                   bool flatpak, bool manageOnly) {
  LaunchCommand command = battleNetCommand(id, prefix, runner, flatpak);
  if (!command.isValid()) {
    setError(QStringLiteral("This game has an invalid Battle.net target."));
    return false;
  }
  const QString exe = battleNetExecutable(prefix);
  if (!QFileInfo(exe).isFile()) {
    setError(QStringLiteral(
        "Battle.net is not installed in this Wine prefix. Install it, then rescan."));
    return false;
  }
  const QString execArg =
      QStringLiteral("--exec=launch %1").arg(BattleNetScanner::launchCodeForProduct(id));
  const bool bottlesRunner = runner == QStringLiteral("bottles");
  const bool protonRunner = runner == QStringLiteral("proton");
  const bool wineRunner = runner == QStringLiteral("wine");
  if (!bottlesRunner && !protonRunner && !wineRunner) {
    setError(QStringLiteral("This game has an invalid Battle.net runner."));
    return false;
  }
  if (bottlesRunner && QStandardPaths::findExecutable(command.program).isEmpty()) {
    setError(flatpak ? QStringLiteral("Flatpak is not installed.")
                     : QStringLiteral("Bottles is not installed."));
    return false;
  }
  if (bottlesRunner && flatpak) {
    const QString error =
        flatpakError(QStringLiteral("com.usebottles.bottles"), QStringLiteral("Bottles"));
    if (!error.isEmpty()) {
      setError(error);
      return false;
    }
  }
  if (protonRunner && QStandardPaths::findExecutable(QStringLiteral("umu-run")).isEmpty()) {
    setError(QStringLiteral(
        "umu-launcher is not installed. Install it to launch Battle.net games from Proton."));
    return false;
  }
  if (wineRunner) {
    const QString wine = wineExecutable();
    if (wine.isEmpty()) {
      setError(QStringLiteral("Wine is not installed."));
      return false;
    }
    command = {wine, manageOnly ? QStringList{exe} : QStringList{exe, execArg}};
  } else if (manageOnly && !command.arguments.isEmpty() &&
             command.arguments.constLast().startsWith(QStringLiteral("--exec="))) {
    command.arguments.removeLast();
    if (!command.arguments.isEmpty() && command.arguments.constLast() == QStringLiteral("--")) {
      command.arguments.removeLast();
    }
  }

  QProcess process;
  process.setProgram(command.program);
  process.setArguments(command.arguments);
  process.setWorkingDirectory(QFileInfo(exe).absolutePath());
  QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
  environment.insert(QStringLiteral("WINEPREFIX"), QDir::cleanPath(prefix));
  if (protonRunner) {
    const bool omarchyStyleProton = QFileInfo(prefix + QStringLiteral("/version")).isFile();
    environment.insert(QStringLiteral("GAMEID"), omarchyStyleProton
                                                       ? QStringLiteral("umu-battlenet")
                                                       : QStringLiteral("0"));
    if (!omarchyStyleProton) {
      environment.insert(QStringLiteral("STEAM_COMPAT_DATA_PATH"),
                         QFileInfo(prefix + QStringLiteral("/..")).absoluteFilePath());
    }
  }
  process.setProcessEnvironment(environment);
  if (!process.startDetached()) {
    setError(QStringLiteral("Battle.net could not be started. Open Battle.net and try again."));
    return false;
  }
  setError({});
  return true;
}

QString GameLauncher::flatpakError(const QString& appId, const QString& launcherName) const {
  return flatpakAppInstalled(appId)
             ? QString{}
             : QStringLiteral("The %1 Flatpak is not installed.").arg(launcherName);
}

void GameLauncher::setError(const QString& error) {
  if (m_lastError == error) {
    return;
  }
  m_lastError = error;
  emit lastErrorChanged();
}
