#include "achievements/AchievementModel.h"
#include "achievements/RetroAchievementsApi.h"
#include "achievements/RetroAchievementsHasher.h"
#include "achievements/RetroAchievementsService.h"

#include <zip.h>
#include "achievements/SteamAchievementApi.h"
#include "app/AppSettings.h"
#include "backup/BackupArchive.h"
#include "backup/BackupSnapshot.h"
#include "backup/BackupDatabase.h"
#include "app/SingleInstance.h"
#include "input/ControllerInput.h"
#include "input/CouchCursorManager.h"
#include "launch/GameLauncher.h"
#include "launch/PlayRequest.h"
#include "launch/SteamLauncher.h"
#include "library/BattleNetGameModel.h"
#include "library/FaugusGameModel.h"
#include "library/GameRoles.h"
#include "library/HeroicGameModel.h"
#include "library/LibraryFilterModel.h"
#include "library/LutrisGameModel.h"
#include "library/MockGameModel.h"
#include "library/ManualGameModel.h"
#include "library/Pcsx2GameModel.h"
#include "library/RyujinxGameModel.h"
#include "library/RetroArchGameModel.h"
#include "library/SteamGameModel.h"
#include "library/SteamOwnedGamesApi.h"
#include "library/UnifiedGameModel.h"
#include "metadata/GameInsightsService.h"
#include "metadata/IgdbApi.h"
#include "sources/battlenet/BattleNetScanner.h"
#include "sources/faugus/FaugusScanner.h"
#include "sources/heroic/HeroicScanner.h"
#include "sources/pcsx2/Pcsx2Scanner.h"
#include "sources/ryujinx/RyujinxScanner.h"
#include "sources/lutris/LutrisScanner.h"
#include "sources/retroarch/RetroArchScanner.h"
#include "sources/steam/SteamScanner.h"
#include "sources/steam/ValveKeyValues.h"
#include "streaming/SunshineIntegration.h"
#include "theme/OmarchyTheme.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QWindow>

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>

namespace {
// A launcher-style source that, like Lutris or Heroic, has no Installed role at all.
class LauncherOnlyModel final : public QAbstractListModel {
public:
  explicit LauncherOnlyModel(QString title, QString coverPath = {}, QObject* parent = nullptr)
      : QAbstractListModel(parent), m_title(std::move(title)), m_coverPath(std::move(coverPath)) {}
  [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override {
    return parent.isValid() ? 0 : 1;
  }
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override {
    if (!index.isValid()) {
      return {};
    }
    switch (role) {
    case GameRoles::Title:
      return m_title;
    case GameRoles::Source:
      return QStringLiteral("Lutris");
    case GameRoles::AppId:
      return QStringLiteral("celeste");
    case GameRoles::Runner:
      return QString{};
    case GameRoles::CoverPath:
      return m_coverPath.isEmpty() ? QString{} : QUrl::fromLocalFile(m_coverPath).toString();
    case GameRoles::Hidden:
    case GameRoles::Favorite:
      return false;
    default:
      return {};
    }
  }
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override {
    return {{GameRoles::Title, "title"},
            {GameRoles::Source, "source"},
            {GameRoles::AppId, "appId"},
            {GameRoles::Runner, "runner"},
            {GameRoles::CoverPath, "coverPath"},
            {GameRoles::Hidden, "hidden"}};
  }

private:
  QString m_title;
  QString m_coverPath;
};

class DelayedSteamModel final : public QAbstractListModel {
public:
  [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override {
    return !parent.isValid() && m_available ? 1 : 0;
  }
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override {
    if (!m_available || !index.isValid() || index.row() != 0) {
      return {};
    }
    switch (role) {
    case GameRoles::Title:
      return QStringLiteral("Portal 2");
    case GameRoles::Source:
      return QStringLiteral("Steam");
    case GameRoles::Runner:
      return QString{};
    case GameRoles::AppId:
      return QStringLiteral("620");
    case GameRoles::Installed:
      return true;
    default:
      return {};
    }
  }
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override {
    return {{GameRoles::Title, "title"},
            {GameRoles::Source, "source"},
            {GameRoles::Runner, "runner"},
            {GameRoles::AppId, "appId"},
            {GameRoles::Installed, "installed"}};
  }
  void publish() {
    beginInsertRows({}, 0, 0);
    m_available = true;
    endInsertRows();
  }

private:
  bool m_available = false;
};

void writeFile(const QString& path, const QByteArray& contents) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(file.errorString()));
  QCOMPARE(file.write(contents), contents.size());
}

QByteArray binaryCString(const QByteArray& value) {
  QByteArray out = value;
  out.append('\0');
  return out;
}

QByteArray binaryKey(char type, const QByteArray& key) {
  QByteArray out;
  out.append(type);
  out.append(binaryCString(key));
  return out;
}

QByteArray binaryStringField(const QByteArray& key, const QByteArray& value) {
  return binaryKey(0x01, key) + binaryCString(value);
}

QByteArray binaryIntField(const QByteArray& key, quint32 value) {
  QByteArray out = binaryKey(0x02, key);
  out.append(char(value & 0xff));
  out.append(char((value >> 8) & 0xff));
  out.append(char((value >> 16) & 0xff));
  out.append(char((value >> 24) & 0xff));
  return out;
}

QByteArray sampleShortcutsVdf(const QByteArray& iconPath) {
  QByteArray shortcut = binaryKey(0x00, "0");
  shortcut += binaryIntField("appid", 3236138358u);
  shortcut += binaryStringField("AppName", "Soulframe");
  shortcut += binaryStringField("Exe", "/games/Soulframe/Launcher.exe");
  shortcut += binaryStringField("StartDir", "/games/Soulframe");
  shortcut += binaryStringField("icon", iconPath);
  shortcut += binaryIntField("LastPlayTime", 1700000001);
  shortcut += binaryKey(0x00, "tags");
  shortcut += char(0x08);
  shortcut += char(0x08);

  QByteArray skipped = binaryKey(0x00, "1");
  skipped += binaryIntField("appid", 1);
  skipped += binaryStringField("AppName", "");
  skipped += char(0x08);

  QByteArray shortcuts = binaryKey(0x00, "shortcuts");
  shortcuts += shortcut;
  shortcuts += skipped;
  shortcuts += char(0x08);
  shortcuts += char(0x08);
  return shortcuts;
}

auto redirectCacheHome(const QString& path) {
  const bool wasSet = qEnvironmentVariableIsSet("XDG_CACHE_HOME");
  const QByteArray previous = qgetenv("XDG_CACHE_HOME");
  qputenv("XDG_CACHE_HOME", path.toUtf8());
  return qScopeGuard([wasSet, previous] {
    if (wasSet) {
      qputenv("XDG_CACHE_HOME", previous);
    } else {
      qunsetenv("XDG_CACHE_HOME");
    }
  });
}

QByteArray sampleTheme(const QByteArray& accent = "#7aa2f7") {
  return "mode = \"dark\"\n"
         "accent = \"" +
         accent +
         "\"\n"
         "selection = \"#292e42\"\n"
         "muted = \"#414868\"\n"
         "background = \"#1a1b26\"\n"
         "dark_background = \"#13141c\"\n"
         "darker_background = \"#0e0e14\"\n"
         "lighter_background = \"#24283b\"\n"
         "foreground = \"#a9b1d6\"\n"
         "dark_foreground = \"#565f89\"\n"
         "light_foreground = \"#b4bee6\"\n"
         "bright_foreground = \"#c0caf5\"\n"
         "red = \"#f7768e\"\n"
         "yellow = \"#e0af68\"\n"
         "green = \"#9ece6a\"\n"
         "cyan = \"#449dab\"\n"
         "blue = \"#7aa2f7\"\n"
         "magenta = \"#ad8ee6\"\n";
}

QByteArray manifest(const QByteArray& appId, const QByteArray& name,
                    const QByteArray& installDirectory) {
  return "\"AppState\"\n{\n\"appid\" \"" + appId + "\"\n\"name\" \"" + name +
         "\"\n\"StateFlags\" \"4\"\n\"installdir\" \"" + installDirectory + "\"\n}\n";
}

void createSteamFixture(const QString& root, const QString& secondLibrary) {
  const QByteArray folders = QStringLiteral("\"libraryfolders\"\n{\n\"0\" { \"path\" \"%1\" }\n"
                                            "\"1\" { \"path\" \"%2\" }\n}\n")
                                 .arg(root, secondLibrary)
                                 .toUtf8();
  writeFile(root + QStringLiteral("/config/libraryfolders.vdf"), folders);
  writeFile(root + QStringLiteral("/steamapps/appmanifest_10.acf"),
            manifest("10", "Counter-Strike", "Counter-Strike"));
  writeFile(root + QStringLiteral("/steamapps/appmanifest_1070560.acf"),
            manifest("1070560", "Steam Linux Runtime 1.0 (scout)", "SteamLinuxRuntime"));
  writeFile(secondLibrary + QStringLiteral("/steamapps/appmanifest_20.acf"),
            manifest("20", "Team Fortress Classic", "Team Fortress Classic"));
  writeFile(secondLibrary + QStringLiteral("/steamapps/appmanifest_10.acf"),
            manifest("10", "Counter-Strike", "Counter-Strike"));
  writeFile(root + QStringLiteral("/appcache/librarycache/10/library_600x900.jpg"), "cover");
  writeFile(root + QStringLiteral("/appcache/librarycache/20/header.jpg"), "landscape");
  writeFile(root + QStringLiteral("/appcache/librarycache/20/library_hero.jpg"), "hero");
  writeFile(root + QStringLiteral("/appcache/librarycache/20/hash/library_capsule.jpg"),
            "portrait");
  writeFile(root + QStringLiteral("/userdata/42/config/grid/10p.png"), "custom cover");
  writeFile(
      root + QStringLiteral("/userdata/42/config/librarycache/10.json"),
      R"([["achievements",{"data":{"nAchieved":1,"nTotal":2,"vecHighlight":[{"strID":"WIN_ONE","strName":"First Win","strDescription":"Win once","strImage":"","bAchieved":true,"rtUnlocked":1700000000,"flAchieved":42.5}],"vecUnachieved":[{"strID":"WIN_TWO","strName":"Second Win","strDescription":"Win twice","strImage":"","bAchieved":false,"flAchieved":20.0}],"vecAchievedHidden":[]}}]])");
  writeFile(root + QStringLiteral("/userdata/42/config/librarycache/achievement_progress.json"),
            R"({"mapCache":[[10,{"unlocked":1,"total":2}]]})");
  writeFile(root + QStringLiteral("/userdata/42/config/localconfig.vdf"),
            "\"UserLocalConfigStore\" { \"Software\" { \"Valve\" { \"Steam\" { \"apps\" { "
            "\"10\" { \"LastPlayed\" \"1700000000\" \"Playtime\" \"125\" } } } } } }\n");
}

void createLutrisFixture(const QString& dataRoot) {
  QDir().mkpath(dataRoot);
  const QString connection =
      QStringLiteral("lutris-fixture-%1").arg(QUuid::createUuid().toString());
  {
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    database.setDatabaseName(dataRoot + QStringLiteral("/pga.db"));
    QVERIFY(database.open());
    QSqlQuery query(database);
    QVERIFY(query.exec(QStringLiteral(
        "CREATE TABLE games (id INTEGER PRIMARY KEY, slug TEXT, name TEXT, runner TEXT, directory "
        "TEXT, platform TEXT, year INTEGER, lastplayed INTEGER, playtime REAL, installed INTEGER, "
        "configpath TEXT)")));
    QVERIFY(query.exec(QStringLiteral(
        "INSERT INTO games VALUES(7, 'signal-hill', 'Signal Hill', 'wine', '/games/signal', "
        "'Windows', 2024, 1700000000, 2.5, 1, 'signal-hill')")));
    QVERIFY(query.exec(QStringLiteral(
        "INSERT INTO games VALUES(8, 'not-installed', 'Not Installed', 'linux', '/games/no', "
        "'Linux', 2020, 0, 0, 0, 'not-installed')")));
    QVERIFY(query.exec(QStringLiteral(
        "INSERT INTO games VALUES(9, 'broken', 'Broken Install', 'wine', '/games/broken', "
        "'Windows', 2020, 0, 0, 1, '')")));
    database.close();
  }
  QSqlDatabase::removeDatabase(connection);
  writeFile(dataRoot + QStringLiteral("/coverart/signal-hill.jpg"), "cover");
}

void createHeroicFixture(const QString& root) {
  writeFile(
      root + QStringLiteral("/legendaryConfig/legendary/installed.json"),
      R"({"EpicApp":{"app_name":"EpicApp","title":"Epic Voyage","install_path":"/games/epic","is_dlc":false},"EpicDlc":{"app_name":"EpicDlc","title":"Epic DLC","install_path":"/games/dlc","is_dlc":true}})");
  writeFile(
      root + QStringLiteral("/store_cache/legendary_library.json"),
      R"({"library":[{"app_name":"EpicApp","title":"Epic Voyage","art_square":"https://example.test/epic-cover.jpg","art_background":"https://example.test/epic-hero.jpg"}]})");
  writeFile(root + QStringLiteral("/icons/EpicApp.jpg"), "cover");

  writeFile(root + QStringLiteral("/gog_store/installed.json"),
            R"({"installed":[{"appName":"12345","install_path":")" +
                (root + QStringLiteral("/gog-game")).toUtf8() + R"(","is_dlc":false}]})");
  writeFile(root + QStringLiteral("/gog-game/goggame-12345.info"),
            R"({"name":"GOG Quest","playTasks":[{"isPrimary":true,"type":"FileTask","path":"start.sh","arguments":"--profile \"couch mode\"","workingDir":""}]})");
  writeFile(root + QStringLiteral("/gog-game/start.sh"), "#!/bin/sh\n");
  writeFile(root + QStringLiteral("/store_cache/gog_library.json"),
            R"({"games":[{"app_name":"12345","title":"GOG Quest","art_square":""}]})");

  writeFile(root + QStringLiteral("/nile_config/nile/installed.json"),
            R"([{"id":"amazon-game","version":"1","path":"/games/amazon"}])");
  writeFile(
      root + QStringLiteral("/nile_config/nile/library.json"),
      R"([{"product":{"id":"amazon-game","title":"Amazon Trail","productDetail":{"iconUrl":"","details":{"backgroundUrl1":""}}}}])");

  writeFile(
      root + QStringLiteral("/sideload_apps/library.json"),
      R"({"games":[{"runner":"sideload","app_name":"j661Z9rpxqYRZSp45Jh92i","title":"Scott Pilgrim EX","install":{"executable":"/home/user/Games/Scott/Game.exe","platform":"Windows","is_dlc":false},"folder_name":"/home/user/Games/Scott","art_cover":"https://cdn2.steamgriddb.com/grid/cover.png","is_installed":true,"art_square":"https://cdn2.steamgriddb.com/grid/square.png"},{"runner":"sideload","app_name":"removedApp","title":"Removed","install":{"executable":"","platform":"Windows","is_dlc":false},"folder_name":"","is_installed":false}]})");
  writeFile(root + QStringLiteral("/images-cache/") +
                QString::fromLatin1(
                    QCryptographicHash::hash("https://cdn2.steamgriddb.com/grid/square.png",
                                             QCryptographicHash::Sha256)
                        .toHex()),
            "sideload cover");
  writeFile(
      root + QStringLiteral("/store/timestamp.json"),
      R"({"EpicApp":{"firstPlayed":"2026-08-01T20:00:00.000Z","lastPlayed":"2026-08-30T21:15:00.000Z","totalPlayed":125}})");
}

void createFaugusFixture(const QString& root) {
  writeFile(
      root + QStringLiteral("/games.json"),
      R"([{"gameid":"signal-hill","title":"Signal Hill","path":"~/Games/Signal/signal.exe","runner":"GE-Proton","playtime":7200},{"gameid":"linux-tool","title":"Linux Tool","path":"/games/linux-tool","runner":"Linux","playtime":0},{"gameid":"pokémon-外伝","title":"Pokémon 外伝","path":"/games/pokemon"},{"gameid":"signal-hill","title":"Duplicate","path":"/games/duplicate"},{"gameid":"bad;id","title":"Unsafe","path":"/games/unsafe"},{"gameid":"missing-path","title":"Missing Path","path":""}])");
  writeFile(root + QStringLiteral("/covers/signal-hill.png"), "cover");
  writeFile(root + QStringLiteral("/banners/signal-hill.png"), "hero");
  writeFile(root + QStringLiteral("/icons/linux-tool.png"), "icon");
}

void createBattleNetFixture(const QString& prefix) {
  writeFile(prefix + QStringLiteral("/drive_c/Program Files (x86)/Battle.net/Battle.net.exe"),
            "mz");
  writeFile(prefix + QStringLiteral("/drive_c/Program Files (x86)/World of Warcraft/cover.png"),
            "cover");
  writeFile(prefix + QStringLiteral("/drive_c/Program Files (x86)/Overwatch/library_hero.jpg"),
            "hero");
  writeFile(prefix + QStringLiteral("/drive_c/Program Files (x86)/Custom/game.exe"), "exe");
  writeFile(prefix + QStringLiteral("/drive_c/Program Files (x86)/Custom/icon.ico"), "icon");
  writeFile(prefix + QStringLiteral("/drive_c/users/steamuser/AppData/Roaming/Battle.net/"
                                    "Battle.net.config"),
            R"({"Games":{"wow":{"LastPlayed":1700000000},"Pro":{"LastPlayed":1700001000}}})");
  writeFile(prefix + QStringLiteral("/drive_c/ProgramData/Battle.net/Agent/product.db"),
            BattleNetScanner::encodeProductDb(
                {{QStringLiteral("agent"), QStringLiteral("agent"),
                  QStringLiteral("C:\\ProgramData\\Battle.net\\Agent"), true, true},
                 {QStringLiteral("wow"), QStringLiteral("wow"),
                  QStringLiteral("C:\\Program Files (x86)\\World of Warcraft"), true, true},
                 {QStringLiteral("pro"), QStringLiteral("pro"),
                  QStringLiteral("C:\\Program Files (x86)\\Overwatch"), true, true},
                 {QStringLiteral("d3"), QStringLiteral("d3"),
                  QStringLiteral("C:\\Program Files (x86)\\Diablo III"), false, false},
                 {QStringLiteral("custom_mod"), QStringLiteral("custom_mod"),
                  QStringLiteral("C:\\Program Files (x86)\\Custom"), true, true}}));
}

void createRetroArchFixture(const QString& root) {
  const QString content = root + QStringLiteral("/roms/Sonic & Tails.bin");
  const QString unassigned = root + QStringLiteral("/roms/Unassigned.nes");
  writeFile(content, "rom");
  writeFile(unassigned, "rom");
  writeFile(root + QStringLiteral("/retroarch.cfg"),
            QStringLiteral("playlist_directory = \"%1/playlists\"\n"
                           "thumbnails_directory = \"%1/thumbnails\"\n")
                .arg(root)
                .toUtf8());
  writeFile(
      root + QStringLiteral("/playlists/Sega - Mega Drive.lpl"),
      QStringLiteral(
          R"({"version":"1.5","default_core_path":"%1/cores/genesis_plus_gx_libretro.so","default_core_name":"Genesis Plus GX","items":[{"path":"%2","label":"Sonic & Tails","core_path":"DETECT","core_name":"DETECT","crc32":"00000000|crc","db_name":"Sega - Mega Drive.lpl"},{"path":"%2","label":"Duplicate","core_path":"DETECT","core_name":"DETECT"}]})")
          .arg(root, content)
          .toUtf8());
  writeFile(
      root + QStringLiteral("/playlists/Nintendo.lpl"),
      QStringLiteral(
          R"({"version":"1.5","default_core_path":"DETECT","default_core_name":"DETECT","items":[{"path":"%1","label":"Unassigned","core_path":"DETECT","core_name":"DETECT"}]})")
          .arg(unassigned)
          .toUtf8());
  writeFile(root + QStringLiteral("/thumbnails/Sega - Mega Drive/Named_Boxarts/Sonic _ Tails.png"),
            "cover");
  writeFile(root + QStringLiteral("/thumbnails/Sega - Mega Drive/Named_Snaps/Sonic _ Tails.png"),
            "hero");
  writeFile(
      root + QStringLiteral("/playlists/logs/Genesis Plus GX/Sonic & Tails.lrtl"),
      R"({"version":"1.0","runtime":"12:34:56","last_played":"2026-08-30 19:45:10","play_count":"4","state_slot":"0"})");
  writeFile(root + QStringLiteral("/playlists/content_history.lpl"),
            R"({"version":"1.5","items":[{"path":"/ignored","label":"Ignored"}]})");
}

void createPcsx2Fixture(const QString& root, qint64 playedSeconds = 14) {
  const QString rom = root + QStringLiteral("/roms/Crash Twinsanity (USA).iso");
  writeFile(rom, "iso");
  writeFile(root + QStringLiteral("/inis/PCSX2.ini"), "[GameList]\n");
  const QByteArray pathBytes = rom.toUtf8();
  QByteArray cache;
  const auto appendU32 = [&cache](quint32 value) {
    for (int k = 0; k < 4; ++k) {
      cache.append(static_cast<char>((value >> (8 * k)) & 0xFF));
    }
  };
  const auto appendU64 = [&cache](quint64 value) {
    for (int k = 0; k < 8; ++k) {
      cache.append(static_cast<char>((value >> (8 * k)) & 0xFF));
    }
  };
  cache.append("GLCE");
  appendU32(34);  // current PCSX2 writes version 34
  appendU32(static_cast<quint32>(pathBytes.size()));
  cache.append(pathBytes);
  const QByteArray serialBytes = QByteArray("SLUS-20909");
  appendU32(static_cast<quint32>(serialBytes.size()));
  cache.append(serialBytes);
  const QByteArray titleBytes = QByteArray("Crash Twinsanity");
  appendU32(static_cast<quint32>(titleBytes.size()));
  cache.append(titleBytes);
  const QByteArray empty;
  appendU32(0);  // title_sort (empty)
  cache.append(empty);
  appendU32(0);  // title_en (empty)
  cache.append(empty);
  cache.append('\0');
  cache.append('\x06');
  appendU64(123456789ULL);
  appendU64(1700000000ULL);
  appendU32(0x1A2B3C4DU);
  cache.append('\0');
  writeFile(root + QStringLiteral("/cache/gamelist.cache"), cache);

  const QString playedLine = QStringLiteral("%1 %2 %3\n")
                                 .arg(QStringLiteral("SLUS-20909"), -32)
                                 .arg(playedSeconds, 20)
                                 .arg(1700000500, 20);
  writeFile(root + QStringLiteral("/inis/playtime.dat"), playedLine.toUtf8());
}

void createRyujinxFixture(const QString& root, const QString& romDirectory) {
  writeFile(root + QStringLiteral("/Config.json"),
            QStringLiteral("{\"version\":70,\"game_dirs\":[\"%1\"]}")
                .arg(romDirectory)
                .toUtf8());
  writeFile(romDirectory + QStringLiteral("/Zelda [0100abcd12345678][v0].nsp"), "rom");
  // Real layout: gui is a directory containing metadata.json.
  writeFile(root + QStringLiteral("/games/0100ABCD12345678/gui/metadata.json"),
            "{\"title\":\"Custom Title\",\"timespan_played\":\"01:00:00\","
            "\"last_played_utc\":\"2026-08-30T19:45:10Z\"}");
}

} // namespace

class CoreTests final : public QObject {
  Q_OBJECT

private slots:
  void mockLibraryIsDeterministic();
  void libraryFiltersByModeAndSearch();
  void randomPickRespectsFiltersAndLinkedIdentity();
  void savedFiltersPersistAndPreserveQueries();
  void completionWorkflowPersistsAtLibraryScale();
  void bulkOrganizationIsAtomicAndPreservesSelection();
  void backupArchiveRoundTripsAndRejectsInvalidContent();
  void backupSnapshotConsolidatesLegacyPersonalState();
  void backupDatabaseMergeReplaceAndRollback();
  void backupSettingsApplyAtomicallyAndKeepAccounts();
  void themeLoadsSemanticColors();
  void themeFallsBackWithoutOmarchy();
  void themeReloadsWhenActiveFileChanges();
  void themeFollowsShellLauncherTransparency();
  void themeResolvesCompactTerminalPalette();
  void themeFallsBackAccentToTerminalBlue();
  void themeResolvesLegacySemanticNames();
  void themeFindsLegacyOmarchyLocation();
  void themeFollowsAtomicDirectoryReplacement();
  void themeUsesMachineLauncherOverride();
  void valveKeyValuesParsesNestedAndEscapedValues();
  void valveKeyValuesRejectsMalformedInput();
  void valveKeyValuesRejectsExcessiveNesting();
  void steamScannerImportsLibrariesAndCustomArtwork();
  void steamScannerImportsNonSteamShortcuts();
  void steamScannerRejectsLandscapeCoverFallbackAndImportsAchievements();
  void steamScannerSurvivesMissingLibrariesAndBrokenManifests();
  void steamModelPersistsFavoritesAndHiddenState();
  void steamModelSkipsUnchangedRescans();
  void steamModelMigratesVersionOneDatabase();
  void steamModelMigratesUnscopedOwnedGamesCache();
  void achievementModelLoadsLocalSteamCache();
  void steamAchievementApiParsesPlayerSchemaAndRarity();
  void steamAchievementApiClassifiesFailures();
  void steamOwnedGamesApiParsesLibraryAndPrivacy();
  void steamOwnedGamesRemainOptionalAndFilterByInstallation();
  void steamOwnedLibraryHandlesTwoThousandGames();
  void steamLauncherBuildsSafeUrls();
  void lutrisScannerImportsOnlyLaunchableGames();
  void lutrisModelIsRepeatableAndPreservesLocalState();
  void malformedLutrisDataDoesNotReplaceCachedGames();
  void unifiedLibraryFiltersSourcesAndRoutesFavorites();
  void unifiedLibraryCanDisableSourcesAtRuntime();
  void customCoverPersistsAndResets();
  void artworkSlotsMigratePersistAndResetIndependently();
  void explicitLinksPersistAndPreserveInstallations();
  void launchActivityPersistsAndSortsExactly();
  void organizationPersistsAndFilters();
  void lutrisLauncherBuildsSafeCommands();
  void heroicScannerImportsEpicGogAndAmazon();
  void gogScannerImportsLooseInstallsAndConfinesLaunchTasks();
  void heroicModelIsRepeatableAndPreservesLocalState();
  void malformedHeroicDataDoesNotReplaceCachedGames();
  void heroicAndGogScanFailuresAreIsolated();
  void coldManagedGogFailureDoesNotImportDirectGames();
  void removingLastDirectGogGameClearsCache();
  void heroicLauncherBuildsSafeCommands();
  void gogLauncherBuildsSafeCommands();
  void faugusScannerImportsLaunchableGamesAndArtwork();
  void faugusModelIsRepeatableAndPreservesLocalState();
  void malformedFaugusDataDoesNotReplaceCachedGames();
  void faugusLauncherBuildsSafeCommands();
  void launcherRefreshesRunAsynchronously();
  void absentLaunchersPersistEmptySourcePaths();
  void retroArchScannerImportsPlaylistsArtworkAndRuntime();
  void retroArchScannerResolvesFlatpakPathsAndCoreNames();
  void retroArchModelIsRepeatableAndPreservesLocalState();
  void malformedRetroArchDataDoesNotReplaceCachedGames();
  void retroArchLauncherBuildsSafeCommands();
  void pcsx2ScannerImportsCacheGamesAndPlaytime();
  void pcsx2ModelIsRepeatableAndPreservesLocalState();
  void malformedPcsx2DataDoesNotReplaceCachedGames();
  void pcsx2UnifiedFilterShowsGames();
  void pcsx2LauncherBuildsSafeCommands();
  void ryujinxScannerImportsRomsMetadataAndPlaytime();
  void ryujinxScannerSkipsConfiguredAddOnsAndUpdates();
  void ryujinxModelIsRepeatableAndPreservesLocalState();
  void malformedRyujinxDataDoesNotReplaceCachedGames();
  void ryujinxLauncherBuildsSafeCommands();
  void battleNetScannerImportsInstalledGamesAndArtwork();
  void battleNetScannerDiscoversKnownPrefixes();
  void battleNetScannerKeepsInstallsFromSeparatePrefixes();
  void battleNetModelIsRepeatableAndPreservesLocalState();
  void battleNetModelMigratesLegacyRowsSafely();
  void malformedBattleNetDataDoesNotReplaceCachedGames();
  void oversizedBattleNetDatabaseDoesNotReplaceCachedGames();
  void battleNetLauncherBuildsSafeCommands();
  void launcherReportsInvalidAndStaleTargets();
  void igdbApiBuildsSafeQueriesAndParsesInsights();
  void igdbInsightsLoadFromOfflineCache();
  void retroAchievementsHasherAppliesHeaderStripRules();
  void retroAchievementsHasherReadsZipArchivedRoms();
  void retroAchievementsApiBuildsUrlsAndParsesResponses();
  void retroArchModelReadsCachedRetroAchievementsSummary();
  void retroAchievementsServiceClearsCacheOnAccountSwitch();
  void retroAchievementsServiceBlocksAccountSwitchWhileBusy();
  void stressLibraryContainsOneThousandGames();
  void settingsPersistReducedMotionAndCacheLimit();
  void gogFoldersPersistAndHandleDisconnectedRoots();
  void manualGamesImportEditLaunchAndRemove();
  void launchKeysRoundTripAndResolveInstallations();
  void singleInstanceForwardsPlayAndQuitCommands();
  void sunshineIntegrationWritesOnlyItsOwnEntries();
  void secondInstanceRequestsActivation();
  void couchCursorFollowsInputMode();
  void virtualControllerConnectsAndMapsPrimaryButton();
  void thousandGameSearchStaysResponsive();
};

void CoreTests::mockLibraryIsDeterministic() {
  MockGameModel games;

  QCOMPARE(games.rowCount(), 100);
  QCOMPARE(games.get(0).value(QStringLiteral("title")).toString(), QStringLiteral("Aster Vale"));
  QCOMPARE(games.get(99).value(QStringLiteral("title")).toString(), QStringLiteral("Wild Orbit 4"));
  QVERIFY(games.get(0).value(QStringLiteral("favorite")).toBool());
}

void CoreTests::libraryFiltersByModeAndSearch() {
  MockGameModel games;
  LibraryFilterModel library;
  library.setSourceModel(&games);

  QCOMPARE(library.rowCount(), 100);

  library.setMode(LibraryFilterModel::Mode::Favorites);
  QCOMPARE(library.rowCount(), 13);

  library.setMode(LibraryFilterModel::Mode::Recent);
  QCOMPARE(library.rowCount(), 13);

  library.setMode(LibraryFilterModel::Mode::All);
  library.setSearchText(QStringLiteral("Aster Vale"));
  QCOMPARE(library.rowCount(), 4);
  QCOMPARE(library.get(0).value(QStringLiteral("title")).toString(), QStringLiteral("Aster Vale"));
}

void CoreTests::themeLoadsSemanticColors() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());

  const QString stateHome = directory.path() + QStringLiteral("/state");
  const QString configHome = directory.path() + QStringLiteral("/config");
  const QString current = stateHome + QStringLiteral("/omarchy/current");
  writeFile(current + QStringLiteral("/theme/colors.toml"), sampleTheme());
  writeFile(current + QStringLiteral("/theme.name"), "tokyo-night\n");

  OmarchyTheme theme(stateHome, configHome);

  QVERIFY(theme.omarchyAvailable());
  QCOMPARE(theme.themeName(), QStringLiteral("Tokyo Night"));
  QCOMPARE(theme.accent(), QColor(QStringLiteral("#7aa2f7")));
  QCOMPARE(theme.darkerBackground(), QColor(QStringLiteral("#0e0e14")));
  QVERIFY(theme.mutedText().isValid());
}

void CoreTests::themeFallsBackWithoutOmarchy() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());

  OmarchyTheme theme(directory.path() + QStringLiteral("/state"),
                     directory.path() + QStringLiteral("/config"));

  QVERIFY(!theme.omarchyAvailable());
  QCOMPARE(theme.themeName(), QStringLiteral("Omakade Dark"));
  QVERIFY(theme.background().isValid());
  QVERIFY(theme.foreground().isValid());
}

void CoreTests::themeReloadsWhenActiveFileChanges() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());

  const QString stateHome = directory.path() + QStringLiteral("/state");
  const QString configHome = directory.path() + QStringLiteral("/config");
  const QString colors = stateHome + QStringLiteral("/omarchy/current/theme/colors.toml");
  writeFile(colors, sampleTheme());

  OmarchyTheme theme(stateHome, configHome);
  QSignalSpy changes(&theme, &OmarchyTheme::themeChanged);
  writeFile(colors, sampleTheme("#ff0000"));

  QTRY_COMPARE_WITH_TIMEOUT(theme.accent(), QColor(QStringLiteral("#ff0000")), 1500);
  QVERIFY(!changes.isEmpty());
}

void CoreTests::themeFollowsShellLauncherTransparency() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString stateHome = directory.path() + QStringLiteral("/state");
  const QString configHome = directory.path() + QStringLiteral("/config");
  const QString themeRoot = stateHome + QStringLiteral("/omarchy/current/theme");
  writeFile(themeRoot + QStringLiteral("/colors.toml"), sampleTheme());
  const QString shell = themeRoot + QStringLiteral("/shell.toml");
  writeFile(shell, "[bar]\nbackground-alpha = 1.0\n[launcher]\nbackground-alpha = 0.63\n");

  OmarchyTheme theme(stateHome, configHome);
  QCOMPARE(theme.surfaceAlpha(), 0.63);
  writeFile(shell, "[launcher]\nbackground-alpha = 0.91\n");
  QTRY_COMPARE_WITH_TIMEOUT(theme.surfaceAlpha(), 0.91, 1500);
  writeFile(shell, "[launcher]\nbackground-alpha = 1.0\n");
  QTRY_COMPARE_WITH_TIMEOUT(theme.surfaceAlpha(), 1.0, 1500);
}

void CoreTests::themeResolvesCompactTerminalPalette() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString stateHome = directory.path() + QStringLiteral("/state");
  const QString configHome = directory.path() + QStringLiteral("/config");
  writeFile(stateHome + QStringLiteral("/omarchy/current/theme/colors.toml"),
            "accent = \"#589df6\"\nselection = \"#b5d5ff\"\n"
            "background = \"#1d2837\"\nforeground = \"#ffffff\"\n"
            "color0 = \"#000000\"\ncolor1 = \"#f9555f\"\n"
            "color2 = \"#21b089\"\ncolor3 = \"#fef02a\"\n"
            "color4 = \"#589df6\"\ncolor5 = \"#944d95\"\n"
            "color6 = \"#1f9ee7\"\ncolor7 = \"#bbbbbb\"\n"
            "color8 = \"#555555\"\ncolor9 = \"#fa8c8f\"\n"
            "color10 = \"#35bb9a\"\ncolor11 = \"#ffff55\"\n"
            "color12 = \"#589df6\"\ncolor13 = \"#e75699\"\n"
            "color14 = \"#3979bc\"\ncolor15 = \"#ffffff\"\n");

  OmarchyTheme theme(stateHome, configHome);
  QVERIFY(theme.omarchyAvailable());
  QCOMPARE(theme.mode(), QStringLiteral("dark"));
  QCOMPARE(theme.accent(), QColor(QStringLiteral("#589df6")));
  QCOMPARE(theme.selection(), QColor(QStringLiteral("#b5d5ff")));
  QCOMPARE(theme.background(), QColor(QStringLiteral("#1d2837")));
  QCOMPARE(theme.darkBackground(), QColor(QStringLiteral("#161e29")));
  QCOMPARE(theme.darkerBackground(), QColor(QStringLiteral("#0f141c")));
  QCOMPARE(theme.lighterBackground(), QColor(QStringLiteral("#1d2837")));
  QCOMPARE(theme.darkForeground(), QColor(QStringLiteral("#555555")));
  QCOMPARE(theme.lightForeground(), QColor(QStringLiteral("#ffffff")));
  QCOMPARE(theme.brightForeground(), QColor(QStringLiteral("#ffffff")));
  QCOMPARE(theme.muted(), QColor(QStringLiteral("#555555")));
  QCOMPARE(theme.red(), QColor(QStringLiteral("#f9555f")));
  QCOMPARE(theme.green(), QColor(QStringLiteral("#21b089")));
  QCOMPARE(theme.yellow(), QColor(QStringLiteral("#fef02a")));
  QCOMPARE(theme.blue(), QColor(QStringLiteral("#589df6")));
}

void CoreTests::themeFallsBackAccentToTerminalBlue() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString stateHome = directory.path() + QStringLiteral("/state");
  const QString configHome = directory.path() + QStringLiteral("/config");
  writeFile(stateHome + QStringLiteral("/omarchy/current/theme/colors.toml"),
            "background = \"#16181d\"\nforeground = \"#c5c5d2\"\ncolor4 = \"#6c9ef8\"\n");

  OmarchyTheme theme(stateHome, configHome);
  QVERIFY(theme.omarchyAvailable());
  QCOMPARE(theme.accent(), QColor(QStringLiteral("#6c9ef8")));
}

void CoreTests::themeResolvesLegacySemanticNames() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString stateHome = directory.path() + QStringLiteral("/state");
  const QString configHome = directory.path() + QStringLiteral("/config");
  writeFile(stateHome + QStringLiteral("/omarchy/current/theme/colors.toml"),
            "mode = \"dark\"\naccent = \"#fcef0c\"\n"
            "bg = \"#1b1d1e\"\ndark_bg = \"#353738\"\n"
            "darker_bg = \"#1a1b1c\"\nlighter_bg = \"#1b1d1e\"\n"
            "fg = \"#a7a8a3\"\ndark_fg = \"#8a8c89\"\n"
            "light_fg = \"#c5c5be\"\nbright_fg = \"#dadad5\"\n");

  OmarchyTheme theme(stateHome, configHome);
  QVERIFY(theme.omarchyAvailable());
  QCOMPARE(theme.background(), QColor(QStringLiteral("#1b1d1e")));
  QCOMPARE(theme.darkBackground(), QColor(QStringLiteral("#353738")));
  QCOMPARE(theme.darkerBackground(), QColor(QStringLiteral("#1a1b1c")));
  QCOMPARE(theme.lighterBackground(), QColor(QStringLiteral("#1b1d1e")));
  QCOMPARE(theme.foreground(), QColor(QStringLiteral("#a7a8a3")));
  QCOMPARE(theme.darkForeground(), QColor(QStringLiteral("#8a8c89")));
  QCOMPARE(theme.lightForeground(), QColor(QStringLiteral("#c5c5be")));
  QCOMPARE(theme.brightForeground(), QColor(QStringLiteral("#dadad5")));
}

void CoreTests::themeFindsLegacyOmarchyLocation() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString stateHome = directory.path() + QStringLiteral("/state");
  const QString configHome = directory.path() + QStringLiteral("/config");
  const QString current = configHome + QStringLiteral("/omarchy/current");
  writeFile(current + QStringLiteral("/theme/colors.toml"),
            "background = \"#202040\"\nforeground = \"#eeeeff\"\naccent = \"#6060ff\"\n");
  writeFile(current + QStringLiteral("/theme.name"), "legacy-blue\n");

  OmarchyTheme theme(stateHome, configHome);
  QVERIFY(theme.omarchyAvailable());
  QCOMPARE(theme.themeName(), QStringLiteral("Legacy Blue"));
  QCOMPARE(theme.background(), QColor(QStringLiteral("#202040")));
  QCOMPARE(theme.accent(), QColor(QStringLiteral("#6060ff")));
}

void CoreTests::themeFollowsAtomicDirectoryReplacement() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString stateHome = directory.path() + QStringLiteral("/state");
  const QString configHome = directory.path() + QStringLiteral("/config");
  const QString root = stateHome + QStringLiteral("/omarchy/current");
  writeFile(root + QStringLiteral("/theme/colors.toml"),
            "background = \"#101020\"\nforeground = \"#eeeeff\"\n");

  OmarchyTheme theme(stateHome, configHome);
  QCOMPARE(theme.background(), QColor(QStringLiteral("#101020")));

  writeFile(root + QStringLiteral("/next-theme/colors.toml"),
            "background = \"#302010\"\nforeground = \"#fff0ee\"\n");
  QVERIFY(QDir(root + QStringLiteral("/theme")).removeRecursively());
  QVERIFY(QDir(root).rename(QStringLiteral("next-theme"), QStringLiteral("theme")));
  QTRY_COMPARE_WITH_TIMEOUT(theme.background(), QColor(QStringLiteral("#302010")), 1500);
}

void CoreTests::themeUsesMachineLauncherOverride() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString stateHome = directory.path() + QStringLiteral("/state");
  const QString configHome = directory.path() + QStringLiteral("/config");
  const QString themeRoot = stateHome + QStringLiteral("/omarchy/current/theme");
  writeFile(themeRoot + QStringLiteral("/colors.toml"), sampleTheme());
  writeFile(themeRoot + QStringLiteral("/shell.toml"), "[launcher]\nbackground-alpha = 0.73\n");
  const QString userShell = configHome + QStringLiteral("/omarchy/shell.toml");
  writeFile(userShell, "[launcher]\nbackground-alpha = 0.91\n");

  OmarchyTheme theme(stateHome, configHome);
  QCOMPARE(theme.surfaceAlpha(), 0.91);

  writeFile(userShell, "[launcher]\nbackground-alpha = 0.64\n");
  QTRY_COMPARE_WITH_TIMEOUT(theme.surfaceAlpha(), 0.64, 1500);

  QVERIFY(QFile::remove(userShell));
  QTRY_COMPARE_WITH_TIMEOUT(theme.surfaceAlpha(), 0.73, 1500);
}

void CoreTests::valveKeyValuesParsesNestedAndEscapedValues() {
  ValveKeyValues values;
  QString error;
  QVERIFY(ValveKeyValuesParser::parse(
      "// comment\n\"Root\" { \"name\" \"A \\\"quoted\\\" game\" \"path\" "
      "\"/games/library\" \"label\" \"\" }",
      &values, &error));
  QVERIFY2(error.isEmpty(), qPrintable(error));
  const ValveKeyValues* root = values.object(QStringLiteral("root"));
  QVERIFY(root != nullptr);
  QCOMPARE(root->value(QStringLiteral("NAME")), QStringLiteral("A \"quoted\" game"));
  QCOMPARE(root->value(QStringLiteral("path")), QStringLiteral("/games/library"));
  QCOMPARE(root->value(QStringLiteral("label")), QString());
}

void CoreTests::valveKeyValuesRejectsMalformedInput() {
  ValveKeyValues values;
  QString error;
  QVERIFY(!ValveKeyValuesParser::parse("\"Root\" { \"name\" \"unfinished\"", &values, &error));
  QVERIFY(!error.isEmpty());

  const QByteArray trailing = sampleShortcutsVdf("/icon.png") + QByteArray(1, 0x01);
  QVERIFY(!ValveKeyValuesParser::parseBinary(trailing, &values, &error));
  QCOMPARE(error, QStringLiteral("Unexpected trailing data"));
}

void CoreTests::valveKeyValuesRejectsExcessiveNesting() {
  QByteArray input;
  for (int depth = 0; depth < 129; ++depth) {
    input.append("\"key\" {");
  }
  input.append("\"value\" \"leaf\"");
  for (int depth = 0; depth < 129; ++depth) {
    input.append('}');
  }
  ValveKeyValues values;
  QString error;
  QVERIFY(!ValveKeyValuesParser::parse(input, &values, &error));
  QCOMPARE(error, QStringLiteral("Object nesting is too deep"));

  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  QFile oversized(directory.path() + QStringLiteral("/oversized.vdf"));
  QVERIFY(oversized.open(QIODevice::WriteOnly));
  QVERIFY(oversized.resize(64LL * 1024 * 1024 + 1));
  oversized.close();
  QVERIFY(!ValveKeyValuesParser::parseFile(oversized.fileName(), &values, &error));
  QCOMPARE(error, QStringLiteral("File is too large"));
}

void CoreTests::steamScannerImportsLibrariesAndCustomArtwork() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/Steam");
  const QString second = directory.path() + QStringLiteral("/Second Library");
  createSteamFixture(root, second);

  const SteamScanResult result = SteamScanner::scan({root});
  QCOMPARE(result.games.size(), 2);
  QCOMPARE(result.games.at(0).appId, QStringLiteral("10"));
  QCOMPARE(result.games.at(0).playtimeMinutes, 125);
  QVERIFY(result.games.at(0).coverPath.endsWith(QStringLiteral("10p.png")));
  QCOMPARE(result.games.at(1).appId, QStringLiteral("20"));
  QVERIFY(result.warnings.isEmpty());
}

void CoreTests::steamScannerImportsNonSteamShortcuts() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/Steam");
  const QString second = directory.path() + QStringLiteral("/Second Library");
  createSteamFixture(root, second);

  const QString icon = directory.path() + QStringLiteral("/soulframe-icon.png");
  writeFile(icon, "icon");
  writeFile(root + QStringLiteral("/userdata/42/config/shortcuts.vdf"),
            sampleShortcutsVdf(icon.toUtf8()));
  writeFile(root + QStringLiteral("/userdata/42/config/grid/3236138358p.png"), "shortcut cover");
  writeFile(root + QStringLiteral("/userdata/42/config/localconfig.vdf"),
            "\"UserLocalConfigStore\" { \"Software\" { \"Valve\" { \"Steam\" { \"apps\" { "
            "\"10\" { \"LastPlayed\" \"1700000000\" \"Playtime\" \"125\" } "
            "\"3236138358\" { \"LastPlayed\" \"1690000000\" \"Playtime\" \"954\" } } } } } }\n");

  ValveKeyValues parsed;
  QVERIFY(ValveKeyValuesParser::parseBinaryFile(
      root + QStringLiteral("/userdata/42/config/shortcuts.vdf"), &parsed));
  QCOMPARE(parsed.object(QStringLiteral("shortcuts"))->object(QStringLiteral("0"))
               ->value(QStringLiteral("AppName")),
           QStringLiteral("Soulframe"));

  const SteamScanResult result = SteamScanner::scan({root});
  QCOMPARE(result.games.size(), 3);
  const auto shortcut = std::find_if(result.games.cbegin(), result.games.cend(), [](const auto& game) {
    return game.appId == QStringLiteral("3236138358");
  });
  QVERIFY(shortcut != result.games.cend());
  QCOMPARE(shortcut->title, QStringLiteral("Soulframe"));
  QCOMPARE(shortcut->installDirectory, QStringLiteral("/games/Soulframe"));
  QCOMPARE(shortcut->playtimeMinutes, 954);
  QCOMPARE(shortcut->lastPlayed, 1700000001);
  QVERIFY(shortcut->coverPath.endsWith(QStringLiteral("3236138358p.png")));
  QVERIFY(result.warnings.isEmpty());
  QVERIFY(result.unreadableManifests.isEmpty());

  writeFile(root + QStringLiteral("/userdata/42/config/shortcuts.vdf"), "not a binary vdf");
  const SteamScanResult unreadable = SteamScanner::scan({root});
  QCOMPARE(unreadable.games.size(), 2);
  QCOMPARE(unreadable.unreadableManifests.size(), 1);
  QVERIFY(unreadable.unreadableManifests.constFirst().endsWith(QStringLiteral("shortcuts.vdf")));
  QVERIFY(unreadable.warnings.join(QLatin1Char('\n')).contains(QStringLiteral("shortcuts.vdf")));
}

void CoreTests::steamScannerRejectsLandscapeCoverFallbackAndImportsAchievements() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/Steam");
  const QString second = directory.path() + QStringLiteral("/Second Library");
  createSteamFixture(root, second);

  const SteamScanResult result = SteamScanner::scan({root});
  QCOMPARE(result.games.at(0).achievementsUnlocked, 1);
  QCOMPARE(result.games.at(0).achievementsTotal, 2);
  QCOMPARE(result.games.at(0).achievements.size(), 2);
  QCOMPARE(result.games.at(0).achievements.at(0).title, QStringLiteral("First Win"));
  QVERIFY(result.games.at(1).coverPath.endsWith(QStringLiteral("library_capsule.jpg")));
  QVERIFY(result.games.at(1).heroPath.endsWith(QStringLiteral("library_hero.jpg")));
}

void CoreTests::steamScannerSurvivesMissingLibrariesAndBrokenManifests() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/Steam");
  const QString second = directory.path() + QStringLiteral("/Second Library");
  const QString missing = directory.path() + QStringLiteral("/Unmounted Drive/SteamLibrary");
  createSteamFixture(root, second);
  writeFile(root + QStringLiteral("/config/libraryfolders.vdf"),
            QStringLiteral("\"libraryfolders\"\n{\n\"0\" { \"path\" \"%1\" }\n\"1\" { \"path\" "
                           "\"%2\" }\n\"2\" { \"path\" \"%3\" }\n}\n")
                .arg(root, second, missing)
                .toUtf8());
  writeFile(root + QStringLiteral("/steamapps/appmanifest_30.acf"), "\"AppState\" {");
  writeFile(root + QStringLiteral("/steamapps/appmanifest_40.acf"), "\"Other\" { }");

  // A stale library path or a manifest Steam is still writing must not empty the library.
  const SteamScanResult result = SteamScanner::scan({root});
  QVERIFY(!result.incomplete);
  QCOMPARE(result.games.size(), 2);
  QCOMPARE(result.unreadableManifests.size(), 2);
  QCOMPARE(result.warnings.size(), 3);
  QVERIFY(result.warnings.join(QLatin1Char('\n')).contains(QStringLiteral("Unmounted Drive")));
  QVERIFY(SteamScanner::isToolTitle(QStringLiteral("Proton 9.0 (Beta)")));
  QVERIFY(SteamScanner::isToolTitle(QStringLiteral("Steam Linux Runtime 3.0 (sniper)")));
  QVERIFY(!SteamScanner::isToolTitle(QStringLiteral("Protonic Blast")));

  const QString database = directory.path() + QStringLiteral("/omakade.sqlite3");
  SteamGameModel model(database);
  model.refreshFromRoots({root});
  QTRY_VERIFY_WITH_TIMEOUT(!model.scanning(), 3000);
  QCOMPARE(model.rowCount(), 2);
  QVERIFY(model.statusText().startsWith(QStringLiteral("Imported 2")));
  QVERIFY(model.errorText().contains(QStringLiteral("appmanifest_30.acf")));

  LibraryFilterModel library;
  library.setSourceModel(&model);
  QCOMPARE(library.indexOf(QStringLiteral("Steam"), QString{}, QStringLiteral("20")), 1);
  QCOMPARE(library.indexOf(QStringLiteral("Steam"), QString{}, QStringLiteral("999")), -1);
  QCOMPARE(library.indexOf(QStringLiteral("Lutris"), QString{}, QStringLiteral("20")), -1);
}

void CoreTests::steamModelPersistsFavoritesAndHiddenState() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/Steam");
  const QString second = directory.path() + QStringLiteral("/Library");
  const QString database = directory.path() + QStringLiteral("/omakade.sqlite3");
  createSteamFixture(root, second);

  {
    SteamGameModel model(database);
    model.refreshFromRoots({root});
    QTRY_VERIFY_WITH_TIMEOUT(!model.scanning(), 3000);
    QCOMPARE(model.rowCount(), 2);
    model.toggleFavorite(0);
    model.toggleHidden(1);
    LibraryFilterModel filtered;
    filtered.setSourceModel(&model);
    QCOMPARE(filtered.rowCount(), 1);
    filtered.setMode(LibraryFilterModel::Mode::Hidden);
    QCOMPARE(filtered.rowCount(), 1);
    writeFile(second + QStringLiteral("/steamapps/appmanifest_20.acf"), "\"AppState\" {");
    model.refreshFromRoots({root});
    QTRY_VERIFY_WITH_TIMEOUT(!model.scanning(), 3000);
    // The unreadable manifest keeps its cached installation instead of freezing the scan.
    QCOMPARE(model.rowCount(), 2);
    QVERIFY(model.statusText().startsWith(QStringLiteral("Imported 1")));
    QVERIFY(model.errorText().contains(QStringLiteral("appmanifest_20.acf")));
  }

  SteamGameModel reloaded(database);
  QCOMPARE(reloaded.rowCount(), 2);
  QVERIFY(reloaded.get(0).value(QStringLiteral("favorite")).toBool());
  QVERIFY(reloaded.get(1).value(QStringLiteral("hidden")).toBool());
}

void CoreTests::steamModelSkipsUnchangedRescans() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/Steam");
  const QString second = directory.path() + QStringLiteral("/Library");
  createSteamFixture(root, second);

  SteamGameModel model(directory.path() + QStringLiteral("/omakade.sqlite3"));
  QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
  model.refreshFromRoots({root});
  QTRY_VERIFY_WITH_TIMEOUT(!model.scanning(), 3000);
  QCOMPARE(model.rowCount(), 2);
  QCOMPARE(resets.count(), 1);

  // Steam touches manifests constantly while downloading. A rescan that finds the same
  // library must not rebuild the model or the database.
  model.refreshFromRoots({root});
  QTRY_VERIFY_WITH_TIMEOUT(!model.scanning(), 3000);
  QCOMPARE(model.rowCount(), 2);
  QCOMPARE(resets.count(), 1);
  QVERIFY(model.statusText().startsWith(QStringLiteral("Imported 2")));

  writeFile(second + QStringLiteral("/steamapps/appmanifest_30.acf"),
            manifest("30", "Day of Defeat", "Day of Defeat"));
  model.refreshFromRoots({root});
  QTRY_VERIFY_WITH_TIMEOUT(!model.scanning(), 3000);
  QCOMPARE(model.rowCount(), 3);
  QCOMPARE(resets.count(), 2);
}

void CoreTests::steamModelMigratesVersionOneDatabase() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString database = directory.path() + QStringLiteral("/library.sqlite3");
  const QString setupConnection = QStringLiteral("migration-setup");
  {
    QSqlDatabase setup = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), setupConnection);
    setup.setDatabaseName(database);
    QVERIFY(setup.open());
    QSqlQuery query(setup);
    QVERIFY(query.exec(QStringLiteral(
        "CREATE TABLE games (app_id TEXT PRIMARY KEY, title TEXT NOT NULL, favorite INTEGER NOT "
        "NULL DEFAULT 0, hidden INTEGER NOT NULL DEFAULT 0)")));
    QVERIFY(query.exec(QStringLiteral(
        "CREATE TABLE installations (app_id TEXT PRIMARY KEY REFERENCES games(app_id), install_dir "
        "TEXT NOT NULL, library_path TEXT NOT NULL, manifest_path TEXT NOT NULL, cover_path TEXT, "
        "hero_path TEXT, logo_path TEXT, last_played INTEGER NOT NULL DEFAULT 0, playtime_minutes "
        "INTEGER NOT NULL DEFAULT 0, observed_at INTEGER NOT NULL)")));
    QVERIFY(query.exec(QStringLiteral("CREATE TABLE source_state (source TEXT PRIMARY KEY, "
                                      "last_scan INTEGER, last_error TEXT)")));
    QVERIFY(query.exec(QStringLiteral(
        "INSERT INTO games VALUES('440', 'Team Fortress 2', 1, 1)")));
    QVERIFY(query.exec(QStringLiteral(
        "INSERT INTO installations VALUES('440', 'Team Fortress 2', '/games', '/manifests/440', "
        "'', '', '', 1700000000, 600, 1700000000)")));
    QVERIFY(query.exec(QStringLiteral(
        "INSERT INTO source_state VALUES('steam', 1700000000, '')")));
    QVERIFY(query.exec(QStringLiteral("PRAGMA user_version = 5")));
    setup.close();
  }
  QSqlDatabase::removeDatabase(setupConnection);

  SteamGameModel model(database);
  UnifiedGameModel unified(database);
  SteamGameModel reopened(database);
  QCOMPARE(reopened.rowCount(), 1);
  QCOMPARE(reopened.get(0).value(QStringLiteral("appId")).toString(), QStringLiteral("440"));
  QVERIFY(reopened.get(0).value(QStringLiteral("favorite")).toBool());
  QVERIFY(reopened.get(0).value(QStringLiteral("hidden")).toBool());
  const QString verifyConnection = QStringLiteral("migration-verify");
  {
    QSqlDatabase verify = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), verifyConnection);
    verify.setDatabaseName(database);
    QVERIFY(verify.open());
    QSqlQuery query(verify);
    QVERIFY(query.exec(QStringLiteral("PRAGMA user_version")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 9);
    QVERIFY(query.exec(QStringLiteral(
        "SELECT paths FROM source_state WHERE source = 'steam'")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), QString{});
    QVERIFY(query.exec(
        QStringLiteral("SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name IN "
                       "('achievement_summary', 'achievements')")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 2);
    QVERIFY(query.exec(
        QStringLiteral("SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = "
                       "'game_insights'")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 1);
    QVERIFY(query.exec(
        QStringLiteral("SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = "
                       "'owned_games'")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 1);
    QVERIFY(query.exec(
        QStringLiteral("SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name IN "
                       "('artwork_overrides', 'game_link_members', 'launch_activity')")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 3);
    QVERIFY(query.exec(
        QStringLiteral("SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name IN "
                       "('game_organization', 'collections', 'collection_games')")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 3);
    verify.close();
  }
  QSqlDatabase::removeDatabase(verifyConnection);
}

void CoreTests::steamModelMigratesUnscopedOwnedGamesCache() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString database = directory.path() + QStringLiteral("/library.sqlite3");
  const QString setupConnection = QStringLiteral("owned-migration-setup");
  {
    QSqlDatabase setup = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), setupConnection);
    setup.setDatabaseName(database);
    QVERIFY(setup.open());
    QSqlQuery query(setup);
    QVERIFY(query.exec(QStringLiteral(
        "CREATE TABLE games (app_id TEXT PRIMARY KEY, title TEXT NOT NULL, favorite INTEGER NOT "
        "NULL DEFAULT 0, hidden INTEGER NOT NULL DEFAULT 0)")));
    QVERIFY(query.exec(QStringLiteral("INSERT INTO games VALUES('440', 'Team Fortress 2', 0, 0)")));
    QVERIFY(query.exec(QStringLiteral(
        "CREATE TABLE owned_games (app_id TEXT PRIMARY KEY REFERENCES games(app_id), "
        "playtime_minutes INTEGER NOT NULL DEFAULT 0, synced_at INTEGER NOT NULL)")));
    QVERIFY(query.exec(QStringLiteral("INSERT INTO owned_games VALUES('440', 600, 1700000000)")));
    QVERIFY(query.exec(QStringLiteral("PRAGMA user_version = 8")));
    setup.close();
  }
  QSqlDatabase::removeDatabase(setupConnection);

  AppSettings settings(directory.path() + QStringLiteral("/config.toml"));
  settings.setSteamId(QStringLiteral("76561198000000000"));
  SteamGameModel model(database, &settings);
  QCOMPARE(model.rowCount(), 0);

  const QString verifyConnection = QStringLiteral("owned-migration-verify");
  {
    QSqlDatabase verify = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), verifyConnection);
    verify.setDatabaseName(database);
    QVERIFY(verify.open());
    QSqlQuery query(verify);
    QVERIFY(query.exec(QStringLiteral("PRAGMA user_version")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 9);
    QVERIFY(query.exec(QStringLiteral("PRAGMA table_info(owned_games)")));
    bool hasSteamId = false;
    while (query.next()) {
      hasSteamId = hasSteamId || query.value(1).toString() == QStringLiteral("steam_id");
    }
    QVERIFY(hasSteamId);
    QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM owned_games")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 0);
    verify.close();
  }
  QSqlDatabase::removeDatabase(verifyConnection);
}

void CoreTests::achievementModelLoadsLocalSteamCache() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/Steam");
  const QString second = directory.path() + QStringLiteral("/Library");
  const QString database = directory.path() + QStringLiteral("/omakade.sqlite3");
  createSteamFixture(root, second);

  AppSettings settings(directory.path() + QStringLiteral("/config.toml"));
  SteamGameModel games(database, &settings);
  games.refreshFromRoots({root});
  QTRY_VERIFY_WITH_TIMEOUT(!games.scanning(), 3000);
  QCOMPARE(games.get(0).value(QStringLiteral("achievementsUnlocked")).toInt(), 1);
  QCOMPARE(games.get(0).value(QStringLiteral("achievementsTotal")).toInt(), 2);

  AchievementModel achievements(database, &settings);
  achievements.load(QStringLiteral("10"));
  QCOMPARE(achievements.unlocked(), 1);
  QCOMPARE(achievements.total(), 2);
  QCOMPARE(achievements.rowCount(), 2);
  QCOMPARE(achievements.data(achievements.index(0), AchievementModel::TitleRole).toString(),
           QStringLiteral("First Win"));

  const QString updateConnection = QStringLiteral("achievement-sort-update");
  {
    QSqlDatabase update = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), updateConnection);
    update.setDatabaseName(database);
    QVERIFY(update.open());
    QSqlQuery query(update);
    QVERIFY(query.exec(QStringLiteral(
        "UPDATE achievements SET unlocked = 1, unlock_time = 1600000000 "
        "WHERE app_id = '10' AND api_name = 'WIN_TWO'")));
    update.close();
  }
  QSqlDatabase::removeDatabase(updateConnection);

  achievements.load(QStringLiteral("10"));
  QCOMPARE(achievements.data(achievements.index(0), AchievementModel::TitleRole).toString(),
           QStringLiteral("Second Win"));
  achievements.setSortMode(1);
  QCOMPARE(achievements.data(achievements.index(0), AchievementModel::TitleRole).toString(),
           QStringLiteral("First Win"));
  {
    QSqlDatabase update = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), updateConnection);
    update.setDatabaseName(database);
    QVERIFY(update.open());
    QSqlQuery query(update);
    QVERIFY(query.exec(QStringLiteral(
        "UPDATE achievement_summary SET unlocked = 0, total = 0, source = "
        "'retroachievements' WHERE app_id = '10'")));
    QVERIFY(query.exec(QStringLiteral("DELETE FROM achievements WHERE app_id = '10'")));
    update.close();
  }
  QSqlDatabase::removeDatabase(updateConnection);
  achievements.load(QStringLiteral("10"));
  QCOMPARE(achievements.total(), 0);
  QCOMPARE(achievements.statusText(), QStringLiteral("This game has no RetroAchievements."));

  QVERIFY(AchievementModel::acceptsIconUrl(QUrl(QStringLiteral(
      "https://steamcdn-a.akamaihd.net/steamcommunity/public/images/apps/10/icon.jpg"))));
  QVERIFY(AchievementModel::acceptsIconUrl(
      QUrl(QStringLiteral("https://shared.steamstatic.com/icon.jpg"))));
  QVERIFY(!AchievementModel::acceptsIconUrl(
      QUrl(QStringLiteral("https://steamcdn-a.akamaihd.net.example.com/icon.jpg"))));
  QVERIFY(!AchievementModel::acceptsIconUrl(
      QUrl(QStringLiteral("http://steamcdn-a.akamaihd.net/icon.jpg"))));
  QVERIFY(AchievementModel::acceptsIconUrl(
      QUrl(QStringLiteral("https://media.retroachievements.org/Badge/012345.png"))));
  QVERIFY(!AchievementModel::acceptsIconUrl(
      QUrl(QStringLiteral("https://media.retroachievements.org.example.com/Badge/x.png"))));
}

void CoreTests::steamAchievementApiParsesPlayerSchemaAndRarity() {
  const QByteArray player =
      R"({"playerstats":{"success":true,"achievements":[{"apiname":"FIRST","achieved":1,"unlocktime":1700000000},{"apiname":"HIDDEN","achieved":0,"unlocktime":0}]}})";
  const QByteArray schema =
      R"({"game":{"availableGameStats":{"achievements":[{"name":"FIRST","displayName":"First Step","description":"Begin","icon":"https://shared.steamstatic.com/first.jpg","hidden":0},{"name":"HIDDEN","displayName":"Secret","description":"","icon":"https://shared.steamstatic.com/hidden.jpg","hidden":1}]}}})";
  const QByteArray rarity =
      R"({"achievementpercentages":{"achievements":[{"name":"FIRST","percent":42.5},{"name":"HIDDEN","percent":3.25}]}})";

  SteamAchievementApiResult result;
  QString error;
  QCOMPARE(SteamAchievementApi::parse(player, schema, rarity, &result, &error),
           SteamApiState::Ready);
  QVERIFY(error.isEmpty());
  QCOMPARE(result.unlocked, 1);
  QCOMPARE(result.total, 2);
  QCOMPARE(result.achievements.at(0).title, QStringLiteral("First Step"));
  QCOMPARE(result.achievements.at(0).rarity, 42.5);
  QVERIFY(result.achievements.at(1).hidden);
}

void CoreTests::steamAchievementApiClassifiesFailures() {
  QCOMPARE(SteamAchievementApi::authenticatedHost(), QStringLiteral("api.steampowered.com"));
  QCOMPARE(SteamAchievementApi::classifyHttpResponse(0, true), SteamApiState::Offline);
  QCOMPARE(SteamAchievementApi::classifyHttpResponse(403, false), SteamApiState::InvalidKey);
  QCOMPARE(SteamAchievementApi::classifyHttpResponse(429, false), SteamApiState::RateLimited);
  QCOMPARE(SteamAchievementApi::classifyHttpResponse(429, true), SteamApiState::RateLimited);
  QCOMPARE(SteamAchievementApi::classifyHttpResponse(500, true), SteamApiState::RemoteError);

  SteamAchievementApiResult result;
  QString error;
  const QByteArray privatePlayer =
      R"({"playerstats":{"success":false,"error":"Profile is private"}})";
  QCOMPARE(SteamAchievementApi::parse(privatePlayer, R"({"game":{}})", R"({})", &result, &error),
           SteamApiState::PrivateProfile);
  QVERIFY(!error.isEmpty());
  const QByteArray invalidKey = R"({"playerstats":{"success":false,"error":"Invalid API key"}})";
  QCOMPARE(SteamAchievementApi::parse(invalidKey, R"({"game":{}})", R"({})", &result, &error),
           SteamApiState::InvalidKey);
  QCOMPARE(SteamAchievementApi::parse("not json", R"({"game":{}})", R"({})", &result, &error),
           SteamApiState::RemoteError);
  QVERIFY(SteamAchievementApi::isNoStatsResponse(
      R"({"playerstats":{"error":"Requested app has no stats","success":false}})"));
  QVERIFY(!SteamAchievementApi::isNoStatsResponse(privatePlayer));
  // Steam answers HTTP 403 with this body when game details are private; the key is valid.
  const QByteArray notPublic =
      R"({"playerstats":{"error":"Profile is not public","success":false}})";
  QVERIFY(SteamAchievementApi::isPrivateProfileResponse(notPublic));
  QVERIFY(SteamAchievementApi::isPrivateProfileResponse(privatePlayer));
  QVERIFY(!SteamAchievementApi::isPrivateProfileResponse(invalidKey));
  QVERIFY(!SteamAchievementApi::isPrivateProfileResponse("not json"));
  QCOMPARE(SteamAchievementApi::parse(notPublic, R"({"game":{}})", R"({})", &result, &error),
           SteamApiState::PrivateProfile);
  QVERIFY(!SteamAchievementApi::isNoStatsResponse("not json"));
  QVERIFY(!SteamAchievementApi::isNoStatsResponse(R"({"playerstats":{"success":true}})"));
}

void CoreTests::steamOwnedGamesApiParsesLibraryAndPrivacy() {
  QVector<SteamOwnedGameRecord> games;
  QString error;
  QCOMPARE(
      SteamOwnedGamesApi::parse(
          R"({"response":{"game_count":2,"games":[{"appid":10,"name":"Counter-Strike","playtime_forever":125},{"appid":30,"name":"Portal","playtime_forever":60}]}})",
          &games, &error),
      SteamApiState::Ready);
  QVERIFY(error.isEmpty());
  QCOMPARE(games.size(), 2);
  QCOMPARE(games.at(0).appId, QStringLiteral("10"));
  QCOMPARE(games.at(0).title, QStringLiteral("Counter-Strike"));
  QCOMPARE(games.at(0).playtimeMinutes, 125);

  QCOMPARE(SteamOwnedGamesApi::parse(R"({"response":{}})", &games, &error),
           SteamApiState::PrivateProfile);
  QVERIFY(!error.isEmpty());
  QCOMPARE(SteamOwnedGamesApi::parse(R"({"response":{"game_count":0}})", &games, &error),
           SteamApiState::Ready);
  QVERIFY(games.isEmpty());
  QCOMPARE(SteamOwnedGamesApi::parse(R"({"response":{"game_count":2}})", &games, &error),
           SteamApiState::RemoteError);
  // Steam's game_count is not reliable for large accounts; the array is the truth.
  QCOMPARE(SteamOwnedGamesApi::parse(
               R"({"response":{"game_count":2,"games":[{"appid":10,"name":"Counter-Strike"}]}})",
               &games, &error),
           SteamApiState::Ready);
  QCOMPARE(games.size(), 1);
  // Duplicates, nameless entries, and Steam tools are skipped instead of failing the sync.
  QCOMPARE(
      SteamOwnedGamesApi::parse(
          R"({"response":{"game_count":5,"games":[{"appid":10,"name":"Counter-Strike"},{"appid":10,"name":"Duplicate"},{"appid":11,"name":""},{"appid":1493710,"name":"Proton Experimental"},{"appid":1628350,"name":"Steam Linux Runtime 3.0 sniper"},{"appid":30,"name":"Portal"}]}})",
          &games, &error),
      SteamApiState::Ready);
  QCOMPARE(games.size(), 2);
  QCOMPARE(games.at(1).appId, QStringLiteral("30"));
  QCOMPARE(SteamOwnedGamesApi::parse("not json", &games, &error), SteamApiState::RemoteError);
  QVERIFY(SteamOwnedGamesApi::messageForState(SteamApiState::Offline)
              .contains(QStringLiteral("owned games")));
  QVERIFY(SteamOwnedGamesApi::messageForState(SteamApiState::RemoteError)
              .contains(QStringLiteral("owned games")));
  QVERIFY(!SteamOwnedGamesApi::messageForState(SteamApiState::Offline)
               .contains(QStringLiteral("achievement")));
}

void CoreTests::steamOwnedGamesRemainOptionalAndFilterByInstallation() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString database = directory.path() + QStringLiteral("/library.sqlite3");
  AppSettings settings(directory.path() + QStringLiteral("/config.toml"));
  settings.setSteamId(QStringLiteral("76561198000000000"));
  settings.setSteamId(QStringLiteral("12345678"));
  settings.setSteamId(QStringLiteral("tsouth89"));
  QCOMPARE(settings.steamId(), QStringLiteral("76561198000000000"));
  {
    SteamGameModel schema(database, &settings);
  }

  const QString connection = QStringLiteral("owned-games-fixture");
  {
    QSqlDatabase setup = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    setup.setDatabaseName(database);
    QVERIFY(setup.open());
    QSqlQuery query(setup);
    QVERIFY(query.exec(QStringLiteral(
        "INSERT INTO games(app_id, title) VALUES('10', 'Counter-Strike'), ('30', 'Portal'), "
        "('40', 'Other Account Game')")));
    QVERIFY(query.exec(QStringLiteral(
        "INSERT INTO owned_games(steam_id, app_id, playtime_minutes, synced_at) VALUES"
        "('76561198000000000', '10', 125, 1700000000), "
        "('76561198000000000', '30', 60, 1700000000), "
        "('76561198000000001', '40', 30, 1700000000)")));
    QVERIFY(query.exec(QStringLiteral(
        "INSERT INTO installations(app_id, install_dir, library_path, manifest_path, cover_path, "
        "hero_path, logo_path, last_played, playtime_minutes, observed_at) VALUES"
        "('10', 'Counter-Strike', '/games', '/manifests/10', '', '', '', 0, 100, 1700000000)")));
    setup.close();
  }
  QSqlDatabase::removeDatabase(connection);

  SteamGameModel games(database, &settings);
  QCOMPARE(games.rowCount(), 2);
  LibraryFilterModel library;
  library.setSourceModel(&games);
  QCOMPARE(library.rowCount(), 1);
  QVERIFY(library.get(0).value(QStringLiteral("installed")).toBool());

  library.setAvailability(LibraryFilterModel::Availability::AllGames);
  QCOMPARE(library.rowCount(), 2);
  library.setAvailability(LibraryFilterModel::Availability::ReadyToInstall);
  QCOMPARE(library.rowCount(), 1);
  QCOMPARE(library.get(0).value(QStringLiteral("appId")).toString(), QStringLiteral("30"));
  QVERIFY(!library.get(0).value(QStringLiteral("installed")).toBool());

  settings.setSteamId(QStringLiteral("76561198000000001"));
  library.setAvailability(LibraryFilterModel::Availability::AllGames);
  QCOMPARE(library.rowCount(), 2);
  library.setAvailability(LibraryFilterModel::Availability::ReadyToInstall);
  QCOMPARE(library.rowCount(), 1);
  QCOMPARE(library.get(0).value(QStringLiteral("appId")).toString(), QStringLiteral("40"));
}

void CoreTests::steamOwnedLibraryHandlesTwoThousandGames() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString database = directory.path() + QStringLiteral("/library.sqlite3");
  AppSettings settings(directory.path() + QStringLiteral("/config.toml"));
  const QString steamId = QStringLiteral("76561198000000000");
  settings.setSteamId(steamId);
  {
    SteamGameModel schema(database, &settings);
  }

  const QString connection = QStringLiteral("large-owned-games-fixture");
  {
    QSqlDatabase setup = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    setup.setDatabaseName(database);
    QVERIFY(setup.open());
    QVERIFY(setup.transaction());
    QSqlQuery gameQuery(setup);
    gameQuery.prepare(QStringLiteral("INSERT INTO games(app_id, title) VALUES(?, ?)"));
    QSqlQuery ownedQuery(setup);
    ownedQuery.prepare(
        QStringLiteral("INSERT INTO owned_games(steam_id, app_id, playtime_minutes, synced_at) "
                       "VALUES(?, ?, ?, 1700000000)"));
    for (int index = 1; index <= 2000; ++index) {
      const QString appId = QString::number(index);
      gameQuery.bindValue(0, appId);
      gameQuery.bindValue(1, QStringLiteral("Owned Game %1").arg(index));
      QVERIFY(gameQuery.exec());
      ownedQuery.bindValue(0, steamId);
      ownedQuery.bindValue(1, appId);
      ownedQuery.bindValue(2, index);
      QVERIFY(ownedQuery.exec());
    }
    QVERIFY(setup.commit());
    setup.close();
  }
  QSqlDatabase::removeDatabase(connection);

  SteamGameModel games(database, &settings);
  QCOMPARE(games.rowCount(), 2000);
  LibraryFilterModel library;
  library.setSourceModel(&games);
  QCOMPARE(library.rowCount(), 0);
  library.setAvailability(LibraryFilterModel::Availability::AllGames);
  QCOMPARE(library.rowCount(), 2000);
  library.setAvailability(LibraryFilterModel::Availability::ReadyToInstall);
  QCOMPARE(library.rowCount(), 2000);
}

void CoreTests::steamLauncherBuildsSafeUrls() {
  QCOMPARE(SteamLauncher::launchUrl(QStringLiteral("440")),
           QUrl(QStringLiteral("steam://rungameid/440")));
  QCOMPARE(SteamLauncher::manageUrl(QStringLiteral("440")),
           QUrl(QStringLiteral("steam://nav/games/details/440")));
  QCOMPARE(SteamLauncher::installUrl(QStringLiteral("440")),
           QUrl(QStringLiteral("steam://install/440")));
  QCOMPARE(SteamLauncher::launchUrl(QStringLiteral("3236138358")),
           QUrl(QStringLiteral("steam://rungameid/13899108412974694400")));
  QCOMPARE(SteamLauncher::launchUrl(QStringLiteral("13899108412974694400")),
           QUrl(QStringLiteral("steam://rungameid/13899108412974694400")));
  QVERIFY(SteamLauncher::launchUrl(QStringLiteral("440;touch /tmp/nope")).isEmpty());
  QVERIFY(SteamLauncher::installUrl(QStringLiteral("440;touch /tmp/nope")).isEmpty());
}

void CoreTests::lutrisScannerImportsOnlyLaunchableGames() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString dataRoot = directory.path() + QStringLiteral("/lutris");
  createLutrisFixture(dataRoot);

  const LutrisScanResult result = LutrisScanner::scan({dataRoot + QStringLiteral("/pga.db")});
  QVERIFY(!result.incomplete);
  QCOMPARE(result.games.size(), 1);
  QCOMPARE(result.games.constFirst().id, QStringLiteral("7"));
  QCOMPARE(result.games.constFirst().title, QStringLiteral("Signal Hill"));
  QCOMPARE(result.games.constFirst().playtimeMinutes, 150);
  QVERIFY(result.games.constFirst().coverPath.endsWith(QStringLiteral("signal-hill.jpg")));
}

void CoreTests::lutrisModelIsRepeatableAndPreservesLocalState() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString dataRoot = directory.path() + QStringLiteral("/lutris");
  const QString database = directory.path() + QStringLiteral("/omakade.sqlite3");
  createLutrisFixture(dataRoot);

  LutrisGameModel model(database);
  const QString source = dataRoot + QStringLiteral("/pga.db");
  model.refreshFromDatabases({source});
  QCOMPARE(model.rowCount(), 1);
  QCOMPARE(model.detectedPaths(), QStringList({source}));
  QVERIFY(model.lastScan() > 0);
  model.toggleFavorite(0);
  model.toggleHidden(0);
  model.refreshFromDatabases({source});
  QCOMPARE(model.rowCount(), 1);
  QVERIFY(model.data(model.index(0), GameRoles::Favorite).toBool());
  QVERIFY(model.data(model.index(0), GameRoles::Hidden).toBool());
  LutrisGameModel reloaded(database);
  QCOMPARE(reloaded.detectedPaths(), QStringList({source}));
  QCOMPARE(reloaded.lastScan(), model.lastScan());
}

void CoreTests::malformedLutrisDataDoesNotReplaceCachedGames() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString dataRoot = directory.path() + QStringLiteral("/lutris");
  const QString database = directory.path() + QStringLiteral("/omakade.sqlite3");
  createLutrisFixture(dataRoot);
  LutrisGameModel model(database);
  model.refreshFromDatabases({dataRoot + QStringLiteral("/pga.db")});
  QCOMPARE(model.rowCount(), 1);

  const QString malformed = directory.path() + QStringLiteral("/malformed.db");
  writeFile(malformed, "not sqlite");
  model.refreshFromDatabases({malformed});
  QCOMPARE(model.rowCount(), 1);
  QVERIFY(model.statusText().startsWith(QStringLiteral("Lutris scan interrupted")));
}

void CoreTests::unifiedLibraryFiltersSourcesAndRoutesFavorites() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString dataRoot = directory.path() + QStringLiteral("/lutris");
  createLutrisFixture(dataRoot);
  MockGameModel demo(nullptr, 2);
  LutrisGameModel lutris(directory.path() + QStringLiteral("/omakade.sqlite3"));
  lutris.refreshFromDatabases({dataRoot + QStringLiteral("/pga.db")});
  UnifiedGameModel games;
  games.addSourceModel(&demo);
  games.addSourceModel(&lutris);
  LibraryFilterModel library;
  library.setSourceModel(&games);

  QCOMPARE(library.rowCount(), 3);
  library.setSourceFilter(QStringLiteral("Lutris"));
  QCOMPARE(library.rowCount(), 1);
  QCOMPARE(library.get(0).value(QStringLiteral("title")).toString(), QStringLiteral("Signal Hill"));
  library.toggleFavorite(0);
  QVERIFY(lutris.data(lutris.index(0), GameRoles::Favorite).toBool());
}

void CoreTests::unifiedLibraryCanDisableSourcesAtRuntime() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString dataRoot = directory.path() + QStringLiteral("/lutris");
  createLutrisFixture(dataRoot);
  MockGameModel demo(nullptr, 2);
  LutrisGameModel lutris(directory.path() + QStringLiteral("/omakade.sqlite3"));
  lutris.refreshFromDatabases({dataRoot + QStringLiteral("/pga.db")});
  UnifiedGameModel games;
  games.addSourceModel(&demo);
  games.addSourceModel(&lutris);
  QCOMPARE(games.rowCount(), 3);
  games.setSourceEnabled(QStringLiteral("Lutris"), false);
  QCOMPARE(games.rowCount(), 2);
  games.setSourceEnabled(QStringLiteral("Lutris"), true);
  QCOMPARE(games.rowCount(), 3);
}

void CoreTests::artworkSlotsMigratePersistAndResetIndependently() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString database = directory.path() + QStringLiteral("/library.sqlite3");
  const QString source = directory.path() + QStringLiteral("/transparent logo.png");
  QImage image(180, 90, QImage::Format_ARGB32);
  image.fill(Qt::transparent);
  image.setPixelColor(10, 10, Qt::white);
  QVERIFY(image.save(source));
  const QString legacy = directory.path() + QStringLiteral("/legacy.png");
  QVERIFY(image.save(legacy));
  // Migrate the exact pre-feature artwork table without discarding its cover.
  const QString fixtureConnection = QStringLiteral("legacy-artwork");
  {
    QSqlDatabase fixture = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), fixtureConnection);
    fixture.setDatabaseName(database);
    QVERIFY(fixture.open());
    QSqlQuery query(fixture);
    QVERIFY(query.exec(QStringLiteral("CREATE TABLE artwork_overrides (source TEXT NOT NULL, runner TEXT NOT NULL, "
                                     "app_id TEXT NOT NULL, cover_path TEXT NOT NULL, PRIMARY KEY(source, runner, app_id))")));
    query.prepare(QStringLiteral("INSERT INTO artwork_overrides VALUES('Demo', '', 'demo-0', ?)"));
    query.addBindValue(legacy);
    QVERIFY(query.exec());
  }
  QSqlDatabase::removeDatabase(fixtureConnection);
  QString heroPath, logoPath;
  {
    MockGameModel demo(nullptr, 2);
    UnifiedGameModel games(database);
    games.addSourceModel(&demo);
    LibraryFilterModel library;
    library.setSourceModel(&games);
    QCOMPARE(library.get(0).value(QStringLiteral("coverPath")).toString(), QUrl::fromLocalFile(legacy).toString());
    QVERIFY(library.setCustomArtwork(0, QStringLiteral("hero"), QUrl::fromLocalFile(source)));
    QVERIFY(library.setCustomArtwork(0, QStringLiteral("logo"), QUrl::fromLocalFile(source)));
    heroPath = QUrl(library.get(0).value(QStringLiteral("heroPath")).toString()).toLocalFile();
    logoPath = QUrl(library.get(0).value(QStringLiteral("logoPath")).toString()).toLocalFile();
    QVERIFY(heroPath != logoPath);
    QVERIFY(QImage(logoPath).pixelColor(0, 0).alpha() == 0);
    QVERIFY(!library.setCustomArtwork(0, QStringLiteral("invalid"), QUrl::fromLocalFile(source)));
    writeFile(directory.path() + QStringLiteral("/invalid.png"), "not an image");
    QVERIFY(!library.setCustomArtwork(0, QStringLiteral("logo"),
                                     QUrl::fromLocalFile(directory.path() + QStringLiteral("/invalid.png"))));
    QCOMPARE(QUrl(library.get(0).value(QStringLiteral("logoPath")).toString()).toLocalFile(), logoPath);
    QVERIFY(library.get(0).value(QStringLiteral("customCover")).toBool());
    QVERIFY(library.get(0).value(QStringLiteral("customHero")).toBool());
    QVERIFY(library.get(0).value(QStringLiteral("customLogo")).toBool());
    const QVariantMap second = library.installations(1).first().toMap();
    QVERIFY(library.linkGames(0, second.value(QStringLiteral("source")).toString(),
                              second.value(QStringLiteral("runner")).toString(),
                              second.value(QStringLiteral("appId")).toString()));
    QVERIFY(library.get(0).value(QStringLiteral("customLogo")).toBool());
    QVERIFY(library.unlinkGames(0));
    QVERIFY(QFile::remove(source));
  }
  MockGameModel demo(nullptr, 2);
  UnifiedGameModel games(database);
  games.addSourceModel(&demo);
  LibraryFilterModel library;
  library.setSourceModel(&games);
  QVERIFY(library.get(0).value(QStringLiteral("customLogo")).toBool());
  QVERIFY(library.resetCustomArtwork(0, QStringLiteral("hero")));
  QVERIFY(!QFileInfo::exists(heroPath));
  QVERIFY(QFileInfo::exists(logoPath));
  QVERIFY(QFileInfo::exists(legacy));
  QVERIFY(library.get(0).value(QStringLiteral("customCover")).toBool());
  QVERIFY(library.get(0).value(QStringLiteral("customLogo")).toBool());
  QVERIFY(!library.get(0).value(QStringLiteral("customHero")).toBool());
  QCOMPARE(library.get(0).value(QStringLiteral("heroPath")), demo.data(demo.index(0), GameRoles::HeroPath));
  QVERIFY(library.resetCustomArtwork(0, QStringLiteral("logo")));
  QVERIFY(library.resetCustomCover(0));
  // A migrated external path is not owned by Omakade and must never be deleted.
  QVERIFY(QFileInfo::exists(legacy));
}

void CoreTests::backupSettingsApplyAtomicallyAndKeepAccounts() {
  QTemporaryDir temp;
  const QString path = temp.filePath("settings.toml");
  AppSettings settings(path);
  settings.setSteamId("76561198000000000");
  settings.setIgdbClientId("localclient");
  settings.setRetroAchievementsUsername("local-user");
  QCOMPARE(settings.igdbClientId(), QString("localclient"));
  settings.setSunshineGameApps(true);
  settings.setCouchModeEnabled(true);
  QVERIFY(settings.applyBackupSettings({{"reduced_motion", true}, {"gog_library_paths", QJsonArray{"/offline/GOG"}}}, false));
  QVERIFY(settings.reducedMotion()); QVERIFY(settings.couchModeEnabled());
  QCOMPARE(settings.gogLibraryPaths(), QStringList{"/offline/GOG"});
  QVERIFY(settings.applyBackupSettings({{"reduced_motion", true}}, true));
  QVERIFY(settings.reducedMotion()); QVERIFY(!settings.couchModeEnabled());
  QVERIFY(settings.gogLibraryPaths().isEmpty());
  QCOMPARE(settings.steamId(), QString("76561198000000000"));
  QCOMPARE(settings.igdbClientId(), QString("localclient"));
  QCOMPARE(settings.retroAchievementsUsername(), QString("local-user"));
  QVERIFY(settings.sunshineGameApps());
  AppSettings reopened(path);
  QCOMPARE(reopened.backupSettings(), settings.backupSettings());
  QCOMPARE(reopened.steamId(), settings.steamId());
  const auto before = settings.backupSettings();
  QVERIFY(!settings.applyBackupSettings({{"steam_id", "not-allowed"}}, false));
  QCOMPARE(settings.backupSettings(), before);
  const QString unavailable = temp.filePath("unavailable");
  writeFile(unavailable, "a regular file blocks the parent directory");
  AppSettings blocked(unavailable + "/settings.toml");
  const auto unchanged = blocked.backupSettings();
  QVERIFY(!blocked.applyBackupSettings({{"reduced_motion", true}}, false));
  QCOMPARE(blocked.backupSettings(), unchanged);
}

void CoreTests::backupDatabaseMergeReplaceAndRollback() {
  QTemporaryDir sourceRoot, targetRoot;
  const QString sourcePath = sourceRoot.filePath("library.sqlite");
  const QString targetPath = targetRoot.filePath("library.sqlite");
  const QString executable = sourceRoot.filePath("native.sh");
  writeFile(executable, "#!/bin/sh\nexit 0\n");
  QVERIFY(QFile::setPermissions(executable, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
  QString manualId, archivedFilter;
  {
    ManualGameModel manual(sourcePath);
    manualId = manual.saveEntry({{"title", "Portable manual"}, {"executable", executable},
        {"directory", sourceRoot.path()}, {"arguments", QStringList{"", "literal argument"}}});
    QVERIFY(!manualId.isEmpty());
    MockGameModel source(nullptr, 4);
    UnifiedGameModel model(sourcePath); model.addSourceModel(&source); model.addSourceModel(&manual);
    LibraryFilterModel filter; filter.setSourceModel(&model);
    QVERIFY(filter.linkGames(filter.indexOf("Demo", "", "demo-0"), "Demo", "", "demo-1"));
    const int row = filter.indexOf("Demo", "", "demo-0");
    QVERIFY(filter.setPreferredInstallation(row, "Demo", "", "demo-1"));
    filter.toggleSelection(row);
    QVERIFY(filter.applyBulkChanges({{"favorite", true}, {"status", "completed"},
        {"tagsAdd", "portable"}, {"collection", "Weekend"}, {"collectionIncluded", true}}));
    QImage image(32, 64, QImage::Format_ARGB32); image.fill(QColor(10, 50, 90, 100));
    const QString cover = sourceRoot.filePath("cover.png"); QVERIFY(image.save(cover));
    QVERIFY(filter.setCustomArtwork(row, "cover", QUrl::fromLocalFile(cover)));
    archivedFilter = filter.saveCurrentFilter("Weekend"); QVERIFY(!archivedFilter.isEmpty());
  }
  BackupPayload incoming; QString error;
  QVERIFY2(BackupSnapshot::capture(sourcePath, {}, &incoming, &error), qPrintable(error));
  QVERIFY(QFile::remove(executable));
  {
    MockGameModel source(nullptr, 4);
    UnifiedGameModel model(targetPath); model.addSourceModel(&source);
    LibraryFilterModel filter; filter.setSourceModel(&model);
    QVERIFY(filter.linkGames(filter.indexOf("Demo", "", "demo-1"), "Demo", "", "demo-2"));
    QVERIFY(filter.linkGames(filter.indexOf("Demo", "", "demo-1"), "Demo", "", "demo-3"));
    QVERIFY(filter.setPreferredInstallation(filter.indexOf("Demo", "", "demo-1"), "Demo", "", "demo-2"));
    QVERIFY(!filter.saveCurrentFilter("Weekend").isEmpty());
    QVERIFY(filter.createCollection("Local only"));
  }
  {
    auto database = QSqlDatabase::addDatabase("QSQLITE", "restore-fixture"); database.setDatabaseName(targetPath); QVERIFY(database.open());
    QSqlQuery query(database);
    QVERIFY(query.exec("CREATE TABLE games(app_id TEXT PRIMARY KEY, favorite INTEGER, hidden INTEGER, title TEXT)"));
    QVERIFY(query.exec("INSERT INTO games VALUES('target-only', 1, 1, 'cache stays')"));
  }
  QSqlDatabase::removeDatabase("restore-fixture");
  QVERIFY2(BackupDatabase::restore(targetPath, incoming, BackupDatabase::Mode::Merge, &error), qPrintable(error));
  BackupPayload merged;
  QVERIFY2(BackupSnapshot::capture(targetPath, {}, &merged, &error), qPrintable(error));
  QCOMPARE(merged.library.value("saved_filters").toArray().size(), 2);
  QCOMPARE(merged.library.value("game_link_members").toArray().size(), 4);
  QCOMPARE(merged.library.value("launch_preferences").toArray().size(), 2);
  QString restoredName;
  for (const auto& value : merged.library.value("saved_filters").toArray()) {
    const auto row = value.toObject();
    if (row.value("id").toString() == archivedFilter) restoredName = row.value("name").toString();
  }
  QVERIFY(restoredName.contains("restored"));
  QVERIFY(BackupDatabase::restore(targetPath, incoming, BackupDatabase::Mode::Merge, &error));
  BackupPayload replayed; QVERIFY(BackupSnapshot::capture(targetPath, {}, &replayed, &error));
  QCOMPARE(replayed.library, merged.library); // Replaying a committed import is idempotent.
  {
    auto database = QSqlDatabase::addDatabase("QSQLITE", "restore-failure"); database.setDatabaseName(targetPath); QVERIFY(database.open());
    QSqlQuery query(database);
    QVERIFY(query.exec("CREATE TRIGGER reject_restore BEFORE INSERT ON game_organization BEGIN SELECT RAISE(ABORT, 'injected restore failure'); END"));
  }
  QSqlDatabase::removeDatabase("restore-failure");
  QVERIFY(!BackupDatabase::restore(targetPath, incoming, BackupDatabase::Mode::Replace, &error));
  BackupPayload afterFailure; QVERIFY(BackupSnapshot::capture(targetPath, {}, &afterFailure, &error));
  QCOMPARE(afterFailure.library, merged.library);
  {
    auto database = QSqlDatabase::addDatabase("QSQLITE", "restore-failure"); database.setDatabaseName(targetPath); QVERIFY(database.open());
    QSqlQuery query(database); QVERIFY(query.exec("DROP TRIGGER reject_restore"));
  }
  QSqlDatabase::removeDatabase("restore-failure");
  QVERIFY2(BackupDatabase::restore(targetPath, incoming, BackupDatabase::Mode::Replace, &error), qPrintable(error));
  BackupPayload replaced; QVERIFY2(BackupSnapshot::capture(targetPath, {}, &replaced, &error), qPrintable(error));
  QCOMPARE(replaced.library.value("saved_filters").toArray().size(), 1);
  QCOMPARE(replaced.library.value("saved_filters").toArray().first().toObject().value("name").toString(), QString("Weekend"));
  QCOMPARE(replaced.library.value("game_link_members").toArray().size(), 2);
  QCOMPARE(replaced.library.value("collections").toArray().size(), 1);
  {
    auto database = QSqlDatabase::addDatabase("QSQLITE", "restore-check"); database.setDatabaseName(targetPath); QVERIFY(database.open());
    QSqlQuery query(database); QVERIFY(query.exec("SELECT favorite, hidden, title FROM games WHERE app_id='target-only'"));
    QVERIFY(query.next()); QCOMPARE(query.value(0).toInt(), 0); QCOMPARE(query.value(1).toInt(), 0); QCOMPARE(query.value(2).toString(), QString("cache stays"));
    query.finish();
    QVERIFY(query.exec("SELECT cover_path FROM artwork_overrides")); QVERIFY(query.next());
    QVERIFY(query.value(0).toString().startsWith(targetRoot.filePath("artwork/")));
    QVERIFY(QFileInfo::exists(query.value(0).toString()));
  }
  QSqlDatabase::removeDatabase("restore-check");
  ManualGameModel manual(targetPath);
  QCOMPARE(manual.count(), 1);
  QCOMPARE(manual.get(manualId).value("arguments").toStringList(), (QStringList{"", "literal argument"}));
  QVERIFY(!QFileInfo::exists(manual.get(manualId).value("executable").toString()));
}

void CoreTests::backupSnapshotConsolidatesLegacyPersonalState() {
  QTemporaryDir temp;
  const QString path = temp.filePath("legacy.sqlite");
  const QString cover = temp.filePath("cover.png");
  QImage image(64, 96, QImage::Format_RGB32); image.fill(Qt::green); QVERIFY(image.save(cover));
  {
    auto database = QSqlDatabase::addDatabase("QSQLITE", "backup-legacy-fixture");
    database.setDatabaseName(path); QVERIFY(database.open());
    QSqlQuery query(database);
    const QStringList statements{
      "CREATE TABLE games(app_id TEXT, favorite INTEGER, hidden INTEGER, title TEXT)",
      "INSERT INTO games VALUES('123', 1, 1, 'cached title not exported')",
      "CREATE TABLE lutris_games(id TEXT, favorite INTEGER, hidden INTEGER)",
      "INSERT INTO lutris_games VALUES('lutris-1', 1, 0)",
      "CREATE TABLE heroic_games(app_id TEXT, runner TEXT, favorite INTEGER, hidden INTEGER)",
      "INSERT INTO heroic_games VALUES('gog-1', 'gog-direct', 1, 0), ('epic-1', 'legendary', 0, 1)",
      "CREATE TABLE faugus_games(game_id TEXT, favorite INTEGER, hidden INTEGER)",
      "INSERT INTO faugus_games VALUES('faugus-1', 1, 0)",
      "CREATE TABLE retroarch_games(game_id TEXT, favorite INTEGER, hidden INTEGER)",
      "INSERT INTO retroarch_games VALUES('retro-1', 1, 0)",
      "CREATE TABLE pcsx2_games(path TEXT, serial TEXT, favorite INTEGER, hidden INTEGER)",
      "INSERT INTO pcsx2_games VALUES('/games/disc.iso', 'SLES-123', 1, 0)",
      "CREATE TABLE ryujinx_games(game_id TEXT, flatpak_app_id TEXT, favorite INTEGER, hidden INTEGER)",
      "INSERT INTO ryujinx_games VALUES('ryu-1', 'org.ryujinx.Ryujinx', 1, 0)",
      "CREATE TABLE battlenet_games(game_id TEXT, runner TEXT, favorite INTEGER, hidden INTEGER)",
      "INSERT INTO battlenet_games VALUES('battle-1', 'wine', 1, 0)",
      "CREATE TABLE user_game_flags(source TEXT, runner TEXT, app_id TEXT, favorite INTEGER, hidden INTEGER)",
      "INSERT INTO user_game_flags VALUES('Steam', '', '123', 0, NULL)",
      "CREATE TABLE account_tokens(token TEXT)",
      "INSERT INTO account_tokens VALUES('fake-private-token-must-stay-out')",
      "CREATE TABLE artwork_overrides(source TEXT, runner TEXT, app_id TEXT, cover_path TEXT)"};
    for (const auto& statement : statements) QVERIFY2(query.exec(statement), qPrintable(query.lastError().text()));
    query.prepare("INSERT INTO artwork_overrides VALUES('Steam', '', '123', ?)"); query.addBindValue(cover); QVERIFY(query.exec());
  }
  QSqlDatabase::removeDatabase("backup-legacy-fixture");
  AppSettings settings(temp.filePath("settings.toml"));
  settings.setSteamId("76561198000000000");
  settings.setIgdbClientId("fakeprivateclient");
  settings.setRetroAchievementsUsername("private-user");
  QFile original(path); QVERIFY(original.open(QIODevice::ReadOnly)); const auto originalBytes = original.readAll(); original.close();
  BackupPayload snapshot;
  QString error;
  QVERIFY2(BackupSnapshot::capture(path, settings.backupSettings(), &snapshot, &error), qPrintable(error));
  QCOMPARE(snapshot.library.value("user_game_flags").toArray().size(), 9);
  QMap<QString, QJsonObject> flags;
  for (const auto& entry : snapshot.library.value("user_game_flags").toArray()) {
    const auto row = entry.toObject(); flags.insert(row.value("source").toString(), row);
  }
  QVERIFY(!flags.value("Steam").value("favorite").toBool());
  QVERIFY(flags.value("Steam").value("hidden").toBool());
  QCOMPARE(flags.value("GOG").value("runner").toString(), QString("gog-direct"));
  QCOMPARE(flags.value("PCSX2").value("app_id").toString(), QString("path:/games/disc.iso"));
  QCOMPARE(flags.value("PCSX2").value("runner").toString(), QString("SLES-123"));
  QCOMPARE(flags.value("Ryujinx").value("runner").toString(), QString("org.ryujinx.Ryujinx"));
  const auto serialized = QJsonDocument(snapshot.library).toJson() + QJsonDocument(snapshot.settings).toJson();
  QVERIFY(!serialized.contains("fake-private"));
  QVERIFY(!serialized.contains("fakeprivateclient"));
  QVERIFY(!serialized.contains("private-user"));
  QVERIFY(!serialized.contains("76561198000000000"));
  QVERIFY(!serialized.contains("cached title"));
  const auto artwork = snapshot.library.value("artwork_overrides").toArray().first().toObject();
  QVERIFY(artwork.value("cover_path").toString().startsWith("artwork/"));
  QVERIFY(artwork.value("hero_path").toString().isEmpty());
  QCOMPARE(snapshot.artwork.size(), 1);
  QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(), originalBytes); original.close();
  QVERIFY2(BackupArchive::write(temp.filePath("export.omakade-backup"), snapshot, &error), qPrintable(error));
  const auto goodLibrary = snapshot.library;
  QVERIFY(QFile::remove(cover));
  QVERIFY(!BackupSnapshot::capture(path, settings.backupSettings(), &snapshot, &error));
  QCOMPARE(snapshot.library, goodLibrary);
}

void CoreTests::backupArchiveRoundTripsAndRejectsInvalidContent() {
  QTemporaryDir temp;
  const QString path = temp.filePath("library.omakade-backup");
  QImage image(64, 32, QImage::Format_ARGB32);
  image.fill(QColor(30, 70, 110, 120));
  const QString imagePath = temp.filePath("source.png");
  QVERIFY(image.save(imagePath));
  QFile imageFile(imagePath); QVERIFY(imageFile.open(QIODevice::ReadOnly));
  const QByteArray imageBytes = imageFile.readAll();
  QString error;
  const QString art = BackupArchive::artworkName(imageBytes, &error);
  QVERIFY2(!art.isEmpty(), qPrintable(error));
  const QJsonObject manual{{"id", "manual-test"}, {"title", "Missing native game"},
      {"executable", "/missing/game/start.sh"}, {"directory", "/missing/game"},
      {"arguments", QJsonArray{"two words", "", "$literal"}}};
  BackupPayload original;
  original.createdAt = "2026-09-05T12:00:00Z";
  original.settings = {{"reduced_motion", true}, {"artwork_cache_limit_mb", 1024},
      {"gog_library_paths", QJsonArray{"/mnt/offline/GOG Games"}}};
  original.library = {
      {"user_game_flags", QJsonArray{QJsonObject{{"source", "Steam"}, {"runner", ""}, {"app_id", "123"}, {"favorite", true}, {"hidden", QJsonValue::Null}}}},
      {"manual_games", QJsonArray{QJsonObject{{"id", "manual-test"}, {"entry", QString::fromUtf8(QJsonDocument(manual).toJson(QJsonDocument::Compact))}, {"favorite", false}, {"hidden", false}, {"active", true}}}},
      {"artwork_overrides", QJsonArray{QJsonObject{{"source", "Steam"}, {"runner", ""}, {"app_id", "123"}, {"cover_path", art}, {"hero_path", ""}, {"logo_path", ""}}}}};
  original.artwork.insert(art, imageBytes);
  QVERIFY2(BackupArchive::write(path, original, &error), qPrintable(error));
  BackupPayload restored;
  QVERIFY2(BackupArchive::read(path, &restored, &error), qPrintable(error));
  QCOMPARE(restored.library, original.library);
  QCOMPARE(restored.settings, original.settings);
  QCOMPARE(restored.artwork, original.artwork);
  QCOMPARE(restored.createdAt, original.createdAt);
  QVERIFY(QFile::remove(imagePath));
  QVERIFY(BackupArchive::read(path, &restored, &error));
  QVERIFY((QFileInfo(path).permissions() & (QFile::ReadOther | QFile::WriteOther)) == 0);
  QFile goodFile(path); QVERIFY(goodFile.open(QIODevice::ReadOnly)); const QByteArray before = goodFile.readAll(); goodFile.close();
  auto invalid = original;
  invalid.settings.insert("steam_api_key", "not-a-real-key");
  QVERIFY(!BackupArchive::write(path, invalid, &error));
  QVERIFY(goodFile.open(QIODevice::ReadOnly)); QCOMPARE(goodFile.readAll(), before); goodFile.close();
  invalid = original; invalid.artwork[art] = "not an image";
  QVERIFY(!BackupArchive::validate(invalid, &error));
  invalid = original; invalid.library.insert("achievement_summary", QJsonArray{});
  QVERIFY(!BackupArchive::validate(invalid, &error));
  invalid = original; invalid.artwork.clear();
  QVERIFY(!BackupArchive::validate(invalid, &error));
  invalid = original;
  auto flags = invalid.library.value("user_game_flags").toArray(); flags.append(flags.first());
  invalid.library.insert("user_game_flags", flags);
  QVERIFY(!BackupArchive::validate(invalid, &error));

  const auto rawArchive = [&](const QString& file, const QJsonObject& manifest, const QString& extraPath) {
    const QByteArray json = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    int code = 0;
    auto* zip = zip_open(QFile::encodeName(file).constData(), ZIP_CREATE | ZIP_TRUNCATE, &code);
    QVERIFY(zip != nullptr);
    auto* source = zip_source_buffer(zip, json.constData(), zip_uint64_t(json.size()), 0);
    QVERIFY(source != nullptr);
    QVERIFY(zip_file_add(zip, "manifest.json", source, ZIP_FL_ENC_UTF_8) >= 0);
    if (!extraPath.isEmpty()) {
      auto* extra = zip_source_buffer(zip, "unexpected", 10, 0);
      QVERIFY(extra != nullptr);
      QVERIFY(zip_file_add(zip, extraPath.toUtf8().constData(), extra, ZIP_FL_ENC_UTF_8) >= 0);
    }
    QCOMPARE(zip_close(zip), 0);
  };
  const QJsonObject emptyManifest{{"format", "omakade-backup"}, {"version", 1},
      {"createdAt", original.createdAt}, {"library", QJsonObject{}}, {"settings", QJsonObject{}}, {"artwork", QJsonArray{}}};
  const QString badPath = temp.filePath("bad.zip");
  rawArchive(badPath, emptyManifest, "unexpected.txt");
  restored = original;
  QVERIFY(!BackupArchive::read(badPath, &restored, &error));
  QCOMPARE(restored.library, original.library); // Invalid reads never replace the caller's payload.
  auto future = emptyManifest; future.insert("version", 2);
  rawArchive(badPath, future, {});
  QVERIFY(!BackupArchive::read(badPath, &restored, &error));
  writeFile(badPath, before.left(before.size() / 2));
  QVERIFY(!BackupArchive::read(badPath, &restored, &error));
}

void CoreTests::completionWorkflowPersistsAtLibraryScale() {
  QTemporaryDir original, restored;
  const QString path = original.filePath("library.sqlite");
  QString savedId;
  {
    MockGameModel source(nullptr, 1000);
    UnifiedGameModel model(path);
    model.addSourceModel(&source);
    LibraryFilterModel filter;
    filter.setSourceModel(&model);
    filter.setShowHidden(true);
    QCOMPARE(filter.rowCount(), 1000);
    filter.selectAllFiltered();
    QCOMPARE(filter.selectionCount(), 1000);
    QVERIFY(filter.applyBulkChanges({{"favorite", true}, {"hidden", false},
        {"status", "backlog"}, {"tagsAdd", "weekend"},
        {"collection", "Large library"}, {"collectionIncluded", true}}));
    QCOMPARE(filter.selectionCount(), 0);
    QVERIFY(filter.linkGames(filter.indexOf("Demo", "", "demo-0"), "Demo", "", "demo-1"));
    QCOMPARE(filter.rowCount(), 999);
    const int linked = filter.indexOf("Demo", "", "demo-0");
    QVERIFY(filter.setPreferredInstallation(linked, "Demo", "", "demo-1"));
    QImage image(32, 64, QImage::Format_ARGB32);
    image.fill(QColor(20, 40, 60, 100));
    const QString artwork = original.filePath("chosen.png");
    QVERIFY(image.save(artwork));
    for (const QString& slot : {QString("cover"), QString("hero"), QString("logo")})
      QVERIFY(filter.setCustomArtwork(linked, slot, QUrl::fromLocalFile(artwork)));
    QVERIFY(QFile::remove(artwork));
    filter.setMode(LibraryFilterModel::Mode::Favorites);
    filter.setCollectionFilter("Large library");
    filter.setTagFilter("weekend");
    filter.setCompletionFilter("backlog");
    filter.setSortMode(LibraryFilterModel::SortMode::RecentlyPlayed);
    savedId = filter.saveCurrentFilter("Large weekend library");
    QVERIFY(!savedId.isEmpty());
  }
  BackupPayload payload;
  QString error;
  QVERIFY2(BackupSnapshot::capture(path, {}, &payload, &error), qPrintable(error));
  QCOMPARE(payload.library.value("game_organization").toArray().size(), 1000);
  const QString archive = original.filePath("library.omakade-backup");
  QVERIFY2(BackupArchive::write(archive, payload, &error), qPrintable(error));
  BackupPayload imported;
  QVERIFY2(BackupArchive::read(archive, &imported, &error), qPrintable(error));
  const QString restoredPath = restored.filePath("library.sqlite");
  QVERIFY2(BackupDatabase::restore(restoredPath, imported, BackupDatabase::Mode::Replace, &error),
           qPrintable(error));
  for (const QString& database : {path, restoredPath}) {
    MockGameModel source(nullptr, 1000);
    UnifiedGameModel model(database);
    model.addSourceModel(&source);
    LibraryFilterModel filter;
    filter.setSourceModel(&model);
    QVERIFY(filter.applySavedFilter(savedId));
    QCOMPARE(filter.rowCount(), 999);
    for (int row = 0; row < filter.rowCount(); ++row) {
      const auto game = filter.get(row);
      QVERIFY(game.value("favorite").toBool());
      QCOMPARE(game.value("completionStatus").toString(), QString("backlog"));
      QVERIFY(game.value("tags").toStringList().contains("weekend"));
      QVERIFY(game.value("collections").toStringList().contains("Large library"));
    }
    const int linked = filter.indexOf("Demo", "", "demo-0");
    const auto game = filter.get(linked);
    for (const QString& flag : {QString("customCover"), QString("customHero"), QString("customLogo")})
      QVERIFY(game.value(flag).toBool());
    bool preferred = false;
    for (const auto& item : filter.installations(linked)) {
      const auto installation = item.toMap();
      if (installation.value("appId").toString() == "demo-1")
        preferred = installation.value("preferred").toBool();
    }
    QVERIFY(preferred);
    QString previous;
    for (int pick = 0; pick < 25; ++pick) {
      const int row = filter.pickRandomGame();
      QVERIFY(row >= 0);
      const QString id = filter.get(row).value("appId").toString();
      QVERIFY(id != previous);
      previous = id;
    }
  }
}

void CoreTests::bulkOrganizationIsAtomicAndPreservesSelection() {
  QTemporaryDir temp;
  const QString path = temp.filePath("library.sqlite");
  {
    MockGameModel source(nullptr, 4);
    UnifiedGameModel model(path);
    model.addSourceModel(&source);
    LibraryFilterModel filter;
    filter.setSourceModel(&model);
    QVERIFY(filter.createCollection("Focus"));
    QVERIFY(filter.setCollectionMembership(0, "Focus", true));
    QVERIFY(filter.setCollectionMembership(1, "Focus", true));
    QVERIFY(filter.setTags(0, "old, keep"));
    filter.setCollectionFilter("Focus");
    filter.selectAllFiltered();
    QCOMPARE(filter.selectionCount(), 2);
    filter.setSortMode(LibraryFilterModel::SortMode::Playtime);
    QVERIFY(filter.isSelected(0)); QVERIFY(filter.isSelected(1));
    filter.setCollectionFilter({});
    QCOMPARE(filter.rowCount(), 4);
    QCOMPARE(filter.selectionCount(), 2);
    const auto before0 = filter.get(filter.indexOf("Demo", "", "demo-0"));
    const auto before1 = filter.get(filter.indexOf("Demo", "", "demo-1"));
    {
      auto database = QSqlDatabase::addDatabase("QSQLITE", "bulk-failure-fixture");
      database.setDatabaseName(path);
      QVERIFY(database.open());
      QSqlQuery query(database);
      QVERIFY(query.exec("CREATE TRIGGER fail_batch BEFORE INSERT ON user_game_flags "
          "WHEN (SELECT count(*) FROM user_game_flags) > 0 BEGIN SELECT RAISE(ABORT, 'injected failure'); END"));
      QVERIFY(!filter.applyBulkChanges({{"favorite", true}, {"status", "completed"}}));
      QCOMPARE(filter.selectionCount(), 2);
      QCOMPARE(filter.get(filter.indexOf("Demo", "", "demo-0")), before0);
      QCOMPARE(filter.get(filter.indexOf("Demo", "", "demo-1")), before1);
      QVERIFY(query.exec("SELECT count(*) FROM user_game_flags"));
      QVERIFY(query.next()); QCOMPARE(query.value(0).toInt(), 0);
      query.finish();
      QVERIFY(query.exec("DROP TRIGGER fail_batch"));
    }
    QSqlDatabase::removeDatabase("bulk-failure-fixture");
    QVERIFY(filter.applyBulkChanges({{"favorite", true}, {"status", "completed"},
        {"tagsAdd", "new"}, {"tagsRemove", "old"}, {"collection", "Weekend"}, {"collectionIncluded", true}}));
    QCOMPARE(filter.selectionCount(), 0);
    for (const QString& id : {QStringLiteral("demo-0"), QStringLiteral("demo-1")}) {
      const auto game = filter.get(filter.indexOf("Demo", "", id));
      QVERIFY(game.value("favorite").toBool());
      QCOMPARE(game.value("completionStatus").toString(), QString("completed"));
      QVERIFY(game.value("tags").toStringList().contains("new"));
      QVERIFY(!game.value("tags").toStringList().contains("old"));
      QVERIFY(game.value("collections").toStringList().contains("Focus"));
      QVERIFY(game.value("collections").toStringList().contains("Weekend"));
    }
    QVERIFY(filter.get(filter.indexOf("Demo", "", "demo-0")).value("tags").toStringList().contains("keep"));
    QVERIFY(!filter.get(filter.indexOf("Demo", "", "demo-2")).value("favorite").toBool());
    filter.toggleFavorite(filter.indexOf("Demo", "", "demo-0"));
    QVERIFY(!filter.get(filter.indexOf("Demo", "", "demo-0")).value("favorite").toBool());
    filter.setCollectionFilter("Focus");
    filter.selectAllFiltered();
    QVERIFY(filter.linkGames(filter.indexOf("Demo", "", "demo-0"), "Demo", "", "demo-1"));
    QCOMPARE(filter.selectionCount(), 1);
    QVERIFY(filter.applyBulkChanges({{"hidden", true}}));
    QCOMPARE(filter.rowCount(), 0);
    filter.setMode(LibraryFilterModel::Mode::Hidden);
    QCOMPARE(filter.rowCount(), 1);
    filter.toggleHidden(0);
    QCOMPARE(filter.rowCount(), 0);
    filter.setMode(LibraryFilterModel::Mode::All);
    QCOMPARE(filter.rowCount(), 1);
    QVERIFY(filter.unlinkGames(0));
    QCOMPARE(filter.rowCount(), 2);
    filter.selectAllFiltered();
    QVERIFY(!filter.applyBulkChanges({{"favorite", "false"}}));
    QCOMPARE(filter.selectionCount(), 2);
    QVERIFY(filter.applyBulkChanges({{"collection", "Focus"}, {"collectionIncluded", false}}));
    QCOMPARE(filter.rowCount(), 0);
  }
  {
    MockGameModel source(nullptr, 4);
    UnifiedGameModel model(path);
    model.addSourceModel(&source);
    LibraryFilterModel filter;
    filter.setSourceModel(&model);
    QCOMPARE(filter.rowCount(), 4);
    const auto first = filter.get(filter.indexOf("Demo", "", "demo-0"));
    QVERIFY(!first.value("favorite").toBool());
    QVERIFY(!first.value("hidden").toBool());
    QCOMPARE(first.value("completionStatus").toString(), QString("completed"));
    QVERIFY(first.value("collections").toStringList().contains("Weekend"));
    QVERIFY(!first.value("collections").toStringList().contains("Focus"));
    filter.toggleSelection(filter.indexOf("Demo", "", "demo-3"));
    QVERIFY(filter.applyBulkChanges({{"tagsAdd", "first tag"}}));
    const auto fresh = filter.get(filter.indexOf("Demo", "", "demo-3"));
    QVERIFY(fresh.value("completionStatus").toString().isEmpty());
    QVERIFY(fresh.value("tags").toStringList().contains("first tag"));
    ManualGameModel manual(path);
    const QString executable = temp.filePath("bulk-native.sh");
    writeFile(executable, "#!/bin/sh\nexit 0\n");
    QVERIFY(QFile::setPermissions(executable, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    const QString id = manual.saveEntry({{"title", "Zebra"}, {"executable", executable},
        {"directory", temp.path()}, {"arguments", QStringList{}}});
    QVERIFY(!id.isEmpty());
    model.addSourceModel(&manual);
    filter.toggleSelection(filter.indexOf("Manual", "", id));
    auto edited = manual.get(id);
    edited.insert("title", "A new title");
    QCOMPARE(manual.saveEntry(edited), id); // Rebuilds the source model and reorders the proxy.
    QCOMPARE(filter.selectionCount(), 1);
    QVERIFY(filter.isSelected(filter.indexOf("Manual", "", id)));
    QVERIFY(manual.removeEntry(id));
    QVERIFY(!filter.applyBulkChanges({{"favorite", true}}));
    QCOMPARE(filter.selectionCount(), 1); // Missing identities never turn into edits to a different row.
    filter.clearSelection();
  }
}

void CoreTests::savedFiltersPersistAndPreserveQueries() {
  QTemporaryDir temp;
  QString id;
  QVariantMap expected;
  {
    MockGameModel source(nullptr, 4);
    UnifiedGameModel model(temp.filePath("library.sqlite"));
    model.addSourceModel(&source);
    LibraryFilterModel filter;
    filter.setSourceModel(&model);
    QVERIFY(filter.createCollection("Weekend"));
    QVERIFY(filter.setCollectionMembership(0, "Weekend", true));
    QVERIFY(filter.setTags(0, "short, relaxing"));
    QVERIFY(filter.setCompletionStatus(0, "backlog"));
    filter.setSearchText("Aster");
    filter.setSourceFilter("Demo");
    filter.setCollectionFilter("Weekend");
    filter.setTagFilter("short");
    filter.setCompletionFilter("backlog");
    filter.setSortMode(LibraryFilterModel::SortMode::RecentlyPlayed);
    filter.setAvailability(LibraryFilterModel::Availability::AllGames);
    filter.setShowHidden(true);
    QCOMPARE(filter.rowCount(), 1);
    expected = filter.filterState();
    id = filter.saveCurrentFilter(" Weekend picks ");
    QVERIFY(!id.isEmpty());
    QVERIFY(filter.saveCurrentFilter("WEEKEND PICKS").isEmpty());
    QVERIFY(filter.saveCurrentFilter(" ").isEmpty());
    QVERIFY(filter.saveCurrentFilter("bad\nname").isEmpty());
    QCOMPARE(filter.savedFilters().size(), 1);
    filter.setSearchText("no game");
    filter.setMode(LibraryFilterModel::Mode::Hidden);
    QVERIFY(filter.applySavedFilter(id));
    QCOMPARE(filter.filterState(), expected);
    QCOMPARE(filter.rowCount(), 1);
    QVERIFY(filter.renameSavedFilter(id, "Quiet weekend"));
    QCOMPARE(filter.savedFilters().first().toMap().value("id").toString(), id);
    QCOMPARE(filter.savedFilters().first().toMap().value("name").toString(), QString("Quiet weekend"));
  }
  {
    MockGameModel source(nullptr, 4);
    UnifiedGameModel model(temp.filePath("library.sqlite"));
    model.addSourceModel(&source);
    LibraryFilterModel filter;
    filter.setSourceModel(&model);
    QVERIFY(filter.applySavedFilter(id));
    QCOMPARE(filter.filterState(), expected);
    QCOMPARE(filter.rowCount(), 1);
    QVERIFY(filter.deleteCollection("Weekend"));
    QVERIFY(filter.applySavedFilter(id));
    QCOMPARE(filter.collectionFilter(), QString("Weekend"));
    QCOMPARE(filter.rowCount(), 0);
    QVERIFY(filter.savedFilterMessage().contains("Weekend"));
    QVERIFY(filter.createCollection("Weekend"));
    filter.setCollectionFilter({});
    QVERIFY(filter.setCollectionMembership(0, "Weekend", true));
    QVERIFY(filter.applySavedFilter(id));
    QCOMPARE(filter.rowCount(), 1);
    QVERIFY(filter.savedFilterMessage().isEmpty());
    QVariantMap future = expected;
    future.insert("version", 999);
    QVERIFY(model.saveFilter("future", "Future format", future));
    const QVariantMap before = filter.filterState();
    QVERIFY(!filter.applySavedFilter("future"));
    QCOMPARE(filter.filterState(), before);
    QVERIFY(!filter.renameSavedFilter(id, "Future format"));
    QVERIFY(filter.removeSavedFilter(id));
    QVERIFY(!filter.removeSavedFilter(id));
    QCOMPARE(filter.filterState(), before); // Deleting a saved view does not alter the current query.
    QVERIFY(!filter.applySavedFilter(id));
  }
}

void CoreTests::randomPickRespectsFiltersAndLinkedIdentity() {
  QTemporaryDir temp;
  MockGameModel source(nullptr, 4, true);
  UnifiedGameModel model(temp.filePath("library.sqlite"));
  model.addSourceModel(&source);
  LibraryFilterModel filter;
  filter.setSourceModel(&model);
  filter.setAvailability(LibraryFilterModel::Availability::AllGames);
  QString previous;
  for (int i = 0; i < 30; ++i) {
    const int row = filter.pickRandomGame();
    QVERIFY(row >= 0);
    const auto game = filter.get(row);
    QVERIFY(game.value("installed").toBool());
    QVERIFY(game.value("appId").toString() != previous);
    previous = game.value("appId").toString();
  }
  filter.setSearchText("Black Meridian");
  QCOMPARE(filter.rowCount(), 1);
  QCOMPARE(filter.pickRandomGame(), 0);
  QCOMPARE(filter.pickRandomGame(), 0);
  filter.setSearchText({});
  filter.setAvailability(LibraryFilterModel::Availability::ReadyToInstall);
  QCOMPARE(filter.pickRandomGame(), -1);
  filter.setAvailability(LibraryFilterModel::Availability::Installed);
  QVERIFY(filter.linkGames(0, "Demo", "", "demo-2"));
  QCOMPARE(filter.rowCount(), 2);
  previous.clear();
  for (int i = 0; i < 20; ++i) {
    const int row = filter.pickRandomGame();
    QVERIFY(row >= 0);
    const QString id = filter.get(row).value("appId").toString();
    QVERIFY(id != previous);
    previous = id;
    filter.setSortMode(i % 2 ? LibraryFilterModel::SortMode::Title : LibraryFilterModel::SortMode::Playtime);
  }
  filter.setSearchText("no matching game");
  QCOMPARE(filter.pickRandomGame(), -1);

  ManualGameModel manual(temp.filePath("manual.sqlite"));
  const QString executable = temp.filePath("game.sh");
  writeFile(executable, "#!/bin/sh\nexit 0\n");
  QVERIFY(QFile::setPermissions(executable, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
  QVERIFY(!manual.saveEntry({{"title", "Manual pick"}, {"executable", executable},
      {"directory", temp.path()}, {"arguments", QStringList{}}}).isEmpty());
  UnifiedGameModel manualLibrary(temp.filePath("manual.sqlite"));
  manualLibrary.addSourceModel(&manual);
  LibraryFilterModel manualFilter;
  manualFilter.setSourceModel(&manualLibrary);
  QCOMPARE(manualFilter.pickRandomGame(), 0);
  manualFilter.toggleHidden(0);
  QCOMPARE(manualFilter.pickRandomGame(), -1);
  manualFilter.setMode(LibraryFilterModel::Mode::Hidden);
  QCOMPARE(manualFilter.pickRandomGame(), 0); // Explicit hidden filter is respected.
  QVERIFY(QFile::remove(executable));
  QCOMPARE(manualFilter.pickRandomGame(), -1);
}

void CoreTests::customCoverPersistsAndResets() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString database = directory.path() + QStringLiteral("/omakade.sqlite3");
  const QString source = directory.path() + QStringLiteral("/cover.png");
  QImage image(20, 30, QImage::Format_RGB32);
  image.fill(Qt::red);
  QVERIFY(image.save(source));

  {
    MockGameModel demo(nullptr, 1);
    UnifiedGameModel games(database);
    games.addSourceModel(&demo);
    LibraryFilterModel library;
    library.setSourceModel(&games);
    QVERIFY(library.setCustomCover(0, QUrl::fromLocalFile(source)));
    const QVariantMap game = library.get(0);
    QVERIFY(game.value(QStringLiteral("customCover")).toBool());
    QVERIFY(
        QFileInfo(QUrl(game.value(QStringLiteral("coverPath")).toString()).toLocalFile()).isFile());
  }

  MockGameModel demo(nullptr, 1);
  UnifiedGameModel games(database);
  games.addSourceModel(&demo);
  LibraryFilterModel library;
  library.setSourceModel(&games);
  QVERIFY(library.get(0).value(QStringLiteral("customCover")).toBool());
  QVERIFY(library.resetCustomCover(0));
  QVERIFY(!library.get(0).value(QStringLiteral("customCover")).toBool());
  writeFile(directory.path() + QStringLiteral("/invalid.png"), "not an image");
  QVERIFY(!library.setCustomCover(
      0, QUrl::fromLocalFile(directory.path() + QStringLiteral("/invalid.png"))));
}

void CoreTests::explicitLinksPersistAndPreserveInstallations() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString dataRoot = directory.path() + QStringLiteral("/lutris");
  const QString database = directory.path() + QStringLiteral("/omakade.sqlite3");
  createLutrisFixture(dataRoot);
  const QString installedPath = directory.path() + QStringLiteral("/installed");
  QVERIFY(QDir().mkpath(installedPath));
  const QString fixtureConnection = QStringLiteral("preference-fixture");
  {
    QSqlDatabase fixture = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), fixtureConnection);
    fixture.setDatabaseName(dataRoot + QStringLiteral("/pga.db"));
    QVERIFY(fixture.open());
    QSqlQuery update(fixture);
    update.prepare(QStringLiteral("UPDATE games SET directory = ? WHERE id = 7"));
    update.addBindValue(installedPath);
    QVERIFY(update.exec());
  }
  QSqlDatabase::removeDatabase(fixtureConnection);

  {
    MockGameModel demo(nullptr, 2);
    LutrisGameModel lutris(database);
    lutris.refreshFromDatabases({dataRoot + QStringLiteral("/pga.db")});
    UnifiedGameModel games(database);
    games.addSourceModel(&demo);
    games.addSourceModel(&lutris);
    QCOMPARE(games.rowCount(), 3);
    QCOMPARE(games.linkCandidates(0, QStringLiteral("Signal")).size(), 1);
    QVERIFY(games.linkGames(0, QStringLiteral("Lutris"), QString{}, QStringLiteral("7")));
    QCOMPARE(games.rowCount(), 2);
    QVERIFY(games.data(games.index(0), GameRoles::Linked).toBool());
    QCOMPARE(games.data(games.index(0), GameRoles::LinkedSources).toString(),
             QStringLiteral("Demo + Lutris"));
    const QVariantList installations = games.installations(0);
    QCOMPARE(installations.size(), 2);
    QCOMPARE(installations.at(0).toMap().value(QStringLiteral("source")).toString(),
             QStringLiteral("Demo"));
    QCOMPARE(installations.at(1).toMap().value(QStringLiteral("source")).toString(),
             QStringLiteral("Lutris"));
    QCOMPARE(installations.at(1).toMap().value(QStringLiteral("appId")).toString(),
             QStringLiteral("7"));
    QVERIFY(!games.setPreferredInstallation(0, QStringLiteral("Lutris"), QString{},
                                            QStringLiteral("missing")));
    LibraryFilterModel preferenceView;
    preferenceView.setSourceModel(&games);
    preferenceView.setSourceFilter(QStringLiteral("Lutris"));
    QCOMPARE(preferenceView.rowCount(), 1);
    QVERIFY(preferenceView.setPreferredInstallation(0, QStringLiteral("Lutris"), QString{},
                                                    QStringLiteral("7")));
    QVERIFY(games.installations(0).at(1).toMap().value(QStringLiteral("preferred")).toBool());
    QCOMPARE(games.preferredInstallation(0).value(QStringLiteral("source")).toString(),
             QStringLiteral("Lutris"));
    QVERIFY(QDir().rmdir(installedPath));
    QVERIFY(games.installations(0)
                .at(0)
                .toMap()
                .value(QStringLiteral("preferredUnavailable"))
                .toBool());
    QVERIFY(QDir().mkpath(installedPath));
    QCOMPARE(games.preferredInstallation(0).value(QStringLiteral("source")).toString(),
             QStringLiteral("Lutris"));
    games.setSourceEnabled(QStringLiteral("Lutris"), false);
    QVERIFY(games.installations(0)
                .at(0)
                .toMap()
                .value(QStringLiteral("preferredUnavailable"))
                .toBool());
    games.setSourceEnabled(QStringLiteral("Lutris"), true);
    QVERIFY(games.installations(0).at(1).toMap().value(QStringLiteral("preferred")).toBool());
    QVERIFY(!games.installations(0)
                 .at(1)
                 .toMap()
                 .value(QStringLiteral("preferredUnavailable"))
                 .toBool());
    games.setSourceEnabled(QStringLiteral("Demo"), false);
    QCOMPARE(games.rowCount(), 1);
    QCOMPARE(games.data(games.index(0), GameRoles::Source).toString(), QStringLiteral("Lutris"));
    QCOMPARE(games.installations(0).size(), 1);
    games.setSourceEnabled(QStringLiteral("Demo"), true);
    QCOMPARE(games.rowCount(), 2);
    QVERIFY(games.setCompletionStatus(0, QStringLiteral("completed")));
    QVERIFY(games.setTags(0, QStringLiteral("cross-platform")));
    QVERIFY(games.createCollection(QStringLiteral("Finished")));
    QVERIFY(games.setCollectionMembership(0, QStringLiteral("Finished"), true));
    for (const QVariant& installation : games.installations(0)) {
      QCOMPARE(installation.toMap().value(QStringLiteral("completionStatus")).toString(),
               QStringLiteral("completed"));
      QCOMPARE(installation.toMap().value(QStringLiteral("tags")).toStringList(),
               QStringList({QStringLiteral("cross-platform")}));
      QCOMPARE(installation.toMap().value(QStringLiteral("collections")).toStringList(),
               QStringList({QStringLiteral("Finished")}));
    }
    games.toggleFavorite(0);
    QVERIFY(!demo.data(demo.index(0), GameRoles::Favorite).toBool());

    LibraryFilterModel library;
    library.setSourceModel(&games);
    library.setSourceFilter(QStringLiteral("Lutris"));
    QCOMPARE(library.rowCount(), 1);
    QVERIFY(library.get(0).value(QStringLiteral("linked")).toBool());
  }

  MockGameModel demo(nullptr, 2);
  LutrisGameModel lutris(database);
  lutris.refreshFromDatabases({dataRoot + QStringLiteral("/pga.db")});
  UnifiedGameModel games(database);
  games.addSourceModel(&demo);
  games.addSourceModel(&lutris);
  QCOMPARE(games.rowCount(), 2);
  QCOMPARE(games.installations(0).size(), 2);
  QCOMPARE(games.data(games.index(0), GameRoles::CompletionStatus).toString(),
           QStringLiteral("completed"));
  QCOMPARE(games.data(games.index(0), GameRoles::Collections).toStringList(),
           QStringList({QStringLiteral("Finished")}));
  QVERIFY(games.installations(0).at(1).toMap().value(QStringLiteral("preferred")).toBool());
  // Explicit CLI requests still select their named source, regardless of the default.
  QCOMPARE(
      PlayRequest::findInstallation(games, LaunchKey::parse(QStringLiteral("Demo::demo-0")), nullptr)
          .value(QStringLiteral("source")).toString(), QStringLiteral("Demo"));
  QCOMPARE(
      PlayRequest::findInstallation(games, LaunchKey::parse(QStringLiteral("Lutris::7")), nullptr)
          .value(QStringLiteral("source"))
          .toString(),
      QStringLiteral("Lutris"));
  QVERIFY(games.unlinkGames(0));
  QCOMPARE(games.rowCount(), 3);
  QVERIFY(games.linkGames(0, QStringLiteral("Lutris"), QString{}, QStringLiteral("7")));
  for (const QVariant& value : games.installations(0)) {
    QVERIFY(!value.toMap().value(QStringLiteral("preferred")).toBool());
  }
  QVERIFY(
      games.setPreferredInstallation(0, QStringLiteral("Lutris"), QString{}, QStringLiteral("7")));
  // Merging another game into the group retains the group's existing default.
  const QVariantMap third = games.installations(1).first().toMap();
  QVERIFY(games.linkGames(0, third.value(QStringLiteral("source")).toString(),
                          third.value(QStringLiteral("runner")).toString(),
                          third.value(QStringLiteral("appId")).toString()));
  QCOMPARE(games.rowCount(), 1);
  int defaults = 0;
  for (const QVariant& value : games.installations(0)) {
    if (value.toMap().value(QStringLiteral("preferred")).toBool()) {
      ++defaults;
      QCOMPARE(value.toMap().value(QStringLiteral("source")).toString(), QStringLiteral("Lutris"));
    }
  }
  QCOMPARE(defaults, 1);
}

void CoreTests::launchActivityPersistsAndSortsExactly() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString database = directory.path() + QStringLiteral("/omakade.sqlite3");

  {
    MockGameModel demo(nullptr, 20);
    UnifiedGameModel games(database);
    games.addSourceModel(&demo);
    LibraryFilterModel library;
    library.setSourceModel(&games);
    library.setMode(LibraryFilterModel::Mode::Recent);
    QCOMPARE(library.rowCount(), 9);

    library.setMode(LibraryFilterModel::Mode::All);
    int launchRow = -1;
    for (int row = 0; row < library.rowCount(); ++row) {
      if (library.get(row).value(QStringLiteral("appId")) == QStringLiteral("demo-10")) {
        launchRow = row;
        break;
      }
    }
    QVERIFY(launchRow >= 0);
    QVERIFY(library.recordLaunch(launchRow, QStringLiteral("Demo"), QString{},
                                 QStringLiteral("demo-10")));
    QVERIFY(!library.recordLaunch(launchRow, QStringLiteral("Steam"), QString{},
                                  QStringLiteral("10")));
    library.setMode(LibraryFilterModel::Mode::Recent);
    QCOMPARE(library.rowCount(), 10);
    library.setSortMode(LibraryFilterModel::SortMode::RecentlyPlayed);
    QCOMPARE(library.get(0).value(QStringLiteral("appId")).toString(),
             QStringLiteral("demo-10"));
  }

  MockGameModel demo(nullptr, 20);
  UnifiedGameModel games(database);
  games.addSourceModel(&demo);
  LibraryFilterModel library;
  library.setSourceModel(&games);
  library.setMode(LibraryFilterModel::Mode::Recent);
  QCOMPARE(library.rowCount(), 10);
  library.setSortMode(LibraryFilterModel::SortMode::RecentlyPlayed);
  QCOMPARE(library.get(0).value(QStringLiteral("appId")).toString(),
           QStringLiteral("demo-10"));
}

void CoreTests::organizationPersistsAndFilters() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString database = directory.path() + QStringLiteral("/omakade.sqlite3");

  {
    MockGameModel demo(nullptr, 20);
    UnifiedGameModel games(database);
    games.addSourceModel(&demo);
    LibraryFilterModel library;
    library.setSourceModel(&games);
    int gameRow = -1;
    for (int row = 0; row < library.rowCount(); ++row) {
      if (library.get(row).value(QStringLiteral("appId")) == QStringLiteral("demo-10")) {
        gameRow = row;
        break;
      }
    }
    QVERIFY(gameRow >= 0);
    QVERIFY(!library.setCompletionStatus(gameRow, QStringLiteral("finished-ish")));
    QVERIFY(library.setCompletionStatus(gameRow, QStringLiteral("Playing")));
    QVERIFY(library.setTags(gameRow, QStringLiteral("Co-op, RPG, co-OP, Long game")));
    QVERIFY(library.createCollection(QStringLiteral("Weekend")));
    QVERIFY(!library.createCollection(QStringLiteral("weekend")));
    QVERIFY(library.setCollectionMembership(gameRow, QStringLiteral("Weekend"), true));

    const QVariantMap game = library.get(gameRow);
    QCOMPARE(game.value(QStringLiteral("completionStatus")).toString(),
             QStringLiteral("playing"));
    QCOMPARE(game.value(QStringLiteral("tags")).toStringList(),
             QStringList({QStringLiteral("Co-op"), QStringLiteral("Long game"),
                          QStringLiteral("RPG")}));
    QCOMPARE(game.value(QStringLiteral("collections")).toStringList(),
             QStringList({QStringLiteral("Weekend")}));
    QCOMPARE(library.collectionNames(), QStringList({QStringLiteral("Weekend")}));
    QCOMPARE(library.tagNames(),
             QStringList({QStringLiteral("Co-op"), QStringLiteral("Long game"),
                          QStringLiteral("RPG")}));

    library.setCompletionFilter(QStringLiteral("playing"));
    QCOMPARE(library.rowCount(), 1);
    library.setCompletionFilter({});
    library.setCollectionFilter(QStringLiteral("Weekend"));
    QCOMPARE(library.rowCount(), 1);
    library.setCollectionFilter({});
    library.setTagFilter(QStringLiteral("rpg"));
    QCOMPARE(library.rowCount(), 1);
    library.setSearchText(QStringLiteral("Long game"));
    QCOMPARE(library.rowCount(), 1);
  }

  MockGameModel demo(nullptr, 20);
  UnifiedGameModel games(database);
  games.addSourceModel(&demo);
  LibraryFilterModel library;
  library.setSourceModel(&games);
  library.setCompletionFilter(QStringLiteral("playing"));
  QCOMPARE(library.rowCount(), 1);
  QCOMPARE(library.get(0).value(QStringLiteral("appId")).toString(),
           QStringLiteral("demo-10"));
  QCOMPARE(library.get(0).value(QStringLiteral("collections")).toStringList(),
           QStringList({QStringLiteral("Weekend")}));
  QVERIFY(library.deleteCollection(QStringLiteral("weekend")));
  QVERIFY(library.collectionNames().isEmpty());
  QVERIFY(library.get(0).value(QStringLiteral("collections")).toStringList().isEmpty());
}

void CoreTests::lutrisLauncherBuildsSafeCommands() {
  const LaunchCommand native = GameLauncher::lutrisCommand(QStringLiteral("42"), false);
  QCOMPARE(native.program, QStringLiteral("lutris"));
  QCOMPARE(native.arguments, QStringList{QStringLiteral("lutris:rungameid/42")});
  const LaunchCommand flatpak = GameLauncher::lutrisCommand(QStringLiteral("42"), true);
  QCOMPARE(flatpak.program, QStringLiteral("flatpak"));
  QCOMPARE(flatpak.arguments,
           QStringList({QStringLiteral("run"), QStringLiteral("net.lutris.Lutris"),
                        QStringLiteral("lutris:rungameid/42")}));
  QVERIFY(!GameLauncher::lutrisCommand(QStringLiteral("42;touch /tmp/nope"), false).isValid());
}

void CoreTests::heroicScannerImportsEpicGogAndAmazon() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/heroic");
  createHeroicFixture(root);

  const HeroicScanResult result = HeroicScanner::scan({root});
  QVERIFY(!result.incomplete);
  QCOMPARE(result.games.size(), 4);
  QCOMPARE(result.games.at(0).runner, QStringLiteral("legendary"));
  QCOMPARE(result.games.at(0).title, QStringLiteral("Epic Voyage"));
  QVERIFY(result.games.at(0).coverPath.endsWith(QStringLiteral("EpicApp.jpg")));
  QCOMPARE(result.games.at(0).playtimeMinutes, 125);
  QVERIFY(result.games.at(0).lastPlayed > 0);
  QCOMPARE(result.games.at(1).runner, QStringLiteral("gog"));
  QCOMPARE(result.games.at(1).title, QStringLiteral("GOG Quest"));
  QCOMPARE(result.games.at(1).playtimeMinutes, 0);
  const std::optional<GogLaunchTask> launchTask =
      HeroicScanner::gogLaunchTask(result.games.at(1).installPath, result.games.at(1).appId);
  QVERIFY(launchTask.has_value());
  QVERIFY(launchTask->executablePath.endsWith(QStringLiteral("/gog-game/start.sh")));
  QCOMPARE(launchTask->arguments,
           QStringList({QStringLiteral("--profile"), QStringLiteral("couch mode")}));
  QCOMPARE(result.games.at(2).runner, QStringLiteral("nile"));
  QCOMPARE(result.games.at(2).title, QStringLiteral("Amazon Trail"));
  // Sideloaded games come from sideload_apps/library.json; uninstalled entries stay out.
  QCOMPARE(result.games.at(3).runner, QStringLiteral("sideload"));
  QCOMPARE(result.games.at(3).appId, QStringLiteral("j661Z9rpxqYRZSp45Jh92i"));
  QCOMPARE(result.games.at(3).title, QStringLiteral("Scott Pilgrim EX"));
  QCOMPARE(result.games.at(3).installPath, QStringLiteral("/home/user/Games/Scott"));
  QVERIFY(result.games.at(3).coverPath.contains(QStringLiteral("/images-cache/")));

  QTemporaryDir sideloadOnly;
  QVERIFY(sideloadOnly.isValid());
  const QString sideloadRoot = sideloadOnly.path() + QStringLiteral("/heroic");
  writeFile(
      sideloadRoot + QStringLiteral("/sideload_apps/library.json"),
      R"({"games":[{"runner":"sideload","app_name":"onlyGame","title":"Only Game","install":{"executable":"/games/only/game.exe","is_dlc":false},"folder_name":"","is_installed":true}]})");
  const HeroicScanResult sideload = HeroicScanner::scan({sideloadRoot});
  QCOMPARE(sideload.roots, QStringList({sideloadRoot}));
  QCOMPARE(sideload.games.size(), 1);
  QCOMPARE(sideload.games.at(0).installPath, QStringLiteral("/games/only"));
}

void CoreTests::gogScannerImportsLooseInstallsAndConfinesLaunchTasks() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/GOG Games");
  const QString game = root + QStringLiteral("/Signal Hill");
  writeFile(game + QStringLiteral("/goggame-98765.info"),
            R"({"name":"Signal Hill","playTasks":[{"type":"FileTask","isPrimary":true,"path":"bin\\game.exe","workingDir":"bin","arguments":"--safe \"two words\""}]})");
  writeFile(game + QStringLiteral("/bin/game.exe"), "game");

  const HeroicScanResult result = HeroicScanner::scan({root});
  QVERIFY(!result.incomplete);
  QCOMPARE(result.roots, QStringList({root}));
  QCOMPARE(result.games.size(), 1);
  QCOMPARE(result.games.at(0).appId, QStringLiteral("98765"));
  QCOMPARE(result.games.at(0).runner, QStringLiteral("gog-direct"));
  QCOMPARE(result.games.at(0).title, QStringLiteral("Signal Hill"));

  const QString outside = directory.path() + QStringLiteral("/outside.exe");
  writeFile(outside, "outside");
  QVERIFY(QFile::link(outside, game + QStringLiteral("/bin/linked.exe")));
  writeFile(game + QStringLiteral("/goggame-98765.info"),
            R"({"name":"Signal Hill","playTasks":[{"type":"FileTask","isPrimary":true,"path":"bin/linked.exe"}]})");
  QVERIFY(!HeroicScanner::gogLaunchTask(game, QStringLiteral("98765")).has_value());

  writeFile(game + QStringLiteral("/goggame-98765.info"),
            R"({"name":"Signal Hill","playTasks":[{"type":"FileTask","isPrimary":true,"path":"../escape.exe"}]})");
  QVERIFY(!HeroicScanner::gogLaunchTask(game, QStringLiteral("98765")).has_value());
  QVERIFY(!HeroicScanner::gogLaunchTask(game, QStringLiteral("98765;touch")).has_value());
}

void CoreTests::heroicModelIsRepeatableAndPreservesLocalState() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/heroic");
  createHeroicFixture(root);
  HeroicGameModel model(directory.path() + QStringLiteral("/omakade.sqlite3"));
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 4);
  QCOMPARE(model.detectedPaths(), QStringList({root}));
  QVERIFY(model.lastScan() > 0);
  QVERIFY(model.heroicDetected());
  QVERIFY(!model.gogDetected());
  QCOMPARE(model.data(model.index(2), GameRoles::Source).toString(), QStringLiteral("Heroic"));
  QCOMPARE(model.data(model.index(1), GameRoles::AppId).toString(), QStringLiteral("EpicApp"));
  QCOMPARE(model.data(model.index(1), GameRoles::Hours).toInt(), 2);
  QVERIFY(model.data(model.index(1), GameRoles::Recent).toBool());
  QVERIFY(!model.data(model.index(0), GameRoles::Recent).toBool());
  model.toggleFavorite(0);
  model.toggleHidden(0);
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 4);
  QVERIFY(model.data(model.index(0), GameRoles::Favorite).toBool());
  QVERIFY(model.data(model.index(0), GameRoles::Hidden).toBool());
  HeroicGameModel reloaded(directory.path() + QStringLiteral("/omakade.sqlite3"));
  QCOMPARE(reloaded.detectedPaths(), QStringList({root}));
  QCOMPARE(reloaded.lastScan(), model.lastScan());
}

void CoreTests::malformedHeroicDataDoesNotReplaceCachedGames() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/heroic");
  createHeroicFixture(root);
  HeroicGameModel model(directory.path() + QStringLiteral("/omakade.sqlite3"));
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 4);
  writeFile(root + QStringLiteral("/legendaryConfig/legendary/installed.json"), "not json");
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 4);
  QVERIFY(model.statusText().contains(QStringLiteral("kept cached results")));
}

void CoreTests::heroicAndGogScanFailuresAreIsolated() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString heroicRoot = directory.path() + QStringLiteral("/heroic");
  const QString gogRoot = directory.path() + QStringLiteral("/GOG Games");
  const QString directGame = gogRoot + QStringLiteral("/Direct Quest");
  createHeroicFixture(heroicRoot);
  writeFile(directGame + QStringLiteral("/goggame-98765.info"),
            R"({"name":"Direct Quest","playTasks":[{"type":"FileTask","isPrimary":true,"path":"start.sh"}]})");
  writeFile(directGame + QStringLiteral("/start.sh"), "#!/bin/sh\n");

  HeroicGameModel model(directory.path() + QStringLiteral("/omakade.sqlite3"));
  model.refreshFromRoots({heroicRoot, gogRoot});
  QCOMPARE(model.rowCount(), 5);
  QVERIFY(model.heroicDetected());
  QVERIFY(model.gogDetected());

  writeFile(heroicRoot + QStringLiteral("/legendaryConfig/legendary/installed.json"),
            "not json");
  model.refreshFromRoots({heroicRoot, gogRoot});
  QCOMPARE(model.rowCount(), 5);
  QVERIFY(model.statusText().contains(QStringLiteral("kept cached results")));

  createHeroicFixture(heroicRoot);
  writeFile(heroicRoot + QStringLiteral("/gog_store/installed.json"), "not json");
  model.refreshFromRoots({heroicRoot, gogRoot});
  QCOMPARE(model.rowCount(), 5);
  QVERIFY(model.statusText().contains(QStringLiteral("kept cached results")));

  createHeroicFixture(heroicRoot);
  writeFile(directGame + QStringLiteral("/goggame-98765.info"), "not json");
  model.refreshFromRoots({heroicRoot, gogRoot});
  QCOMPARE(model.rowCount(), 5);
  QVERIFY(model.statusText().contains(QStringLiteral("kept cached results")));
}

void CoreTests::coldManagedGogFailureDoesNotImportDirectGames() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/heroic");
  createHeroicFixture(root);
  writeFile(root + QStringLiteral("/gog_store/installed.json"), "not json");
  const HeroicScanResult result = HeroicScanner::scan({root});
  QVERIFY(result.managedGogIncomplete);
  for (const HeroicGameRecord& game : result.games) {
    QVERIFY(game.runner != QStringLiteral("gog-direct"));
  }
  HeroicGameModel model(directory.path() + QStringLiteral("/library.sqlite3"));
  model.refreshFromRoots({root});
  QVERIFY(!model.gogDetected());
  createHeroicFixture(root);
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 4);
  QVERIFY(model.heroicDetected());
  QVERIFY(!model.gogDetected());
}

void CoreTests::removingLastDirectGogGameClearsCache() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/GOG Games");
  const QString game = root + QStringLiteral("/Direct Quest");
  writeFile(game + QStringLiteral("/goggame-98765.info"),
            R"({"name":"Direct Quest","playTasks":[{"type":"FileTask","isPrimary":true,"path":"start.sh"}]})");
  writeFile(game + QStringLiteral("/start.sh"), "#!/bin/sh\n");
  const QString database = directory.path() + QStringLiteral("/library.sqlite3");
  HeroicGameModel model(database);
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 1);
  model.refreshFromRoots({directory.path() + QStringLiteral("/missing")});
  QCOMPARE(model.rowCount(), 1);
  QVERIFY(QFile::remove(game + QStringLiteral("/goggame-98765.info")));
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 0);
  QVERIFY(!model.gogDetected());
  HeroicGameModel reloaded(database);
  QCOMPARE(reloaded.rowCount(), 0);
}

void CoreTests::heroicLauncherBuildsSafeCommands() {
  const LaunchCommand native =
      GameLauncher::heroicCommand(QStringLiteral("EpicApp"), QStringLiteral("legendary"), false);
  QCOMPARE(native.program, QStringLiteral("heroic"));
  QCOMPARE(native.arguments.constFirst(), QStringLiteral("--no-gui"));
  QCOMPARE(native.arguments.constLast(),
           QStringLiteral("heroic://launch?appName=EpicApp&runner=legendary&gui=false"));
  const LaunchCommand flatpak =
      GameLauncher::heroicCommand(QStringLiteral("12345"), QStringLiteral("gog"), true);
  QCOMPARE(flatpak.program, QStringLiteral("flatpak"));
  QCOMPARE(flatpak.arguments.at(1), QStringLiteral("com.heroicgameslauncher.hgl"));
  QVERIFY(!GameLauncher::heroicCommand(QStringLiteral("bad;id"), QStringLiteral("gog"), false)
               .isValid());
  QVERIFY(!GameLauncher::heroicCommand(QStringLiteral("good"), QStringLiteral("unknown"), false)
               .isValid());
  const LaunchCommand sideload = GameLauncher::heroicCommand(
      QStringLiteral("j661Z9rpxqYRZSp45Jh92i"), QStringLiteral("sideload"), false);
  QVERIFY(sideload.isValid());
  QVERIFY(sideload.arguments.constLast().contains(QStringLiteral("runner=sideload")));
}

void CoreTests::gogLauncherBuildsSafeCommands() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString nativeGame = directory.path() + QStringLiteral("/native");
  writeFile(nativeGame + QStringLiteral("/goggame-123.info"),
            R"({"playTasks":[{"type":"FileTask","isPrimary":true,"path":"start.sh","arguments":"--profile \"living room\""}]})");
  writeFile(nativeGame + QStringLiteral("/start.sh"), "#!/bin/sh\n");
  const LaunchCommand native =
      GameLauncher::gogCommand(QStringLiteral("123"), nativeGame);
  QVERIFY(native.program.endsWith(QStringLiteral("/native/start.sh")));
  QCOMPARE(native.arguments,
           QStringList({QStringLiteral("--profile"), QStringLiteral("living room")}));

  const QString windowsGame = directory.path() + QStringLiteral("/windows");
  writeFile(windowsGame + QStringLiteral("/goggame-456.info"),
            R"({"playTasks":[{"type":"FileTask","isPrimary":true,"path":"game.exe","arguments":"-windowed"}]})");
  writeFile(windowsGame + QStringLiteral("/game.exe"), "game");
  const QString prefix = directory.path() + QStringLiteral("/prefix");
  const LaunchCommand windows =
      GameLauncher::gogCommand(QStringLiteral("456"), windowsGame, prefix);
  QCOMPARE(windows.program, QStringLiteral("env"));
  QCOMPARE(windows.arguments.at(0), QStringLiteral("WINEPREFIX=%1").arg(prefix));
  QCOMPARE(windows.arguments.at(1), QStringLiteral("GAMEID=umu-default"));
  QCOMPARE(windows.arguments.at(2), QStringLiteral("STORE=gog"));
  QCOMPARE(windows.arguments.at(3), QStringLiteral("umu-run"));
  QVERIFY(windows.arguments.at(4).endsWith(QStringLiteral("/windows/game.exe")));
  QCOMPARE(windows.arguments.at(5), QStringLiteral("-windowed"));
  QVERIFY(!GameLauncher::gogCommand(QStringLiteral("456;touch"), windowsGame).isValid());
}

void CoreTests::faugusScannerImportsLaunchableGamesAndArtwork() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString nativeRoot = directory.path() + QStringLiteral("/faugus-launcher");
  const QString flatpakRoot =
      directory.path() +
      QStringLiteral("/.var/app/io.github.Faugus.faugus-launcher/data/faugus-launcher");
  createFaugusFixture(nativeRoot);
  createFaugusFixture(flatpakRoot);

  const FaugusScanResult result = FaugusScanner::scan({nativeRoot, flatpakRoot});
  QVERIFY(!result.incomplete);
  QCOMPARE(result.roots, QStringList({nativeRoot, flatpakRoot}));
  QCOMPARE(result.games.size(), 3);
  QCOMPARE(result.games.at(0).gameId, QStringLiteral("signal-hill"));
  QCOMPARE(result.games.at(0).title, QStringLiteral("Signal Hill"));
  QCOMPARE(result.games.at(0).runner, QStringLiteral("GE-Proton"));
  QCOMPARE(result.games.at(0).playtimeSeconds, 7200);
  QVERIFY(result.games.at(0).executablePath.startsWith(QDir::homePath()));
  QVERIFY(result.games.at(0).coverPath.endsWith(QStringLiteral("signal-hill.png")));
  QVERIFY(result.games.at(0).heroPath.endsWith(QStringLiteral("signal-hill.png")));
  QVERIFY(!result.games.at(0).flatpak);
  QVERIFY(result.games.at(1).coverPath.endsWith(QStringLiteral("linux-tool.png")));
  QCOMPARE(result.games.at(2).gameId, QStringLiteral("pokémon-外伝"));
}

void CoreTests::faugusModelIsRepeatableAndPreservesLocalState() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/faugus-launcher");
  const QString database = directory.path() + QStringLiteral("/omakade.sqlite3");
  createFaugusFixture(root);

  FaugusGameModel model(database);
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 3);
  QCOMPARE(model.detectedPaths(), QStringList({root}));
  QVERIFY(model.lastScan() > 0);
  QCOMPARE(model.data(model.index(0), GameRoles::Source).toString(), QStringLiteral("Faugus"));
  QCOMPARE(model.data(model.index(2), GameRoles::Hours).toInt(), 2);
  model.toggleFavorite(0);
  model.toggleHidden(0);
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 3);
  QVERIFY(model.data(model.index(0), GameRoles::Favorite).toBool());
  QVERIFY(model.data(model.index(0), GameRoles::Hidden).toBool());
  writeFile(root + QStringLiteral("/games.json"), "[]");
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 0);
  createFaugusFixture(root);
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 3);
  QVERIFY(model.data(model.index(0), GameRoles::Favorite).toBool());
  QVERIFY(model.data(model.index(0), GameRoles::Hidden).toBool());

  FaugusGameModel reloaded(database);
  QCOMPARE(reloaded.detectedPaths(), QStringList({root}));
  QCOMPARE(reloaded.lastScan(), model.lastScan());
}

void CoreTests::malformedFaugusDataDoesNotReplaceCachedGames() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/faugus-launcher");
  createFaugusFixture(root);
  FaugusGameModel model(directory.path() + QStringLiteral("/omakade.sqlite3"));
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 3);
  writeFile(root + QStringLiteral("/games.json"), "not json");
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 3);
  QVERIFY(model.statusText().startsWith(QStringLiteral("Faugus scan interrupted")));
}

void CoreTests::faugusLauncherBuildsSafeCommands() {
  const LaunchCommand native = GameLauncher::faugusCommand(QStringLiteral("signal-hill"), false);
  QCOMPARE(native.program, QStringLiteral("faugus-launcher"));
  QCOMPARE(native.arguments,
           QStringList({QStringLiteral("--game"), QStringLiteral("signal-hill")}));
  const LaunchCommand flatpak = GameLauncher::faugusCommand(QStringLiteral("signal-hill"), true);
  QCOMPARE(flatpak.program, QStringLiteral("flatpak"));
  QCOMPARE(flatpak.arguments,
           QStringList({QStringLiteral("run"),
                        QStringLiteral("--command=/app/bin/faugus-launcher"),
                        QStringLiteral("io.github.Faugus.faugus-launcher"),
                        QStringLiteral("--game"), QStringLiteral("signal-hill")}));
  QVERIFY(!GameLauncher::faugusCommand(QStringLiteral("bad;id"), false).isValid());
  QVERIFY(GameLauncher::faugusCommand(QStringLiteral("pokémon-外伝"), false).isValid());
  QVERIFY(!GameLauncher::faugusCommand(QStringLiteral("../escape"), false).isValid());
}

void CoreTests::launcherRefreshesRunAsynchronously() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const bool dataHomeWasSet = qEnvironmentVariableIsSet("XDG_DATA_HOME");
  const bool configHomeWasSet = qEnvironmentVariableIsSet("XDG_CONFIG_HOME");
  const QByteArray previousDataHome = qgetenv("XDG_DATA_HOME");
  const QByteArray previousConfigHome = qgetenv("XDG_CONFIG_HOME");
  const auto restoreEnvironment = qScopeGuard([&] {
    if (dataHomeWasSet) {
      qputenv("XDG_DATA_HOME", previousDataHome);
    } else {
      qunsetenv("XDG_DATA_HOME");
    }
    if (configHomeWasSet) {
      qputenv("XDG_CONFIG_HOME", previousConfigHome);
    } else {
      qunsetenv("XDG_CONFIG_HOME");
    }
  });
  const QString dataHome = directory.path() + QStringLiteral("/data");
  const QString configHome = directory.path() + QStringLiteral("/config");
  qputenv("XDG_DATA_HOME", dataHome.toUtf8());
  qputenv("XDG_CONFIG_HOME", configHome.toUtf8());
  const auto restoreCacheHome = redirectCacheHome(directory.path() + QStringLiteral("/cache"));
  Q_UNUSED(restoreCacheHome);
  createLutrisFixture(dataHome + QStringLiteral("/lutris"));
  createHeroicFixture(configHome + QStringLiteral("/heroic"));
  createFaugusFixture(dataHome + QStringLiteral("/faugus-launcher"));
  createBattleNetFixture(directory.path() + QStringLiteral("/home/.wine"));
  const bool homeWasSet = qEnvironmentVariableIsSet("HOME");
  const QByteArray previousHome = qgetenv("HOME");
  qputenv("HOME", QByteArray(directory.path().toUtf8() + "/home"));
  const auto restoreHome = qScopeGuard([&] {
    if (homeWasSet) {
      qputenv("HOME", previousHome);
    } else {
      qunsetenv("HOME");
    }
  });

  const QString database = directory.path() + QStringLiteral("/omakade.sqlite3");
  LutrisGameModel lutris(database);
  HeroicGameModel heroic(database);
  FaugusGameModel faugus(database);
  BattleNetGameModel battlenet(database);
  lutris.refresh();
  heroic.refresh();
  faugus.refresh();
  battlenet.refresh();
  QCOMPARE(lutris.statusText(), QStringLiteral("Scanning Lutris library"));
  QCOMPARE(heroic.statusText(), QStringLiteral("Scanning Heroic and GOG libraries"));
  QCOMPARE(faugus.statusText(), QStringLiteral("Scanning Faugus library"));
  QCOMPARE(battlenet.statusText(), QStringLiteral("Scanning Battle.net library"));
  QVERIFY(battlenet.scanning());
  QTRY_COMPARE_WITH_TIMEOUT(lutris.rowCount(), 1, 3000);
  QTRY_COMPARE_WITH_TIMEOUT(heroic.rowCount(), 4, 3000);
  QTRY_COMPARE_WITH_TIMEOUT(faugus.rowCount(), 3, 3000);
  QTRY_COMPARE_WITH_TIMEOUT(battlenet.rowCount(), 3, 3000);
  QTRY_VERIFY_WITH_TIMEOUT(!battlenet.scanning(), 3000);
}

void CoreTests::absentLaunchersPersistEmptySourcePaths() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString database = directory.path() + QStringLiteral("/omakade.sqlite3");
  {
    SteamGameModel model(database);
    model.refreshFromRoots({});
    QVERIFY(model.errorText().isEmpty());
  }
  {
    LutrisGameModel model(database);
    model.refreshFromDatabases({});
    QVERIFY(model.errorText().isEmpty());
  }
  {
    HeroicGameModel model(database);
    model.refreshFromRoots({});
    QVERIFY(model.errorText().isEmpty());
  }
  {
    FaugusGameModel model(database);
    model.refreshFromRoots({});
    QVERIFY(model.errorText().isEmpty());
  }
  {
    RetroArchGameModel model(database);
    model.refreshFromRoots({});
    QVERIFY(model.errorText().isEmpty());
  }
  {
    Pcsx2GameModel model(database);
    model.refreshFromRoots({});
    QVERIFY(model.errorText().isEmpty());
  }
  {
    RyujinxGameModel model(database);
    model.refreshFromRoots({});
    QVERIFY(model.errorText().isEmpty());
  }
  {
    BattleNetGameModel model(database);
    model.refreshFromPrefixes({});
    QVERIFY(model.errorText().isEmpty());
  }

  const QString connection = QStringLiteral("empty-source-paths-") + QUuid::createUuid().toString();
  {
    QSqlDatabase stored = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    stored.setDatabaseName(database);
    QVERIFY(stored.open());
    QSqlQuery query(stored);
    QVERIFY(query.exec(QStringLiteral(
        "SELECT COUNT(*) FROM source_state WHERE source IN "
        "('lutris', 'heroic', 'faugus', 'retroarch', 'pcsx2', 'ryujinx', 'battlenet') AND paths = '' AND paths "
        "IS NOT NULL")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 7);
  }
  QSqlDatabase::removeDatabase(connection);
}

void CoreTests::retroArchScannerImportsPlaylistsArtworkAndRuntime() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/retroarch");
  const QString flatpakRoot =
      directory.path() + QStringLiteral("/.var/app/org.libretro.RetroArch/config/retroarch");
  createRetroArchFixture(root);
  createRetroArchFixture(flatpakRoot);

  const RetroArchScanResult result = RetroArchScanner::scan({root, flatpakRoot});
  QVERIFY(!result.incomplete);
  QCOMPARE(result.roots, QStringList({root, flatpakRoot}));
  QCOMPARE(result.games.size(), 4);
  const auto sonic = std::find_if(result.games.cbegin(), result.games.cend(), [](const auto& game) {
    return game.title == QStringLiteral("Sonic & Tails") && !game.flatpak;
  });
  QVERIFY(sonic != result.games.cend());
  QCOMPARE(sonic->coreName, QStringLiteral("Genesis Plus GX"));
  QVERIFY(sonic->corePath.endsWith(QStringLiteral("genesis_plus_gx_libretro.so")));
  QVERIFY(sonic->coverPath.endsWith(QStringLiteral("Sonic _ Tails.png")));
  QVERIFY(sonic->heroPath.endsWith(QStringLiteral("Sonic _ Tails.png")));
  QCOMPARE(sonic->playtimeSeconds, 45296);
  QVERIFY(sonic->lastPlayed > 0);
  const auto unassigned =
      std::find_if(result.games.cbegin(), result.games.cend(),
                   [](const auto& game) { return game.title == QStringLiteral("Unassigned"); });
  QVERIFY(unassigned != result.games.cend());
  QVERIFY(unassigned->corePath.isEmpty());
  QCOMPARE(std::count_if(result.games.cbegin(), result.games.cend(),
                         [](const auto& game) { return game.flatpak; }),
           2);

  writeFile(root + QStringLiteral("/playlists/logs/Genesis Plus GX/Sonic & Tails.lrtl"),
            R"({"runtime":"9223372036854775807:00:00"})");
  const RetroArchScanResult hostileRuntime = RetroArchScanner::scan({root});
  const auto safeSonic =
      std::find_if(hostileRuntime.games.cbegin(), hostileRuntime.games.cend(),
                   [](const auto& game) { return game.title == QStringLiteral("Sonic & Tails"); });
  QVERIFY(safeSonic != hostileRuntime.games.cend());
  QCOMPARE(safeSonic->playtimeSeconds, 0);
}

void CoreTests::retroArchScannerResolvesFlatpakPathsAndCoreNames() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString appDirectory =
      directory.path() + QStringLiteral("/.var/app/org.libretro.RetroArch");
  const QString root = appDirectory + QStringLiteral("/config/retroarch");
  const QString archive = directory.path() + QStringLiteral("/roms/Sonic Pack.zip");
  const QString snes = directory.path() + QStringLiteral("/roms/Yoshi`s Island.sfc");
  writeFile(archive, "zip");
  writeFile(snes, "rom");
  // The Flatpak writes sandbox paths into its config; they only exist inside the sandbox.
  writeFile(root + QStringLiteral("/retroarch.cfg"),
            "playlist_directory = \"/var/config/retroarch/playlists\"\n"
            "thumbnails_directory = \"/var/config/retroarch/thumbnails\"\n"
            "runtime_log_directory = \"/var/config/retroarch/playlists/logs\"\n"
            "libretro_info_path = \"/var/config/retroarch/cores\"\n");
  writeFile(
      root + QStringLiteral("/cores/genesis_plus_gx_libretro.info"),
      "display_name = \"Sega - MS/GG/MD/CD (Genesis Plus GX)\"\ncorename = \"Genesis Plus GX\"\n");
  writeFile(
      root + QStringLiteral("/playlists/Sega - Mega Drive.lpl"),
      QStringLiteral(
          R"json({"version":"1.5","default_core_path":"DETECT","default_core_name":"DETECT","items":[{"path":"%1#Sonic.bin","label":"Sonic","core_path":"/var/config/retroarch/cores/genesis_plus_gx_libretro.so","core_name":"Sega - MS/GG/MD/CD (Genesis Plus GX)","db_name":"Sega - Mega Drive.lpl"}]})json")
          .arg(archive)
          .toUtf8());
  writeFile(
      root + QStringLiteral("/playlists/Nintendo - SNES.lpl"),
      QStringLiteral(
          R"json({"version":"1.5","default_core_path":"DETECT","default_core_name":"DETECT","items":[{"path":"%1","label":"Yoshi`s Island","core_path":"/var/config/retroarch/cores/snes9x_libretro.so","core_name":"Nintendo - SNES / SFC (Snes9x)","db_name":"Nintendo - SNES.lpl"}]})json")
          .arg(snes)
          .toUtf8());
  writeFile(root + QStringLiteral("/playlists/logs/Genesis Plus GX/Sonic.lrtl"),
            R"({"version":"1.0","runtime":"01:00:00","last_played":"2026-08-30 19:45:10"})");
  writeFile(root + QStringLiteral("/playlists/logs/Snes9x/Yoshi`s Island.lrtl"),
            R"({"version":"1.0","runtime":"02:00:00","last_played":"2026-08-30 19:45:10"})");
  writeFile(root + QStringLiteral("/thumbnails/Nintendo - SNES/Named_Boxarts/Yoshi_s Island.png"),
            "cover");

  QCOMPARE(RetroArchScanner::discoverRoots().contains(root), false);
  const RetroArchScanResult result = RetroArchScanner::scan({root});
  QVERIFY(!result.incomplete);
  QCOMPARE(result.roots, QStringList({root}));
  QCOMPARE(result.games.size(), 2);
  const auto sonic = std::find_if(result.games.cbegin(), result.games.cend(), [](const auto& game) {
    return game.title == QStringLiteral("Sonic");
  });
  QVERIFY(sonic != result.games.cend());
  QVERIFY(sonic->flatpak);
  QCOMPARE(sonic->playtimeSeconds, 3600);
  const auto yoshi = std::find_if(result.games.cbegin(), result.games.cend(), [](const auto& game) {
    return game.title == QStringLiteral("Yoshi`s Island");
  });
  QVERIFY(yoshi != result.games.cend());
  QCOMPARE(yoshi->playtimeSeconds, 7200);
  QVERIFY(yoshi->coverPath.endsWith(QStringLiteral("Yoshi_s Island.png")));
}

void CoreTests::retroArchModelIsRepeatableAndPreservesLocalState() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/retroarch");
  const QString database = directory.path() + QStringLiteral("/omakade.sqlite3");
  createRetroArchFixture(root);
  RetroArchGameModel model(database);
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 2);
  QCOMPARE(model.detectedPaths(), QStringList({root}));
  model.toggleFavorite(0);
  model.toggleHidden(0);
  model.refreshFromRoots({root});
  QVERIFY(model.data(model.index(0), GameRoles::Favorite).toBool());
  QVERIFY(model.data(model.index(0), GameRoles::Hidden).toBool());
  RetroArchGameModel reloaded(database);
  QCOMPARE(reloaded.rowCount(), 2);
  QVERIFY(reloaded.data(reloaded.index(0), GameRoles::Favorite).toBool());
}

void CoreTests::malformedRetroArchDataDoesNotReplaceCachedGames() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/retroarch");
  createRetroArchFixture(root);
  RetroArchGameModel model(directory.path() + QStringLiteral("/omakade.sqlite3"));
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 2);
  writeFile(root + QStringLiteral("/playlists/Nintendo.lpl"), "not json");
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 2);
  QVERIFY(model.statusText().startsWith(QStringLiteral("RetroArch scan interrupted")));
}

void CoreTests::retroArchLauncherBuildsSafeCommands() {
  const QString content = QStringLiteral("/games/Sonic & Tails.bin");
  const QString core = QStringLiteral("/cores/genesis_plus_gx_libretro.so");
  const LaunchCommand native = GameLauncher::retroArchCommand(content, core, false);
  QCOMPARE(native.program, QStringLiteral("retroarch"));
  QCOMPARE(native.arguments, QStringList({QStringLiteral("-L"), core, content}));
  const LaunchCommand flatpak = GameLauncher::retroArchCommand(content, core, true);
  QCOMPARE(flatpak.program, QStringLiteral("flatpak"));
  QCOMPARE(flatpak.arguments,
           QStringList({QStringLiteral("run"), QStringLiteral("org.libretro.RetroArch"),
                        QStringLiteral("-L"), core, content}));
  QVERIFY(!GameLauncher::retroArchCommand(content, QStringLiteral("DETECT"), false).isValid());
  QVERIFY(!GameLauncher::retroArchCommand({}, core, false).isValid());

  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString archive = directory.path() + QStringLiteral("/games.zip");
  writeFile(archive, "archive");
  GameLauncher launcher;
  QVERIFY(!launcher.launch(QStringLiteral("RetroArch"), QStringLiteral("id"), false, {},
                           archive + QStringLiteral("#Sonic.bin"), {}));
  QVERIFY(!launcher.lastError().startsWith(QStringLiteral("The installed files are missing.")));
}

void CoreTests::battleNetScannerImportsInstalledGamesAndArtwork() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString prefix = directory.path() + QStringLiteral("/wine");
  createBattleNetFixture(prefix);

  const BattleNetScanResult result = BattleNetScanner::scan({prefix});
  QVERIFY(!result.incomplete);
  QCOMPARE(result.prefixes, QStringList({QDir::cleanPath(prefix)}));
  QCOMPARE(result.games.size(), 3);
  QCOMPARE(result.games.at(0).productId, QStringLiteral("wow"));
  QCOMPARE(result.games.at(0).title, QStringLiteral("World of Warcraft"));
  QCOMPARE(result.games.at(0).launchCode, QStringLiteral("WoW"));
  QCOMPARE(result.games.at(0).runner, QStringLiteral("wine"));
  QCOMPARE(result.games.at(0).lastPlayed, 1700000000);
  QVERIFY(result.games.at(0).coverPath.endsWith(QStringLiteral("cover.png")));
  QCOMPARE(result.games.at(1).productId, QStringLiteral("pro"));
  QCOMPARE(result.games.at(1).title, QStringLiteral("Overwatch 2"));
  QCOMPARE(result.games.at(1).lastPlayed, 1700001000);
  QVERIFY(result.games.at(1).heroPath.endsWith(QStringLiteral("library_hero.jpg")));
  QCOMPARE(result.games.at(2).productId, QStringLiteral("custom_mod"));
  QCOMPARE(result.games.at(2).title, QStringLiteral("Custom mod"));
  QVERIFY(result.games.at(2).coverPath.isEmpty());
  QVERIFY(result.games.at(0).gameId.contains(QStringLiteral("wow@")));
  QVERIFY(result.games.at(0).gameId != result.games.at(0).productId);
  QVERIFY(BattleNetScanner::isToolProduct(QStringLiteral("agent")));
  QVERIFY(BattleNetScanner::isToolProduct(QStringLiteral("bna")));
  QVERIFY(!BattleNetScanner::isToolProduct(QStringLiteral("wow")));
  QCOMPARE(BattleNetScanner::slugForProduct(QStringLiteral("hero")),
           QStringLiteral("heroes-of-the-storm"));
  QCOMPARE(BattleNetScanner::coverUrl(QStringLiteral("hero")).toString(),
           QStringLiteral("https://lutris.net/games/cover/heroes-of-the-storm.jpg"));
  QCOMPARE(BattleNetScanner::heroUrl(QStringLiteral("hero")).toString(),
           QStringLiteral("https://lutris.net/games/banner/heroes-of-the-storm.jpg"));
  QVERIFY(BattleNetScanner::coverUrl(QStringLiteral("custom_mod")).isEmpty());

  bool ok = false;
  const QVector<BattleNetProductInstall> decoded = BattleNetScanner::decodeProductDb(
      BattleNetScanner::encodeProductDb({{QStringLiteral("wow"), QStringLiteral("wow"),
                                          QStringLiteral("C:\\Games\\WoW"), true, true}}),
      &ok);
  QVERIFY(ok);
  QCOMPARE(decoded.size(), 1);
  QCOMPARE(decoded.at(0).productCode, QStringLiteral("wow"));
  QCOMPARE(decoded.at(0).installPath, QStringLiteral("C:\\Games\\WoW"));
  QVERIFY(decoded.at(0).installed);
}

void CoreTests::battleNetScannerDiscoversKnownPrefixes() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const bool homeWasSet = qEnvironmentVariableIsSet("HOME");
  const bool dataHomeWasSet = qEnvironmentVariableIsSet("XDG_DATA_HOME");
  const QByteArray previousHome = qgetenv("HOME");
  const QByteArray previousDataHome = qgetenv("XDG_DATA_HOME");
  const auto restoreEnvironment = qScopeGuard([&] {
    if (homeWasSet) {
      qputenv("HOME", previousHome);
    } else {
      qunsetenv("HOME");
    }
    if (dataHomeWasSet) {
      qputenv("XDG_DATA_HOME", previousDataHome);
    } else {
      qunsetenv("XDG_DATA_HOME");
    }
  });
  const QString home = directory.path() + QStringLiteral("/home");
  const QString data = directory.path() + QStringLiteral("/data");
  qputenv("HOME", home.toUtf8());
  qputenv("XDG_DATA_HOME", data.toUtf8());

  createBattleNetFixture(home + QStringLiteral("/.wine"));
  createBattleNetFixture(data + QStringLiteral("/wineprefixes/bnet"));
  createBattleNetFixture(data + QStringLiteral("/bottles/bottles/Wow"));
  writeFile(data + QStringLiteral("/bottles/bottles/Wow/bottle.yml"), "Name: Wow\n");
  createBattleNetFixture(home + QStringLiteral("/Games/battlenet"));
  writeFile(home + QStringLiteral("/Games/battlenet/version"), "GE-Proton11-6\n");
  const QString steamRoot = home + QStringLiteral("/.local/share/Steam");
  createBattleNetFixture(steamRoot + QStringLiteral("/steamapps/compatdata/4242/pfx"));
  writeFile(steamRoot + QStringLiteral("/steamapps/compatdata/4242/version"), "9.0\n");
  writeFile(steamRoot + QStringLiteral("/steamapps/libraryfolders.vdf"),
            "\"libraryfolders\"\n{\n\"0\" { \"path\" \"" + steamRoot.toUtf8() + "\" }\n}\n");

  const QStringList prefixes = BattleNetScanner::discoverPrefixes();
  QVERIFY(prefixes.contains(QDir::cleanPath(home + QStringLiteral("/.wine"))));
  QVERIFY(prefixes.contains(QDir::cleanPath(data + QStringLiteral("/wineprefixes/bnet"))));
  QVERIFY(prefixes.contains(QDir::cleanPath(data + QStringLiteral("/bottles/bottles/Wow"))));
  QVERIFY(prefixes.contains(QDir::cleanPath(home + QStringLiteral("/Games/battlenet"))));
  QVERIFY(prefixes.contains(
      QDir::cleanPath(steamRoot + QStringLiteral("/steamapps/compatdata/4242/pfx"))));

  const BattleNetScanResult bottles =
      BattleNetScanner::scan({data + QStringLiteral("/bottles/bottles/Wow")});
  QCOMPARE(bottles.games.at(0).runner, QStringLiteral("bottles"));
  const BattleNetScanResult proton =
      BattleNetScanner::scan({steamRoot + QStringLiteral("/steamapps/compatdata/4242/pfx")});
  QCOMPARE(proton.games.at(0).runner, QStringLiteral("proton"));
  const BattleNetScanResult omarchy =
      BattleNetScanner::scan({home + QStringLiteral("/Games/battlenet")});
  QCOMPARE(omarchy.games.at(0).runner, QStringLiteral("proton"));
}

void CoreTests::battleNetScannerKeepsInstallsFromSeparatePrefixes() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString first = directory.path() + QStringLiteral("/wine");
  const QString second = directory.path() + QStringLiteral("/proton/pfx");
  createBattleNetFixture(first);
  createBattleNetFixture(second);
  writeFile(directory.path() + QStringLiteral("/proton/version"), "9.0\n");

  const BattleNetScanResult result = BattleNetScanner::scan({first, second});
  QVERIFY(!result.incomplete);
  QCOMPARE(result.games.size(), 6);
  QStringList wowIds;
  for (const BattleNetGameRecord& game : result.games) {
    if (game.productId == QStringLiteral("wow")) {
      wowIds.append(game.gameId);
    }
  }
  QCOMPARE(wowIds.size(), 2);
  QVERIFY(wowIds.at(0) != wowIds.at(1));
  QCOMPARE(BattleNetScanner::productCodeFromId(wowIds.at(0)), QStringLiteral("wow"));
}

void CoreTests::battleNetModelIsRepeatableAndPreservesLocalState() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto restoreCacheHome = redirectCacheHome(directory.path() + QStringLiteral("/cache"));
  Q_UNUSED(restoreCacheHome);
  const QString prefix = directory.path() + QStringLiteral("/wine");
  const QString database = directory.path() + QStringLiteral("/omakade.sqlite3");
  createBattleNetFixture(prefix);

  BattleNetGameModel model(database);
  model.refreshFromPrefixes({prefix});
  QCOMPARE(model.rowCount(), 3);
  QCOMPARE(model.detectedPaths(), QStringList({QDir::cleanPath(prefix)}));
  QVERIFY(model.lastScan() > 0);
  QCOMPARE(model.data(model.index(0), GameRoles::Source).toString(),
           QStringLiteral("Battle.net"));
  QCOMPARE(model.data(model.index(0), GameRoles::Runner).toString(), QStringLiteral("wine"));
  QCOMPARE(model.data(model.index(0), GameRoles::LaunchTarget).toString(),
           QDir::cleanPath(prefix));
  model.toggleFavorite(0);
  model.toggleHidden(0);
  model.refreshFromPrefixes({prefix});
  QCOMPARE(model.rowCount(), 3);
  QVERIFY(model.data(model.index(0), GameRoles::Favorite).toBool());
  QVERIFY(model.data(model.index(0), GameRoles::Hidden).toBool());

  BattleNetGameModel reloaded(database);
  QCOMPARE(reloaded.detectedPaths(), QStringList({QDir::cleanPath(prefix)}));
  QCOMPARE(reloaded.lastScan(), model.lastScan());
  QVERIFY(reloaded.data(reloaded.index(0), GameRoles::Favorite).toBool());
}

void CoreTests::battleNetModelMigratesLegacyRowsSafely() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto restoreCacheHome = redirectCacheHome(directory.path() + QStringLiteral("/cache"));
  Q_UNUSED(restoreCacheHome);
  const QString databasePath = directory.path() + QStringLiteral("/omakade.sqlite3");
  const QString connection = QStringLiteral("battlenet-legacy-migration");
  {
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    database.setDatabaseName(databasePath);
    QVERIFY(database.open());
    QSqlQuery query(database);
    QVERIFY(query.exec(QStringLiteral(
        "CREATE TABLE battlenet_games (product_id TEXT, name TEXT NOT NULL, launch_code TEXT, "
        "install_path TEXT, wine_prefix TEXT, runner TEXT, cover_path TEXT, hero_path TEXT, "
        "last_played INTEGER NOT NULL DEFAULT 0, flatpak INTEGER NOT NULL DEFAULT 0, favorite "
        "INTEGER NOT NULL DEFAULT 0, hidden INTEGER NOT NULL DEFAULT 0, observed_at INTEGER NOT "
        "NULL)")));
    QVERIFY(query.exec(QStringLiteral(
        "INSERT INTO battlenet_games VALUES "
        "('wow', 'World of Warcraft', 'WoW', '/games/wow', '', 'wine', '', '', 0, 0, 1, 0, 1), "
        "('wow', 'Duplicate', 'WoW', '/games/duplicate', '', 'wine', '', '', 0, 0, 0, 1, 1), "
        "('hero', 'Heroes of the Storm', 'Hero', '/games/hero', '', 'wine', '', '', 0, 0, 0, 0, 1)")));
    database.close();
  }
  QSqlDatabase::removeDatabase(connection);

  BattleNetGameModel model(databasePath);
  QCOMPARE(model.rowCount(), 2);
  bool favoritePreserved = false;
  for (int row = 0; row < model.rowCount(); ++row) {
    favoritePreserved = favoritePreserved ||
                        model.data(model.index(row), GameRoles::Favorite).toBool();
  }
  QVERIFY(favoritePreserved);

  {
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    database.setDatabaseName(databasePath);
    QVERIFY(database.open());
    QSqlQuery query(database);
    QVERIFY(query.exec(QStringLiteral(
        "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = "
        "'battlenet_games_legacy'")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 0);
    database.close();
  }
  QSqlDatabase::removeDatabase(connection);
}

void CoreTests::malformedBattleNetDataDoesNotReplaceCachedGames() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto restoreCacheHome = redirectCacheHome(directory.path() + QStringLiteral("/cache"));
  Q_UNUSED(restoreCacheHome);
  const QString prefix = directory.path() + QStringLiteral("/wine");
  createBattleNetFixture(prefix);
  BattleNetGameModel model(directory.path() + QStringLiteral("/omakade.sqlite3"));
  model.refreshFromPrefixes({prefix});
  QCOMPARE(model.rowCount(), 3);
  const QStringList detectedPaths = model.detectedPaths();
  writeFile(prefix + QStringLiteral("/drive_c/ProgramData/Battle.net/Agent/product.db"),
            "not protobuf");
  model.refreshFromPrefixes({prefix});
  QCOMPARE(model.rowCount(), 3);
  QVERIFY(model.battleNetDetected());
  QCOMPARE(model.detectedPaths(), detectedPaths);
  QVERIFY(model.statusText().startsWith(QStringLiteral("Battle.net scan interrupted")));
}

void CoreTests::oversizedBattleNetDatabaseDoesNotReplaceCachedGames() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto restoreCacheHome = redirectCacheHome(directory.path() + QStringLiteral("/cache"));
  Q_UNUSED(restoreCacheHome);
  const QString prefix = directory.path() + QStringLiteral("/wine");
  createBattleNetFixture(prefix);
  BattleNetGameModel model(directory.path() + QStringLiteral("/omakade.sqlite3"));
  model.refreshFromPrefixes({prefix});
  QCOMPARE(model.rowCount(), 3);
  const QStringList detectedPaths = model.detectedPaths();
  QFile productDb(prefix + QStringLiteral("/drive_c/ProgramData/Battle.net/Agent/product.db"));
  QVERIFY(productDb.resize(16LL * 1024 * 1024 + 1));
  model.refreshFromPrefixes({prefix});
  QCOMPARE(model.rowCount(), 3);
  QVERIFY(model.battleNetDetected());
  QCOMPARE(model.detectedPaths(), detectedPaths);
  QVERIFY(model.statusText().startsWith(QStringLiteral("Battle.net scan interrupted")));
}

void CoreTests::battleNetLauncherBuildsSafeCommands() {
  const QString prefix = QStringLiteral("/tmp/omakade-bnet");
  const LaunchCommand wine =
      GameLauncher::battleNetCommand(QStringLiteral("wow"), prefix, QStringLiteral("wine"), false);
  QCOMPARE(wine.program, QStringLiteral("wine"));
  QCOMPARE(wine.arguments.constLast(), QStringLiteral("--exec=launch WoW"));
  QVERIFY(wine.arguments.constFirst().contains(QStringLiteral("Battle.net.exe")));
  QTemporaryDir bottlesDir;
  QVERIFY(bottlesDir.isValid());
  const QString bottlesPrefix = bottlesDir.path() + QStringLiteral("/folder-name");
  writeFile(bottlesPrefix + QStringLiteral("/bottle.yml"), "Name: Actual Bottle\n");
  const LaunchCommand bottles = GameLauncher::battleNetCommand(
      QStringLiteral("pro"), bottlesPrefix, QStringLiteral("bottles"), false);
  QCOMPARE(bottles.program, QStringLiteral("bottles-cli"));
  const int bottleFlag = bottles.arguments.indexOf(QStringLiteral("-b"));
  QVERIFY(bottleFlag >= 0);
  QCOMPARE(bottles.arguments.at(bottleFlag + 1), QStringLiteral("Actual Bottle"));
  QCOMPARE(bottles.arguments.constLast(), QStringLiteral("--exec=launch Pro"));
  const QString scopedId =
      BattleNetScanner::gameIdFor(QStringLiteral("wow"), prefix);
  const LaunchCommand scoped =
      GameLauncher::battleNetCommand(scopedId, prefix, QStringLiteral("wine"), false);
  QVERIFY(scoped.isValid());
  QCOMPARE(scoped.arguments.constLast(), QStringLiteral("--exec=launch WoW"));
  const LaunchCommand proton = GameLauncher::battleNetCommand(
      QStringLiteral("s2"), prefix, QStringLiteral("proton"), false);
  QCOMPARE(proton.program, QStringLiteral("env"));
  QCOMPARE(proton.arguments.at(0), QStringLiteral("WINEPREFIX=%1").arg(prefix));
  QCOMPARE(proton.arguments.at(1), QStringLiteral("umu-run"));
  QVERIFY(proton.arguments.at(2).contains(QStringLiteral("Battle.net.exe")));
  QCOMPARE(proton.arguments.constLast(), QStringLiteral("--exec=launch S2"));
  QTemporaryDir omarchyPrefixDir;
  QVERIFY(omarchyPrefixDir.isValid());
  const QString omarchyPrefix = omarchyPrefixDir.path() + QStringLiteral("/battlenet");
  createBattleNetFixture(omarchyPrefix);
  writeFile(omarchyPrefix + QStringLiteral("/version"), "GE-Proton11-6\n");
  const LaunchCommand omarchy = GameLauncher::battleNetCommand(
      QStringLiteral("wow"), omarchyPrefix, QStringLiteral("proton"), false);
  QCOMPARE(omarchy.program, QStringLiteral("env"));
  QVERIFY(omarchy.arguments.contains(QStringLiteral("GAMEID=umu-battlenet")));
  QVERIFY(omarchy.arguments.contains(QStringLiteral("STORE=battlenet")));
  QVERIFY(omarchy.arguments.contains(QStringLiteral("PROTONPATH=GE-Proton")));
  QVERIFY(omarchy.arguments.contains(QStringLiteral("PROTON_VERB=run")));
  QVERIFY(!GameLauncher::battleNetCommand(QStringLiteral("bad;id"), prefix, QStringLiteral("wine"),
                                          false)
               .isValid());
  QVERIFY(!GameLauncher::battleNetCommand(QStringLiteral("wow"), QStringLiteral("relative"),
                                          QStringLiteral("wine"), false)
               .isValid());
  QVERIFY(!GameLauncher::battleNetCommand(QStringLiteral("../escape"), prefix,
                                          QStringLiteral("wine"), false)
               .isValid());
  QVERIFY(!GameLauncher::battleNetCommand(QStringLiteral("wow"), prefix,
                                          QStringLiteral("unknown"), false)
               .isValid());
}

void CoreTests::launcherReportsInvalidAndStaleTargets() {
  GameLauncher launcher;
  QVERIFY(!launcher.launch(QStringLiteral("Lutris"), QStringLiteral("bad")));
  QCOMPARE(launcher.lastError(), QStringLiteral("This game has an invalid Lutris ID."));
  QVERIFY(!launcher.launch(QStringLiteral("Heroic"), QStringLiteral("valid"), false,
                           QStringLiteral("unknown")));
  QCOMPARE(launcher.lastError(), QStringLiteral("This game has an invalid Heroic target."));
  QVERIFY(!launcher.launch(QStringLiteral("Steam"), QStringLiteral("440"), false, {},
                           QStringLiteral("/path/that/does/not/exist")));
  QVERIFY(launcher.lastError().startsWith(QStringLiteral("The installed files are missing.")));
  QVERIFY(!launcher.launch(QStringLiteral("Battle.net"), QStringLiteral("wow;rm"), false,
                           QStringLiteral("wine"), {}, QStringLiteral("/tmp/omakade-bnet")));
  QCOMPARE(launcher.lastError(), QStringLiteral("This game has an invalid Battle.net target."));

  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString prefix = directory.path() + QStringLiteral("/prefix");
  createBattleNetFixture(prefix);
  const QByteArray previousPath = qgetenv("PATH");
  const bool pathWasSet = qEnvironmentVariableIsSet("PATH");
  const auto restorePath = qScopeGuard([previousPath, pathWasSet] {
    if (pathWasSet) {
      qputenv("PATH", previousPath);
    } else {
      qunsetenv("PATH");
    }
  });
  qputenv("PATH", directory.path().toUtf8());
  QVERIFY(!launcher.launch(QStringLiteral("Battle.net"), QStringLiteral("wow"), false,
                           QStringLiteral("bottles"), {}, prefix));
  QCOMPARE(launcher.lastError(), QStringLiteral("Bottles is not installed."));
  QVERIFY(!launcher.launch(QStringLiteral("Battle.net"), QStringLiteral("wow"), false,
                           QStringLiteral("proton"), {}, prefix));
  QCOMPARE(launcher.lastError(),
           QStringLiteral("umu-launcher is not installed. Install it to launch Battle.net games "
                          "from Proton."));
}

void CoreTests::igdbApiBuildsSafeQueriesAndParsesInsights() {
  const QByteArray mappingQuery = IgdbApi::steamMappingQuery(QStringLiteral("1245620"));
  QVERIFY(mappingQuery.contains("uid = \"1245620\""));
  QVERIFY(mappingQuery.contains("external_game_source.name = \"Steam\""));
  QVERIFY(IgdbApi::steamMappingQuery(QStringLiteral("1; limit 500")).isEmpty());
  QVERIFY(IgdbApi::gameQuery(0).isEmpty());
  QVERIFY(IgdbApi::timeToBeatQuery(-1).isEmpty());

  qint64 gameId = 0;
  QString error;
  QVERIFY(IgdbApi::parseSteamMapping(R"([{"id":9,"game":1942}])", &gameId, &error));
  QCOMPARE(gameId, 1942);

  IgdbGameInsight insight;
  QVERIFY(IgdbApi::parseGame(
      R"([{"id":1942,"name":"The Witcher 3","aggregated_rating":92.6,"aggregated_rating_count":47}])",
      &insight, &error));
  QCOMPARE(insight.gameId, 1942);
  QCOMPARE(insight.title, QStringLiteral("The Witcher 3"));
  QCOMPARE(insight.criticScore, 93);
  QCOMPARE(insight.criticReviewCount, 47);

  QVERIFY(IgdbApi::parseTimeToBeat(
      R"([{"game_id":1942,"hastily":184200,"normally":370800,"completely":624600,"count":382}])",
      &insight, &error));
  QCOMPARE(insight.rushedSeconds, 184200);
  QCOMPARE(insight.normalSeconds, 370800);
  QCOMPARE(insight.completeSeconds, 624600);
  QCOMPARE(insight.timeSampleCount, 382);
  QVERIFY(!IgdbApi::parseTimeToBeat(R"([{"game_id":7,"normally":20}])", &insight, &error));
  QVERIFY(!IgdbApi::parseGame("not json", &insight, &error));
}

void CoreTests::igdbInsightsLoadFromOfflineCache() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString databasePath = directory.path() + QStringLiteral("/library.sqlite3");
  const QString connection = QStringLiteral("igdb-cache-fixture");
  {
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    database.setDatabaseName(databasePath);
    QVERIFY(database.open());
    QSqlQuery query(database);
    QVERIFY(query.exec(QStringLiteral(
        "CREATE TABLE game_insights (source TEXT NOT NULL, app_id TEXT NOT NULL, provider TEXT "
        "NOT NULL, provider_game_id INTEGER NOT NULL, title TEXT NOT NULL, critic_score INTEGER "
        "NOT NULL, critic_review_count INTEGER NOT NULL, rushed_seconds INTEGER NOT NULL, "
        "normal_seconds INTEGER NOT NULL, complete_seconds INTEGER NOT NULL, time_sample_count "
        "INTEGER NOT NULL, updated_at INTEGER NOT NULL, PRIMARY KEY(source, app_id, provider))")));
    QVERIFY(query.exec(QStringLiteral(
        "INSERT INTO game_insights VALUES('Steam', '10', 'igdb', 1942, 'Cached Game', 88, 31, "
        "7200, 14400, 28800, 99, 1700000000)")));
    // A remembered miss: IGDB had no entry, so provider_game_id is 0.
    QVERIFY(query.exec(QStringLiteral(
        "INSERT INTO game_insights VALUES('Steam', '20', 'igdb', 0, '', -1, 0, 0, 0, 0, 0, "
        "1700000000)")));
    database.close();
  }
  QSqlDatabase::removeDatabase(connection);

  AppSettings settings(directory.path() + QStringLiteral("/config.toml"));
  GameInsightsService insights(databasePath, &settings);
  QTRY_VERIFY_WITH_TIMEOUT(!insights.busy(), 2000);
  insights.loadSteam(QStringLiteral("10"));
  QVERIFY(insights.available());
  QCOMPARE(insights.criticScore(), 88);
  QCOMPARE(insights.criticReviewCount(), 31);
  QCOMPARE(insights.rushedHours(), 2);
  QCOMPARE(insights.normalHours(), 4);
  QCOMPARE(insights.completeHours(), 8);
  QCOMPARE(insights.timeSampleCount(), 99);
  QCOMPARE(insights.statusText(), QStringLiteral("Cached IGDB data"));
  insights.loadSteam(QStringLiteral("20"));
  QVERIFY(!insights.available());
  QCOMPARE(insights.statusText(), QStringLiteral("IGDB has no entry for this game"));
}

void CoreTests::retroAchievementsHasherAppliesHeaderStripRules() {
  QCOMPARE(RetroAchievementsHasher::consoleFor(QStringLiteral("Nintendo - Game Boy")).rule,
           RetroAchievementsHashRule::WholeFileMd5);
  QCOMPARE(RetroAchievementsHasher::consoleFor(
               QStringLiteral("Nintendo - Nintendo Entertainment System"))
               .rule,
           RetroAchievementsHashRule::NesHeaderStrip);
  QCOMPARE(RetroAchievementsHasher::consoleFor(
               QStringLiteral("Nintendo - Super Nintendo Entertainment System"))
               .rule,
           RetroAchievementsHashRule::SnesHeaderStrip);
  QCOMPARE(RetroAchievementsHasher::consoleFor(QStringLiteral("Sega - Mega Drive - Genesis")).rule,
           RetroAchievementsHashRule::WholeFileMd5);
  QCOMPARE(RetroAchievementsHasher::consoleFor(QStringLiteral("Atari - 7800")).rule,
           RetroAchievementsHashRule::Atari7800HeaderStrip);
  QCOMPARE(RetroAchievementsHasher::consoleFor(QStringLiteral("Atari - Lynx")).rule,
           RetroAchievementsHashRule::AtariLynxHeaderStrip);
  QCOMPARE(RetroAchievementsHasher::consoleFor(QStringLiteral("Sony - PlayStation")).rule,
           RetroAchievementsHashRule::Unsupported);

  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QByteArray payload = "PAYLOAD-BYTES";
  const QByteArray payloadMd5 = QCryptographicHash::hash(payload, QCryptographicHash::Md5).toHex();

  const QString nesHeadered = directory.path() + QStringLiteral("/game.nes");
  {
    QFile file(nesHeadered);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QByteArray("NES\x1A", 4) + QByteArray(12, '\0') + payload);
  }
  QCOMPARE(RetroAchievementsHasher::hashFile(nesHeadered, RetroAchievementsHashRule::NesHeaderStrip)
               .value_or(QByteArray()),
           payloadMd5);

  const QString nesHeaderless = directory.path() + QStringLiteral("/plain.nes");
  {
    QFile file(nesHeaderless);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(payload);
  }
  QCOMPARE(RetroAchievementsHasher::hashFile(nesHeaderless, RetroAchievementsHashRule::NesHeaderStrip)
               .value_or(QByteArray()),
           payloadMd5);

  // SNES headers are detected at 8KB (0x2000) granularity, not 32KB — use a payload size that's
  // a multiple of 8KB but not of 32KB so the test actually distinguishes the two.
  const QString snesHeadered = directory.path() + QStringLiteral("/game.sfc");
  const QByteArray snesPayload(0x2000, 'A');
  const QByteArray snesPayloadMd5 =
      QCryptographicHash::hash(snesPayload, QCryptographicHash::Md5).toHex();
  {
    QFile file(snesHeadered);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QByteArray(512, 'H') + snesPayload);
  }
  QCOMPARE(
      RetroAchievementsHasher::hashFile(snesHeadered, RetroAchievementsHashRule::SnesHeaderStrip)
          .value_or(QByteArray()),
      snesPayloadMd5);

  // PC Engine uses the same header-strip rule as SNES but at 128KB (0x20000) granularity.
  const QString pcEngineHeadered = directory.path() + QStringLiteral("/game.pce");
  const QByteArray pcEnginePayload(0x20000, 'B');
  const QByteArray pcEnginePayloadMd5 =
      QCryptographicHash::hash(pcEnginePayload, QCryptographicHash::Md5).toHex();
  {
    QFile file(pcEngineHeadered);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QByteArray(512, 'H') + pcEnginePayload);
  }
  QCOMPARE(RetroAchievementsHasher::hashFile(pcEngineHeadered,
                                             RetroAchievementsHashRule::PcEngineHeaderStrip)
               .value_or(QByteArray()),
           pcEnginePayloadMd5);
  // A payload that isn't 512 bytes past a 128KB boundary must not be stripped.
  QCOMPARE(RetroAchievementsHasher::hashFile(nesHeadered, RetroAchievementsHashRule::PcEngineHeaderStrip)
               .value_or(QByteArray()),
           QCryptographicHash::hash(QByteArray("NES\x1A", 4) + QByteArray(12, '\0') + payload,
                                    QCryptographicHash::Md5)
               .toHex());

  const QString atari7800Headered = directory.path() + QStringLiteral("/game.a78");
  {
    QFile file(atari7800Headered);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QByteArray(1, '\x01') + QByteArray("ATARI7800", 9) + QByteArray(118, '\0') +
              payload);
  }
  QCOMPARE(RetroAchievementsHasher::hashFile(atari7800Headered,
                                             RetroAchievementsHashRule::Atari7800HeaderStrip)
               .value_or(QByteArray()),
           payloadMd5);

  const QString lynxHeadered = directory.path() + QStringLiteral("/game.lnx");
  {
    QFile file(lynxHeadered);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QByteArray("LYNX", 4) + QByteArray(1, '\0') + QByteArray(59, '\0') + payload);
  }
  QCOMPARE(
      RetroAchievementsHasher::hashFile(lynxHeadered, RetroAchievementsHashRule::AtariLynxHeaderStrip)
          .value_or(QByteArray()),
      payloadMd5);

  QVERIFY(!RetroAchievementsHasher::hashFile(nesHeadered, RetroAchievementsHashRule::Unsupported)
               .has_value());
  QVERIFY(!RetroAchievementsHasher::hashFile(directory.path() + QStringLiteral("/missing.nes"),
                                             RetroAchievementsHashRule::WholeFileMd5)
               .has_value());
}

void CoreTests::retroAchievementsHasherReadsZipArchivedRoms() {
  // RetroArch stores archived content as "archive.zip#inner/path.rom" (see
  // RetroArchScanner::runtimeFileName), which is how the overwhelming majority of real RetroArch
  // libraries store ROMs. The hasher must read the specific zip entry, not the literal path.
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QByteArray payload = "ZIPPED-ROM-BYTES";
  const QByteArray payloadMd5 = QCryptographicHash::hash(payload, QCryptographicHash::Md5).toHex();
  const QString archivePath = directory.path() + QStringLiteral("/game.zip");
  const QString innerName = QStringLiteral("game.gb");

  int errorCode = 0;
  zip_t* archive = zip_open(archivePath.toUtf8().constData(), ZIP_CREATE | ZIP_TRUNCATE, &errorCode);
  QVERIFY(archive != nullptr);
  zip_source_t* source = zip_source_buffer(archive, payload.constData(), payload.size(), 0);
  QVERIFY(source != nullptr);
  QVERIFY(zip_file_add(archive, innerName.toUtf8().constData(), source, ZIP_FL_OVERWRITE) >= 0);
  QCOMPARE(zip_close(archive), 0);

  const QString contentPath = archivePath + QLatin1Char('#') + innerName;
  QCOMPARE(RetroAchievementsHasher::hashFile(contentPath, RetroAchievementsHashRule::WholeFileMd5)
               .value_or(QByteArray()),
           payloadMd5);

  QVERIFY(!RetroAchievementsHasher::hashFile(archivePath + QStringLiteral("#missing-entry.gb"),
                                             RetroAchievementsHashRule::WholeFileMd5)
               .has_value());
  QVERIFY(!RetroAchievementsHasher::hashFile(directory.path() +
                                                 QStringLiteral("/missing.zip#game.gb"),
                                             RetroAchievementsHashRule::WholeFileMd5)
               .has_value());
}

void CoreTests::retroAchievementsApiBuildsUrlsAndParsesResponses() {
  const QUrl gameInfoUrl =
      RetroAchievementsApi::gameInfoAndProgressUrl(QStringLiteral("KEY123"), 1942,
                                                   QStringLiteral("someuser"));
  QCOMPARE(gameInfoUrl.host(), QStringLiteral("retroachievements.org"));
  QVERIFY(gameInfoUrl.query().contains(QStringLiteral("g=1942")));
  QVERIFY(gameInfoUrl.query().contains(QStringLiteral("u=someuser")));
  QVERIFY(gameInfoUrl.query().contains(QStringLiteral("y=KEY123")));

  QCOMPARE(RetroAchievementsApi::classifyHttpResponse(0, true), RetroAchievementsApiState::Offline);
  QCOMPARE(RetroAchievementsApi::classifyHttpResponse(403, false),
           RetroAchievementsApiState::InvalidKey);
  QCOMPARE(RetroAchievementsApi::classifyHttpResponse(429, false),
           RetroAchievementsApiState::RateLimited);
  QCOMPARE(RetroAchievementsApi::classifyHttpResponse(200, false), RetroAchievementsApiState::Ready);

  QVector<RetroAchievementsConsoleRecord> consoles;
  QVERIFY(RetroAchievementsApi::parseConsoleIds(
      R"([{"ID":7,"Name":"NES/Famicom"},{"ID":1,"Name":"Genesis/Mega Drive"}])", &consoles));
  QCOMPARE(consoles.size(), 2);
  QCOMPARE(consoles.at(0).id, 7);
  QCOMPARE(consoles.at(0).name, QStringLiteral("NES/Famicom"));

  // Regression: "Game Boy" is a substring of "Game Boy Color", so naive substring matching must
  // not let the shorter, unrelated system win just because it's listed first.
  const QVector<RetroAchievementsConsoleRecord> gameBoyFamily = {
      {.id = 4, .name = QStringLiteral("Game Boy")},
      {.id = 5, .name = QStringLiteral("Game Boy Advance")},
      {.id = 6, .name = QStringLiteral("Game Boy Color")},
  };
  QCOMPARE(RetroAchievementsApi::bestConsoleMatch(gameBoyFamily, QStringLiteral("Game Boy Color")),
           6);
  QCOMPARE(RetroAchievementsApi::bestConsoleMatch(gameBoyFamily, QStringLiteral("Game Boy")), 4);
  QCOMPARE(RetroAchievementsApi::bestConsoleMatch(gameBoyFamily, QStringLiteral("PlayStation")), 0);
  const QVector<RetroAchievementsConsoleRecord> headlineSystems = {
      {.id = 7, .name = QStringLiteral("NES/Famicom")},
      {.id = 3, .name = QStringLiteral("SNES/Super Famicom")},
      {.id = 1, .name = QStringLiteral("Mega Drive")},
      {.id = 8, .name = QStringLiteral("Famicom Disk System")},
  };
  QCOMPARE(RetroAchievementsApi::bestConsoleMatch(
               headlineSystems, QStringLiteral("Nintendo Entertainment System")),
           7);
  QCOMPARE(RetroAchievementsApi::bestConsoleMatch(
               headlineSystems, QStringLiteral("Super Nintendo Entertainment System")),
           3);
  QCOMPARE(RetroAchievementsApi::bestConsoleMatch(headlineSystems,
                                                   QStringLiteral("Genesis/Mega Drive")),
           1);
  QCOMPARE(RetroAchievementsApi::bestConsoleMatch(
               headlineSystems, QStringLiteral("Nintendo - Famicom Disk System")),
           8);

  QVector<RetroAchievementsHashRecord> games;
  QVERIFY(RetroAchievementsApi::parseGameList(
      R"([{"ID":1942,"Title":"Some Game","Hashes":["ABCDEF","abc123"]}])", &games));
  QCOMPARE(games.size(), 1);
  QCOMPARE(games.at(0).gameId, qint64(1942));
  QCOMPARE(games.at(0).md5Hashes.size(), 2);
  QCOMPARE(games.at(0).md5Hashes.at(0), QStringLiteral("abcdef"));

  const QByteArray progressJson =
      R"({"ID":1942,"Title":"Some Game","Achievements":{)"
      R"("111":{"ID":111,"Title":"First","Description":"Do the thing","BadgeName":"012345",)"
      R"("DateEarned":"2021-01-02 03:04:05"},)"
      R"("112":{"ID":112,"Title":"Second","Description":"","BadgeName":"012346"}}})";
  RetroAchievementsProgressResult progress;
  QString error;
  QCOMPARE(RetroAchievementsApi::parseGameInfoAndProgress(progressJson, &progress, &error),
           RetroAchievementsApiState::Ready);
  QVERIFY(error.isEmpty());
  QCOMPARE(progress.total, 2);
  QCOMPARE(progress.unlocked, 1);
  bool foundUnlocked = false;
  bool foundLocked = false;
  for (const RetroAchievementsAchievementRecord& achievement : progress.achievements) {
    if (achievement.apiName == QStringLiteral("111")) {
      QVERIFY(achievement.unlocked);
      QCOMPARE(achievement.unlockTime, qint64(1609556645));
      QVERIFY(achievement.iconUrl.endsWith(QStringLiteral("012345.png")));
      foundUnlocked = true;
    } else if (achievement.apiName == QStringLiteral("112")) {
      QVERIFY(!achievement.unlocked);
      QVERIFY(achievement.iconUrl.endsWith(QStringLiteral("012346_lock.png")));
      foundLocked = true;
    }
  }
  QVERIFY(foundUnlocked);
  QVERIFY(foundLocked);

  RetroAchievementsProgressResult errorResult;
  QCOMPARE(RetroAchievementsApi::parseGameInfoAndProgress(R"({"Error":"Game not found"})",
                                                          &errorResult, &error),
           RetroAchievementsApiState::RemoteError);
  QCOMPARE(error, QStringLiteral("Game not found"));
  QCOMPARE(RetroAchievementsApi::parseGameInfoAndProgress("not json", &errorResult, &error),
           RetroAchievementsApiState::RemoteError);
}

void CoreTests::retroArchModelReadsCachedRetroAchievementsSummary() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString databasePath = directory.path() + QStringLiteral("/library.sqlite3");
  const QString connection = QStringLiteral("retroachievements-cache-fixture");
  {
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    database.setDatabaseName(databasePath);
    QVERIFY(database.open());
    QSqlQuery query(database);
    QVERIFY(query.exec(QStringLiteral(
        "CREATE TABLE retroarch_games (game_id TEXT PRIMARY KEY, name TEXT NOT NULL, "
        "content_path TEXT NOT NULL, core_path TEXT, core_name TEXT, cover_path TEXT, hero_path "
        "TEXT, system TEXT NOT NULL DEFAULT '', playtime_seconds INTEGER NOT NULL DEFAULT 0, "
        "last_played INTEGER NOT NULL DEFAULT 0, flatpak INTEGER NOT NULL DEFAULT 0, favorite "
        "INTEGER NOT NULL DEFAULT 0, hidden INTEGER NOT NULL DEFAULT 0, observed_at INTEGER NOT "
        "NULL)")));
    QVERIFY(query.exec(QStringLiteral(
        "INSERT INTO retroarch_games(game_id, name, content_path, system, observed_at) VALUES("
        "'rg-1', 'Test Game', '/tmp/test.gb', 'Nintendo - Game Boy', 1700000000)")));
    QVERIFY(query.exec(QStringLiteral(
        "CREATE TABLE achievement_summary (app_id TEXT PRIMARY KEY, unlocked INTEGER NOT NULL, "
        "total INTEGER NOT NULL, source TEXT NOT NULL, updated_at INTEGER NOT NULL)")));
    QVERIFY(query.exec(QStringLiteral("INSERT INTO achievement_summary VALUES('rg-1', 3, 10, "
                                      "'retroachievements', 1700000000)")));
    QVERIFY(query.exec(QStringLiteral(
        "CREATE TABLE achievements (app_id TEXT NOT NULL, api_name TEXT NOT NULL, title TEXT "
        "NOT NULL, description TEXT, icon_url TEXT, icon_path TEXT, unlocked INTEGER NOT NULL, "
        "unlock_time INTEGER NOT NULL, rarity REAL NOT NULL, hidden INTEGER NOT NULL, "
        "current_progress REAL NOT NULL, maximum_progress REAL NOT NULL, source TEXT NOT NULL, "
        "PRIMARY KEY(app_id, api_name))")));
    database.close();
  }
  QSqlDatabase::removeDatabase(connection);

  RetroArchGameModel model(databasePath);
  QCOMPARE(model.rowCount(), 1);
  const QModelIndex row = model.index(0);
  QCOMPARE(model.data(row, GameRoles::AchievementsUnlocked).toInt(), 3);
  QCOMPARE(model.data(row, GameRoles::AchievementsTotal).toInt(), 10);
  QCOMPARE(model.data(row, GameRoles::Progress).toInt(), 30);

  {
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    database.setDatabaseName(databasePath);
    QVERIFY(database.open());
    QSqlQuery query(database);
    QVERIFY(query.exec(QStringLiteral(
        "UPDATE achievement_summary SET unlocked = 10, total = 10 WHERE app_id = 'rg-1'")));
    database.close();
  }
  QSqlDatabase::removeDatabase(connection);

  QSignalSpy dataChangedSpy(&model, &RetroArchGameModel::dataChanged);
  model.reloadAchievementSummary(QStringLiteral("rg-1"));
  QCOMPARE(dataChangedSpy.count(), 1);
  QCOMPARE(model.data(row, GameRoles::AchievementsUnlocked).toInt(), 10);
  QCOMPARE(model.data(row, GameRoles::Progress).toInt(), 100);

  model.clearAchievementSummaries();
  QCOMPARE(dataChangedSpy.count(), 2);
  QCOMPARE(model.data(row, GameRoles::AchievementsUnlocked).toInt(), 0);
  QCOMPARE(model.data(row, GameRoles::AchievementsTotal).toInt(), 0);

  model.reloadAchievementSummary(QStringLiteral("rg-1"));
  QCOMPARE(model.data(row, GameRoles::AchievementsUnlocked).toInt(), 10);
  {
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    database.setDatabaseName(databasePath);
    QVERIFY(database.open());
    QSqlQuery query(database);
    QVERIFY(query.exec(QStringLiteral("DELETE FROM achievement_summary WHERE app_id = 'rg-1'")));
    database.close();
  }
  QSqlDatabase::removeDatabase(connection);
  model.reloadAchievementSummary(QStringLiteral("rg-1"));
  QCOMPARE(model.data(row, GameRoles::AchievementsUnlocked).toInt(), 0);
  QCOMPARE(model.data(row, GameRoles::AchievementsTotal).toInt(), 0);
}

void CoreTests::retroAchievementsServiceClearsCacheOnAccountSwitch() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString databasePath = directory.path() + QStringLiteral("/library.sqlite3");
  const QString connection = QStringLiteral("retroachievements-account-switch-fixture");
  {
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    database.setDatabaseName(databasePath);
    QVERIFY(database.open());
    QSqlQuery query(database);
    QVERIFY(query.exec(QStringLiteral(
        "CREATE TABLE achievement_summary (app_id TEXT PRIMARY KEY, unlocked INTEGER NOT NULL, "
        "total INTEGER NOT NULL, source TEXT NOT NULL, updated_at INTEGER NOT NULL)")));
    QVERIFY(query.exec(QStringLiteral(
        "CREATE TABLE achievements (app_id TEXT NOT NULL, api_name TEXT NOT NULL, title TEXT "
        "NOT NULL, description TEXT, icon_url TEXT, icon_path TEXT, unlocked INTEGER NOT NULL, "
        "unlock_time INTEGER NOT NULL, rarity REAL NOT NULL, hidden INTEGER NOT NULL, "
        "current_progress REAL NOT NULL, maximum_progress REAL NOT NULL, source TEXT NOT NULL, "
        "PRIMARY KEY(app_id, api_name))")));
    database.close();
  }
  QSqlDatabase::removeDatabase(connection);

  AppSettings settings(directory.path() + QStringLiteral("/config.toml"));
  RetroAchievementsService service(databasePath, &settings);
  QTRY_VERIFY_WITH_TIMEOUT(!service.busy(), 2000);

  service.setUsername(QStringLiteral("accountA"));
  QCOMPARE(service.username(), QStringLiteral("accountA"));

  // Seed progress as if account A had already refreshed this game.
  {
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    database.setDatabaseName(databasePath);
    QVERIFY(database.open());
    QSqlQuery query(database);
    QVERIFY(query.exec(QStringLiteral("INSERT INTO achievement_summary VALUES('rg-1', 3, 10, "
                                      "'retroachievements', 1700000000)")));
    QVERIFY(query.exec(QStringLiteral(
        "INSERT INTO achievements VALUES('rg-1', 'ach-1', 'Title', '', '', '', 1, 1700000000, 0, "
        "0, 1, 1, 'retroachievements')")));
    database.close();
  }
  QSqlDatabase::removeDatabase(connection);

  // Switching to a different account must not leave account A's cached progress behind for
  // account B to silently reuse.
  QSignalSpy achievementsClearedSpy(&service, &RetroAchievementsService::achievementsCleared);
  service.setUsername(QStringLiteral("accountB"));
  QCOMPARE(achievementsClearedSpy.count(), 1);

  {
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    database.setDatabaseName(databasePath);
    QVERIFY(database.open());
    QSqlQuery verify(database);
    QVERIFY(verify.exec(QStringLiteral(
        "SELECT COUNT(*) FROM achievement_summary WHERE source = 'retroachievements'")));
    QVERIFY(verify.next());
    QCOMPARE(verify.value(0).toInt(), 0);
    QVERIFY(verify.exec(
        QStringLiteral("SELECT COUNT(*) FROM achievements WHERE source = 'retroachievements'")));
    QVERIFY(verify.next());
    QCOMPARE(verify.value(0).toInt(), 0);
    database.close();
  }
  QSqlDatabase::removeDatabase(connection);
}

void CoreTests::retroAchievementsServiceBlocksAccountSwitchWhileBusy() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  AppSettings settings(directory.path() + QStringLiteral("/config.toml"));
  settings.setRetroAchievementsUsername(QStringLiteral("accountA"));
  RetroAchievementsService service(directory.path() + QStringLiteral("/library.sqlite3"),
                                   &settings);
  QVERIFY(service.busy());

  service.setUsername(QStringLiteral("accountB"));
  QCOMPARE(service.username(), QStringLiteral("accountA"));
  QVERIFY(service.statusText().contains(QStringLiteral("still busy")));
  QTRY_VERIFY_WITH_TIMEOUT(!service.busy(), 2000);
}

void CoreTests::stressLibraryContainsOneThousandGames() {
  MockGameModel games(nullptr, 1000);
  QCOMPARE(games.rowCount(), 1000);
  QCOMPARE(games.get(999).value(QStringLiteral("title")).toString(),
           QStringLiteral("Wild Orbit 40"));
}

void CoreTests::manualGamesImportEditLaunchAndRemove() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString executable = directory.path() + QStringLiteral("/native game");
  writeFile(executable, "#!/bin/sh\nprintf '%s\\n' \"$PWD\" \"$@\" > launch-result.txt\n");
  QVERIFY(QFile::setPermissions(executable, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
  const QString database = directory.path() + QStringLiteral("/library.sqlite3");
  QString id;
  {
    ManualGameModel manual(database);
    QVariantMap draft = manual.draftFromFile(QUrl::fromLocalFile(executable));
    QCOMPARE(draft.value(QStringLiteral("executable")).toString(), executable);
    draft.insert(QStringLiteral("title"), QStringLiteral("Native Test"));
    draft.insert(QStringLiteral("arguments"), QStringList{QStringLiteral("two words"), QString{}, QStringLiteral("$literal")});
    id = manual.saveEntry(draft);
    QVERIFY2(!id.isEmpty(), qPrintable(manual.lastError()));
    QCOMPARE(manual.rowCount(), 1);
    UnifiedGameModel unified(database);
    unified.addSourceModel(&manual);
    LibraryFilterModel library;
    library.setSourceModel(&unified);
    library.setSourceFilter(QStringLiteral("Manual"));
    QCOMPARE(library.rowCount(), 1);
    library.toggleFavorite(0);
    QVERIFY(library.setTags(0, QStringLiteral("native, indie")));
    QVERIFY(library.createCollection(QStringLiteral("Local")));
    QVERIFY(library.setCollectionMembership(0, QStringLiteral("Local"), true));
    GameLauncher launcher;
    QString error;
    QVERIFY2(PlayRequest::perform(unified, launcher, LaunchKey::parse(QStringLiteral("Manual::") + id), &error),
             qPrintable(error));
    const QString output = directory.path() + QStringLiteral("/launch-result.txt");
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(output), 3000);
    QFile result(output);
    QVERIFY(result.open(QIODevice::ReadOnly));
    QCOMPARE(result.readAll(), directory.path().toUtf8() + "\ntwo words\n\n$literal\n");
    QVERIFY(library.get(0).value(QStringLiteral("lastPlayed")).toLongLong() > 0);
    draft = manual.get(id);
    draft.insert(QStringLiteral("title"), QStringLiteral("Renamed Test"));
    QCOMPARE(manual.saveEntry(draft), id);
    QCOMPARE(manual.rowCount(), 1);
    QVERIFY(library.get(0).value(QStringLiteral("favorite")).toBool());
    QCOMPARE(library.get(0).value(QStringLiteral("title")).toString(), QStringLiteral("Renamed Test"));
  }
  ManualGameModel reloaded(database);
  QCOMPARE(reloaded.get(id).value(QStringLiteral("title")).toString(), QStringLiteral("Renamed Test"));
  QVERIFY(reloaded.get(id).value(QStringLiteral("favorite")).toBool());
  const QString desktop = directory.path() + QStringLiteral("/native.desktop");
  writeFile(desktop, QStringLiteral("[Desktop Entry]\nType=Application\nName=Desktop Game\nIcon=game-icon\n"
                                   "Exec=\"%1\" \"two words\" %% %c %k %i %U\nPath=%2\n")
                        .arg(executable, directory.path()).toUtf8());
  const QVariantMap imported = reloaded.draftFromFile(QUrl::fromLocalFile(desktop));
  QCOMPARE(imported.value(QStringLiteral("arguments")).toStringList(),
           QStringList({QStringLiteral("two words"), QStringLiteral("%"), QStringLiteral("Desktop Game"),
                        desktop, QStringLiteral("--icon"), QStringLiteral("game-icon")}));
  QCOMPARE(imported.value(QStringLiteral("directory")).toString(), directory.path());
  QVERIFY(!reloaded.saveEntry(imported).isEmpty());
  QCOMPARE(reloaded.rowCount(), 2);
  writeFile(desktop, "[Desktop Entry]\nType=Application\nName=Bad\nExec=true %Z\n");
  QVERIFY(reloaded.draftFromFile(QUrl::fromLocalFile(desktop)).isEmpty());
  QVERIFY(QFile::rename(executable, executable + QStringLiteral(" moved")));
  QString error;
  const QString target = reloaded.data(reloaded.index(0), GameRoles::LaunchTarget).toString();
  const QString targetId = reloaded.data(reloaded.index(0), GameRoles::AppId).toString();
  QVERIFY(!ManualGameModel::validateLaunch(target, targetId, nullptr, nullptr, nullptr, &error));
  QVERIFY(error.contains(QStringLiteral("missing")));
  QCOMPARE(reloaded.rowCount(), 2);
  MockGameModel demo(nullptr, 1);
  UnifiedGameModel linked(database);
  linked.addSourceModel(&demo);
  linked.addSourceModel(&reloaded);
  QVERIFY(linked.linkGames(0, QStringLiteral("Manual"), QString{}, id));
  LibraryFilterModel filtered;
  filtered.setSourceModel(&linked);
  filtered.setSourceFilter(QStringLiteral("Manual"));
  QVERIFY(filtered.indexOf(QStringLiteral("Manual"), QString{}, id) >= 0);
  QVERIFY(reloaded.removeEntry(id));
  QCOMPARE(filtered.indexOf(QStringLiteral("Manual"), QString{}, id), -1);
  QVERIFY(reloaded.get(id).isEmpty());
  QVERIFY(QFileInfo::exists(executable + QStringLiteral(" moved")));
  ManualGameModel afterRemoval(database);
  QCOMPARE(afterRemoval.rowCount(), 1);
}

void CoreTests::gogFoldersPersistAndHandleDisconnectedRoots() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString config = directory.path() + QStringLiteral("/settings.toml");
  const QString first = directory.path() + QStringLiteral("/GOG \"one\" é");
  const QString second = directory.path() + QStringLiteral("/GOG two");
  const auto addGame = [](const QString& root, const QString& id, const QString& title) {
    const QJsonObject task{{QStringLiteral("type"), QStringLiteral("FileTask")},
                           {QStringLiteral("isPrimary"), true},
                           {QStringLiteral("path"), QStringLiteral("start.sh")}};
    const QJsonObject manifest{{QStringLiteral("name"), title},
                               {QStringLiteral("playTasks"), QJsonArray{task}}};
    writeFile(root + QStringLiteral("/game/goggame-%1.info").arg(id),
              QJsonDocument(manifest).toJson());
    writeFile(root + QStringLiteral("/game/start.sh"), "#!/bin/sh\n");
  };
  addGame(first, QStringLiteral("101"), QStringLiteral("First"));
  addGame(second, QStringLiteral("202"), QStringLiteral("Second"));
  {
    AppSettings settings(config);
    QVERIFY(settings.addGogLibraryPath(QUrl::fromLocalFile(first).toString()));
    QVERIFY(settings.addGogLibraryPath(first + QStringLiteral("/../") + QFileInfo(first).fileName()));
    QCOMPARE(settings.gogLibraryPaths().size(), 1);
    QVERIFY(settings.addGogLibraryPath(second));
    QVERIFY(!settings.addGogLibraryPath(QStringLiteral("relative/path")));
    QVERIFY(!settings.addGogLibraryPath(QStringLiteral("https://example.test/games")));
    QVERIFY(!settings.addGogLibraryPath(first + QLatin1Char('\n')));
    QCOMPARE(settings.gogLibraryPathStatus(first), QStringLiteral("Available"));
  }
  AppSettings reloaded(config);
  QCOMPARE(reloaded.gogLibraryPaths(), QStringList({first, second}));
  const auto discovered = HeroicScanner::discoverRoots(reloaded.gogLibraryPaths());
  QVERIFY(discovered.contains(first));
  QVERIFY(discovered.contains(second));
  const auto scan = HeroicScanner::scan({first, second, first});
  QCOMPARE(scan.games.size(), 2);
  HeroicGameModel model(directory.path() + QStringLiteral("/library.sqlite3"));
  model.setGogLibraryPaths({first, second});
  model.refreshFromRoots({first, second});
  QCOMPARE(model.rowCount(), 2);
  const auto rowFor = [&model](const QString& id) {
    for (int row = 0; row < model.rowCount(); ++row)
      if (model.data(model.index(row), GameRoles::AppId).toString() == id) return row;
    return -1;
  };
  model.toggleFavorite(rowFor(QStringLiteral("101")));
  QVERIFY(QDir().rename(first, first + QStringLiteral(" offline")));
  QVERIFY(HeroicScanner::discoverRoots(reloaded.gogLibraryPaths()).contains(first));
  QVERIFY(reloaded.gogLibraryPathStatus(first).startsWith(QStringLiteral("Unavailable")));
  addGame(second, QStringLiteral("202"), QStringLiteral("Updated"));
  model.refreshFromRoots({first, second});
  QCOMPARE(model.rowCount(), 2);
  QVERIFY(model.errorText().contains(first));
  QCOMPARE(model.data(model.index(rowFor(QStringLiteral("202"))), GameRoles::Title).toString(),
           QStringLiteral("Updated"));
  QVERIFY(model.data(model.index(rowFor(QStringLiteral("101"))), GameRoles::Favorite).toBool());
  QVERIFY(QDir().rename(first + QStringLiteral(" offline"), first));
  model.refreshFromRoots({first, second});
  QVERIFY(!model.errorText().contains(QStringLiteral("GOG folder unavailable")));
  QVERIFY(reloaded.removeGogLibraryPath(first));
  QVERIFY(reloaded.removeGogLibraryPath(second));
  model.setGogLibraryPaths(reloaded.gogLibraryPaths());
  model.refreshFromRoots({});
  QCOMPARE(model.rowCount(), 0);
  QVERIFY(QFileInfo::exists(first + QStringLiteral("/game/goggame-101.info")));
  model.setGogLibraryPaths({first});
  model.refreshFromRoots({first});
  QCOMPARE(model.rowCount(), 1);
  QVERIFY(model.data(model.index(0), GameRoles::Favorite).toBool());
  // Closing before the rescan finishes cannot resurrect an explicitly removed folder.
  model.setGogLibraryPaths({});
  HeroicGameModel restarted(directory.path() + QStringLiteral("/library.sqlite3"));
  restarted.setGogLibraryPaths({});
  restarted.refreshFromRoots({});
  QCOMPARE(restarted.rowCount(), 0);
  AppSettings empty(config);
  QVERIFY(empty.gogLibraryPaths().isEmpty());
  // A failed settings write must not report a folder as saved in memory.
  AppSettings unwritable(directory.path());
  QVERIFY(!unwritable.addGogLibraryPath(first));
  QVERIFY(unwritable.gogLibraryPaths().isEmpty());
}

void CoreTests::settingsPersistReducedMotionAndCacheLimit() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString path = directory.path() + QStringLiteral("/config.toml");
  {
    AppSettings settings(path);
    QVERIFY(!settings.closeAfterLaunch());
    QVERIFY(!settings.couchModeEnabled());
    QCOMPARE(settings.couchLibraryView(), QStringLiteral("detail"));
    settings.setReducedMotion(true);
    settings.setArtworkCacheLimitMb(512);
    settings.setSteamId(QStringLiteral("76561198000000000"));
    settings.setIgdbClientId(QStringLiteral("publicclient123"));
    settings.setSteamEnabled(false);
    settings.setLutrisEnabled(false);
    settings.setGogEnabled(false);
    settings.setFaugusEnabled(false);
    settings.setRetroArchEnabled(false);
    QVERIFY(settings.pcsx2AutoEnabled());
    QVERIFY(settings.ryujinxAutoEnabled());
    settings.setPcsx2Enabled(false);  // explicit: clears the auto flag
    settings.setRyujinxEnabled(false);
    settings.setBattleNetEnabled(false);
    settings.setCloseAfterLaunch(true);
    settings.setCouchModeEnabled(true);
    settings.setCouchLibraryView(QStringLiteral("grid"));
  }
  AppSettings reloaded(path);
  QVERIFY(reloaded.reducedMotion());
  QCOMPARE(reloaded.artworkCacheLimitMb(), 512);
  QCOMPARE(reloaded.steamId(), QStringLiteral("76561198000000000"));
  QCOMPARE(reloaded.igdbClientId(), QStringLiteral("publicclient123"));
  QVERIFY(!reloaded.steamEnabled());
  QVERIFY(!reloaded.lutrisEnabled());
  QVERIFY(reloaded.heroicEnabled());
  QVERIFY(!reloaded.gogEnabled());
  QVERIFY(!reloaded.faugusEnabled());
  QVERIFY(!reloaded.retroArchEnabled());
  QVERIFY(!reloaded.pcsx2Enabled());
  QVERIFY(!reloaded.ryujinxEnabled());
  QVERIFY(!reloaded.pcsx2AutoEnabled());  // explicit write cleared auto-detection
  QVERIFY(!reloaded.ryujinxAutoEnabled());
  QVERIFY(!reloaded.battleNetEnabled());
  QVERIFY(reloaded.closeAfterLaunch());
  QVERIFY(reloaded.couchModeEnabled());
  QCOMPARE(reloaded.couchLibraryView(), QStringLiteral("grid"));

  // A config without emulator keys keeps auto-detection pending and the keys absent
  // even after unrelated settings change.
  const QString autoPath = directory.path() + QStringLiteral("/auto.toml");
  {
    AppSettings settings(autoPath);
    settings.setReducedMotion(true);
  }
  QFile autoConfig(autoPath);
  QVERIFY(autoConfig.open(QIODevice::ReadOnly));
  const QString autoContents = QString::fromUtf8(autoConfig.readAll());
  autoConfig.close();
  QVERIFY(!autoContents.contains(QStringLiteral("pcsx2_enabled")));
  QVERIFY(!autoContents.contains(QStringLiteral("ryujinx_enabled")));
  AppSettings autoReloaded(autoPath);
  QVERIFY(autoReloaded.pcsx2AutoEnabled());
  QVERIFY(autoReloaded.ryujinxAutoEnabled());
  QVERIFY(!autoReloaded.pcsx2Enabled());
}

void CoreTests::launchKeysRoundTripAndResolveInstallations() {
  const LaunchKey heroic = LaunchKey::parse(QStringLiteral("Heroic:legendary:Sugar"));
  QVERIFY(heroic.isValid());
  QCOMPARE(heroic.source, QStringLiteral("Heroic"));
  QCOMPARE(heroic.runner, QStringLiteral("legendary"));
  QCOMPARE(heroic.appId, QStringLiteral("Sugar"));
  QCOMPARE(heroic.toString(), QStringLiteral("Heroic:legendary:Sugar"));
  // RetroArch ids are content paths, so everything after the second colon belongs to the id.
  const LaunchKey retro = LaunchKey::parse(QStringLiteral("RetroArch::/roms/Game: Two (USA).zip"));
  QCOMPARE(retro.runner, QString{});
  QCOMPARE(retro.appId, QStringLiteral("/roms/Game: Two (USA).zip"));
  QVERIFY(!LaunchKey::parse(QStringLiteral("Steam")).isValid());
  QVERIFY(!LaunchKey::parse(QStringLiteral("Steam:620")).isValid());
  QVERIFY(!LaunchKey::parse(QStringLiteral("::620")).isValid());

  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  MockGameModel games(nullptr, 3, true);
  UnifiedGameModel unified(directory.path() + QStringLiteral("/library.sqlite3"));
  unified.addSourceModel(&games);
  int row = -1;
  const QVariantMap found =
      PlayRequest::findInstallation(unified, LaunchKey::parse(QStringLiteral("demo::demo-1")), &row);
  QCOMPARE(row, 1);
  QCOMPARE(found.value(QStringLiteral("appId")).toString(), QStringLiteral("demo-1"));
  QCOMPARE(found.value(QStringLiteral("title")).toString(),
           unified.data(unified.index(1, 0), GameRoles::Title).toString());
  QVERIFY(PlayRequest::findInstallation(unified, LaunchKey::parse(QStringLiteral("Demo::nope")),
                                        &row)
              .isEmpty());
  QCOMPARE(row, -1);

  DelayedSteamModel delayedSource;
  UnifiedGameModel delayedUnified(QStringLiteral(":memory:"));
  delayedUnified.addSourceModel(&delayedSource);
  const LaunchKey delayedKey = LaunchKey::parse(QStringLiteral("Steam::620"));
  QTimer::singleShot(20, &delayedSource, [&delayedSource] { delayedSource.publish(); });
  QVERIFY(PlayRequest::waitForInstallation(delayedUnified, delayedKey, 1000));
  QCOMPARE(PlayRequest::findInstallation(delayedUnified, delayedKey, &row)
               .value(QStringLiteral("title"))
               .toString(),
           QStringLiteral("Portal 2"));

  GameLauncher launcher;
  QString error;
  QVERIFY(!PlayRequest::perform(unified, launcher, LaunchKey::parse(QStringLiteral("Steam::demo-0")),
                                &error));
  QVERIFY(error.contains(QStringLiteral("not installed")));
  QVERIFY(!PlayRequest::perform(unified, launcher, LaunchKey::parse(QStringLiteral("Demo::demo-1")),
                                &error));
  QCOMPARE(error, QStringLiteral("Demo games cannot be launched yet."));
  QVERIFY(!PlayRequest::perform(unified, launcher, LaunchKey::parse(QStringLiteral("bad")), &error));
  QVERIFY(error.contains(QStringLiteral("Steam::620")));
}

void CoreTests::singleInstanceForwardsPlayAndQuitCommands() {
  const QString name = QStringLiteral("omakade-test-") + QUuid::createUuid().toString();
  QVERIFY(!SingleInstance::sendCommand(name, "quit"));
  SingleInstance primary(name);
  QVERIFY(primary.claimOrNotify());
  QSignalSpy plays(&primary, &SingleInstance::playRequested);
  QSignalSpy quits(&primary, &SingleInstance::quitRequested);
  QSignalSpy activations(&primary, &SingleInstance::activationRequested);

  QVERIFY(SingleInstance::sendCommand(name, "play Steam::620"));
  QTRY_COMPARE_WITH_TIMEOUT(plays.size(), 1, 1000);
  QCOMPARE(plays.takeFirst().at(0).toString(), QStringLiteral("Steam::620"));
  SingleInstance secondary(name);
  QVERIFY(!secondary.claimOrNotify("play RetroArch::/roms/a b.zip"));
  QTRY_COMPARE_WITH_TIMEOUT(plays.size(), 1, 1000);
  QCOMPARE(plays.takeFirst().at(0).toString(), QStringLiteral("RetroArch::/roms/a b.zip"));
  QVERIFY(SingleInstance::sendCommand(name, "quit"));
  QTRY_COMPARE_WITH_TIMEOUT(quits.size(), 1, 1000);
  QCOMPARE(activations.size(), 0);
}

void CoreTests::sunshineIntegrationWritesOnlyItsOwnEntries() {
  QCOMPARE(SunshineIntegration::shellQuote(QStringLiteral("it's")), QStringLiteral("'it'\\''s'"));
  QCOMPARE(SunshineIntegration::commandPrefix(true),
           QStringLiteral("flatpak-spawn --host omakade"));
  const QString nativePrefix = SunshineIntegration::commandPrefix(false);
  QVERIFY(nativePrefix == QStringLiteral("omakade") ||
          nativePrefix.endsWith(QStringLiteral("/omakade'")));

  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString sunshineConfig = directory.path() + QStringLiteral("/sunshine.conf");
  writeFile(sunshineConfig, "output_name = 'DP-2' # streamed monitor\n");
  QCOMPARE(SunshineIntegration::configuredOutputName(sunshineConfig), QStringLiteral("DP-2"));
  const QStringList screens{QStringLiteral("HDMI-A-1"), QStringLiteral("DP-2")};
  QCOMPARE(SunshineIntegration::outputScreenIndex(QStringLiteral("DP-2"), screens), 1);
  QCOMPARE(SunshineIntegration::outputScreenIndex(QStringLiteral("0"), screens), 0);
  QCOMPARE(SunshineIntegration::outputScreenIndex(QString{}, screens), 0);
  QCOMPARE(SunshineIntegration::outputScreenIndex(QStringLiteral("missing"), screens), 0);
  QCOMPARE(SunshineIntegration::outputScreenIndex(QStringLiteral("9"), screens), 0);
  QCOMPARE(SunshineIntegration::outputScreenIndex(QString{}, {}), -1);
  const QString appsPath = directory.path() + QStringLiteral("/apps.json");
  const QByteArray stock = R"({
    "env": {"PATH": "$(PATH):$(HOME)/.local/bin"},
    "apps": [
        {"name": "Desktop", "image-path": "desktop.png"},
        {"name": "Steam Big Picture", "cmd": "", "detached": ["setsid steam steam://open/bigpicture"],
         "image-path": "steam.png", "prep-cmd": [{"do": "", "undo": "setsid steam steam://close/bigpicture"}]}
    ]
})";
  writeFile(appsPath, stock);
  const auto readApps = [&appsPath]() -> QJsonObject {
    QFile file(appsPath);
    if (!file.open(QIODevice::ReadOnly)) {
      return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
  };

  AppSettings settings(directory.path() + QStringLiteral("/config.toml"));
  MockGameModel games(nullptr, 3, true);
  UnifiedGameModel unified(directory.path() + QStringLiteral("/library.sqlite3"));
  unified.addSourceModel(&games);
  // Same title as the second demo game, from a source without an Installed role.
  const QString sharedTitle = unified.data(unified.index(1, 0), GameRoles::Title).toString();
  const QString coverPath = directory.path() + QStringLiteral("/celeste.jpg");
  QImage cover(300, 450, QImage::Format_RGB32);
  cover.fill(Qt::red);
  QVERIFY(cover.save(coverPath, "JPEG"));
  LauncherOnlyModel lutris(sharedTitle, coverPath);
  unified.addSourceModel(&lutris);
  const QString imageRoot = directory.path() + QStringLiteral("/images");
  SunshineIntegration sunshine(&unified, &settings, appsPath, imageRoot);
  QVERIFY(sunshine.detected());
  QVERIFY(!sunshine.restartNeeded());
  QCOMPARE(readApps().value(QStringLiteral("apps")).toArray().size(), 2);
  // The export runs on a worker thread; wait for it before reading the file.
  const auto settle = [&sunshine] { QTRY_VERIFY_WITH_TIMEOUT(!sunshine.busy(), 10000); };

  // The uninstalled first demo game stays out; the two installed demo games and the
  // launcher game (no Installed role) become apps.
  settings.setSunshineGameApps(true);
  settle();
  QJsonObject document = readApps();
  QJsonArray apps = document.value(QStringLiteral("apps")).toArray();
  QCOMPARE(apps.size(), 5);
  QCOMPARE(apps.at(0).toObject().value(QStringLiteral("name")).toString(),
           QStringLiteral("Desktop"));
  QCOMPARE(apps.at(1).toObject().value(QStringLiteral("prep-cmd")).toArray().size(), 1);
  QCOMPARE(document.value(QStringLiteral("env")).toObject().value(QStringLiteral("PATH")).toString(),
           QStringLiteral("$(PATH):$(HOME)/.local/bin"));
  const QJsonObject firstGame = apps.at(2).toObject();
  QCOMPARE(firstGame.value(QStringLiteral("omakade")).toString(), QStringLiteral("Demo::demo-1"));
  QCOMPARE(firstGame.value(QStringLiteral("cmd")).toString(), QString{});
  QCOMPARE(firstGame.value(QStringLiteral("detached")).toArray().at(0).toString(),
           SunshineIntegration::commandPrefix(false) + QStringLiteral(" --play 'Demo::demo-1'"));
  QVERIFY(!firstGame.contains(QStringLiteral("image-path")));
  // Two stores share a title, so both names carry their source.
  QCOMPARE(firstGame.value(QStringLiteral("name")).toString(), sharedTitle + QStringLiteral(" (Demo)"));
  const QJsonObject lutrisGame = apps.at(4).toObject();
  QCOMPARE(lutrisGame.value(QStringLiteral("name")).toString(),
           sharedTitle + QStringLiteral(" (Lutris)"));
  QCOMPARE(lutrisGame.value(QStringLiteral("omakade")).toString(), QStringLiteral("Lutris::celeste"));
  const QString boxArt = lutrisGame.value(QStringLiteral("image-path")).toString();
  QVERIFY(boxArt.startsWith(imageRoot));
  QCOMPARE(QImage(boxArt).size(), QSize(600, 800));
  QCOMPARE(apps.at(3).toObject().value(QStringLiteral("name")).toString(),
           unified.data(unified.index(2, 0), GameRoles::Title).toString());
  QCOMPARE(sunshine.exportedGames(), 3);
  QVERIFY(sunshine.restartNeeded());
  QVERIFY(QFileInfo::exists(appsPath + QStringLiteral(".omakade-backup")));

  settings.setSunshineOmakadeApp(true);
  settle();
  apps = readApps().value(QStringLiteral("apps")).toArray();
  QCOMPARE(apps.size(), 6);
  const QJsonObject omakade = apps.at(2).toObject();
  QCOMPARE(omakade.value(QStringLiteral("name")).toString(), QStringLiteral("Omakade"));
  QCOMPARE(omakade.value(QStringLiteral("detached")).toArray().at(0).toString(),
           nativePrefix);
  QCOMPARE(omakade.value(QStringLiteral("prep-cmd"))
               .toArray()
               .at(0)
               .toObject()
               .value(QStringLiteral("undo"))
               .toString(),
           nativePrefix + QStringLiteral(" --quit"));

  // A second sync with nothing changed leaves the file alone, even though Sunshine's own
  // formatting differs from Qt's.
  writeFile(appsPath, QJsonDocument(readApps()).toJson(QJsonDocument::Compact));
  const QDateTime before = QFileInfo(appsPath).lastModified();
  QVERIFY(sunshine.sync());
  settle();
  QCOMPARE(readApps().value(QStringLiteral("apps")).toArray().size(), 6);
  QCOMPARE(QFileInfo(appsPath).lastModified(), before);
  QCOMPARE(sunshine.statusText(), QStringLiteral("Sunshine app list is up to date"));

  // A linked game's exported launch target follows its saved default.
  QVERIFY(unified.linkGames(1, QStringLiteral("Lutris"), QString{}, QStringLiteral("celeste")));
  QVERIFY(unified.setPreferredInstallation(1, QStringLiteral("Lutris"), QString{},
                                           QStringLiteral("celeste")));
  QVERIFY(sunshine.sync());
  settle();
  apps = readApps().value(QStringLiteral("apps")).toArray();
  QCOMPARE(apps.size(), 5);
  bool exportedPreferred = false;
  for (const QJsonValue& value : apps) {
    const QString key = value.toObject().value(QStringLiteral("omakade")).toString();
    QVERIFY(key != QStringLiteral("Demo::demo-1"));
    if (key == QStringLiteral("Lutris::celeste")) {
      exportedPreferred = true;
    }
  }
  QVERIFY(exportedPreferred);

  settings.setSunshineGameApps(false);
  settings.setSunshineOmakadeApp(false);
  settle();
  apps = readApps().value(QStringLiteral("apps")).toArray();
  QCOMPARE(apps.size(), 2);
  QCOMPARE(apps.at(1).toObject().value(QStringLiteral("name")).toString(),
           QStringLiteral("Steam Big Picture"));
  QCOMPARE(sunshine.exportedGames(), 0);
  QVERIFY(QDir(imageRoot).entryList({QStringLiteral("*.png")}, QDir::Files).isEmpty());
}

void CoreTests::secondInstanceRequestsActivation() {
  const QString name = QStringLiteral("omakade-test-") + QUuid::createUuid().toString();
  SingleInstance primary(name);
  QVERIFY(primary.claimOrNotify());
  QSignalSpy activation(&primary, &SingleInstance::activationRequested);

  SingleInstance secondary(name);
  QVERIFY(!secondary.claimOrNotify());
  QTRY_COMPARE_WITH_TIMEOUT(activation.size(), 1, 1000);
}

void CoreTests::couchCursorFollowsInputMode() {
  QWindow window;
  window.setProperty("couchMode", false);
  CouchCursorManager cursor(&window, 30);

  QVERIFY(!cursor.cursorHidden());
  window.setProperty("couchMode", true);
  cursor.syncCouchMode();
  QVERIFY(!cursor.cursorHidden());
  QTRY_VERIFY_WITH_TIMEOUT(cursor.cursorHidden(), 250);

  QMouseEvent move(QEvent::MouseMove, QPointF(20, 20), QPointF(20, 20), Qt::NoButton, Qt::NoButton,
                   Qt::NoModifier);
  QCoreApplication::sendEvent(&window, &move);
  QVERIFY(!cursor.cursorHidden());

  cursor.navigationActivity();
  QVERIFY(cursor.cursorHidden());

  QMouseEvent stationaryMove(QEvent::MouseMove, QPointF(20, 20), QPointF(20, 20), Qt::NoButton,
                             Qt::NoButton, Qt::NoModifier);
  QCoreApplication::sendEvent(&window, &stationaryMove);
  QVERIFY(cursor.cursorHidden());

  QMouseEvent secondMove(QEvent::MouseMove, QPointF(24, 20), QPointF(24, 20), Qt::NoButton,
                         Qt::NoButton, Qt::NoModifier);
  QCoreApplication::sendEvent(&window, &secondMove);
  QVERIFY(!cursor.cursorHidden());

  QKeyEvent keyPress(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
  QCoreApplication::sendEvent(&window, &keyPress);
  QVERIFY(cursor.cursorHidden());

  window.setProperty("couchMode", false);
  cursor.syncCouchMode();
  QVERIFY(!cursor.cursorHidden());
  QTest::qWait(60);
  QVERIFY(!cursor.cursorHidden());
}

void CoreTests::virtualControllerConnectsAndMapsPrimaryButton() {
  QVERIFY(SDL_Init(SDL_INIT_GAMEPAD));
  SDL_VirtualJoystickDesc description;
  SDL_INIT_INTERFACE(&description);
  description.type = SDL_JOYSTICK_TYPE_GAMEPAD;
  description.naxes = SDL_GAMEPAD_AXIS_COUNT;
  description.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
  description.button_mask = (1U << SDL_GAMEPAD_BUTTON_SOUTH) | (1U << SDL_GAMEPAD_BUTTON_WEST) |
                            (1U << SDL_GAMEPAD_BUTTON_NORTH) | (1U << SDL_GAMEPAD_BUTTON_DPAD_UP) |
                            (1U << SDL_GAMEPAD_BUTTON_DPAD_DOWN) |
                            (1U << SDL_GAMEPAD_BUTTON_DPAD_LEFT) |
                            (1U << SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
  description.axis_mask = (1U << SDL_GAMEPAD_AXIS_LEFTX) | (1U << SDL_GAMEPAD_AXIS_LEFTY);
  description.name = "Omakade test controller";
  const SDL_JoystickID id = SDL_AttachVirtualJoystick(&description);
  QVERIFY2(id != 0, SDL_GetError());

  // A real controller may be in use while the offscreen suite runs.
  std::atomic<SDL_JoystickID> testControllerId{id};
  SDL_SetEventFilter([](void* context, SDL_Event* event) {
    const auto allowed = static_cast<std::atomic<SDL_JoystickID>*>(context)->load();
    if (event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
        event->type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
      return event->gbutton.which == allowed;
    }
    if (event->type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
      return event->gaxis.which == allowed;
    }
    return true;
  }, &testControllerId);
  const auto clearEventFilter = qScopeGuard([] { SDL_SetEventFilter(nullptr, nullptr); });

  ControllerInput controller;
  controller.start();
  QTRY_VERIFY_WITH_TIMEOUT(controller.connected(), 1000);
  const int connectedCount = controller.controllerCount();
  QSignalSpy keys(&controller, &ControllerInput::keyRequested);
  QSignalSpy focusDirections(&controller, &ControllerInput::focusDirectionRequested);
  QSignalSpy favorites(&controller, &ControllerInput::favoriteRequested);
  QSignalSpy toolbar(&controller, &ControllerInput::toolbarRequested);
  SDL_Joystick* joystick = SDL_OpenJoystick(id);
  QVERIFY(joystick != nullptr);
  QVERIFY(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_SOUTH, true));
  SDL_UpdateJoysticks();
  QTRY_VERIFY_WITH_TIMEOUT(!keys.isEmpty(), 1000);
  QCOMPARE(keys.first().at(0).toInt(), static_cast<int>(Qt::Key_Return));

  SDL_Event favorite{};
  favorite.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
  favorite.gbutton.which = id;
  favorite.gbutton.button = SDL_GAMEPAD_BUTTON_WEST;
  QVERIFY(SDL_PushEvent(&favorite));
  QTRY_COMPARE_WITH_TIMEOUT(favorites.size(), 1, 1000);

  SDL_Event controls{};
  controls.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
  controls.gbutton.which = id;
  controls.gbutton.button = SDL_GAMEPAD_BUTTON_NORTH;
  QVERIFY(SDL_PushEvent(&controls));
  QTRY_COMPARE_WITH_TIMEOUT(toolbar.size(), 1, 1000);

  keys.clear();
  QVERIFY(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, 20000));
  SDL_UpdateJoysticks();
  QTRY_VERIFY_WITH_TIMEOUT(!keys.isEmpty(), 1000);
  QCOMPARE(keys.first().at(0).toInt(), static_cast<int>(Qt::Key_Right));

  keys.clear();
  controller.setFocusNavigation(true);
  QVERIFY(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, 0));
  SDL_UpdateJoysticks();
  QVERIFY(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, -20000));
  SDL_UpdateJoysticks();
  QTRY_VERIFY_WITH_TIMEOUT(!focusDirections.isEmpty(), 1000);
  QCOMPARE(focusDirections.first().at(0).toInt(), static_cast<int>(Qt::Key_Left));
  QVERIFY(keys.isEmpty());

  focusDirections.clear();
  QVERIFY(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, 0));
  QVERIFY(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTY, -20000));
  SDL_UpdateJoysticks();
  QTRY_VERIFY_WITH_TIMEOUT(!focusDirections.isEmpty(), 1000);
  QCOMPARE(focusDirections.first().at(0).toInt(), static_cast<int>(Qt::Key_Up));

  focusDirections.clear();
  QVERIFY(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTY, 0));
  SDL_UpdateJoysticks();
  QVERIFY(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTY, 20000));
  SDL_UpdateJoysticks();
  QTRY_VERIFY_WITH_TIMEOUT(!focusDirections.isEmpty(), 1000);
  QCOMPARE(focusDirections.first().at(0).toInt(), static_cast<int>(Qt::Key_Down));

  QVERIFY(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTY, 0));
  SDL_UpdateJoysticks();
  QTest::qWait(30);
  focusDirections.clear();
  SDL_Event dpadDown{};
  dpadDown.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
  dpadDown.gbutton.which = id;
  dpadDown.gbutton.button = SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
  QVERIFY(SDL_PushEvent(&dpadDown));
  QTRY_VERIFY_WITH_TIMEOUT(focusDirections.size() >= 3, 700);
  for (const QList<QVariant>& direction : std::as_const(focusDirections)) {
    QCOMPARE(direction.at(0).toInt(), static_cast<int>(Qt::Key_Right));
  }

  SDL_Event dpadUp{};
  dpadUp.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
  dpadUp.gbutton.which = id;
  dpadUp.gbutton.button = SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
  QVERIFY(SDL_PushEvent(&dpadUp));
  QTest::qWait(50);
  const qsizetype directionsAfterRelease = focusDirections.size();
  QTest::qWait(300);
  QCOMPARE(focusDirections.size(), directionsAfterRelease);

  focusDirections.clear();
  dpadDown.gbutton.button = SDL_GAMEPAD_BUTTON_DPAD_LEFT;
  QVERIFY(SDL_PushEvent(&dpadDown));
  QTRY_VERIFY_WITH_TIMEOUT(focusDirections.size() >= 3, 700);
  for (const QList<QVariant>& direction : std::as_const(focusDirections)) {
    QCOMPARE(direction.at(0).toInt(), static_cast<int>(Qt::Key_Left));
  }
  dpadUp.gbutton.button = SDL_GAMEPAD_BUTTON_DPAD_LEFT;
  QVERIFY(SDL_PushEvent(&dpadUp));
  QTest::qWait(50);
  const qsizetype leftDirectionsAfterRelease = focusDirections.size();
  QTest::qWait(300);
  QCOMPARE(focusDirections.size(), leftDirectionsAfterRelease);

  // Losing focus must cancel held navigation and suppress every controller action.
  QVERIFY(SDL_PushEvent(&dpadDown));
  QTRY_VERIFY_WITH_TIMEOUT(focusDirections.size() > leftDirectionsAfterRelease, 700);
  controller.setInputEnabled(false);
  keys.clear();
  focusDirections.clear();
  favorites.clear();
  toolbar.clear();
  for (int button : {SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST,
                     SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_WEST,
                     SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_DPAD_RIGHT}) {
    SDL_Event backgroundButton{};
    backgroundButton.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    backgroundButton.gbutton.which = id;
    backgroundButton.gbutton.button = button;
    QVERIFY(SDL_PushEvent(&backgroundButton));
  }
  QVERIFY(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, 20000));
  SDL_UpdateJoysticks();
  QTest::qWait(400);
  QVERIFY(keys.isEmpty());
  QVERIFY(focusDirections.isEmpty());
  QVERIFY(favorites.isEmpty());
  QVERIFY(toolbar.isEmpty());

  // Input already queued on return must not be replayed, nor may an old hold resume.
  QVERIFY(SDL_PushEvent(&favorite));
  controller.setInputEnabled(true);
  QTest::qWait(350);
  QVERIFY(keys.isEmpty());
  QVERIFY(focusDirections.isEmpty());
  QVERIFY(favorites.isEmpty());
  QVERIFY(toolbar.isEmpty());
  QVERIFY(SDL_PushEvent(&favorite));
  QTRY_COMPARE_WITH_TIMEOUT(favorites.size(), 1, 1000);

  SDL_CloseJoystick(joystick);
  SDL_Event removed{};
  removed.type = SDL_EVENT_GAMEPAD_REMOVED;
  removed.gdevice.which = id;
  QVERIFY(SDL_PushEvent(&removed));
  QVERIFY(SDL_DetachVirtualJoystick(id));
  QTRY_COMPARE_WITH_TIMEOUT(controller.controllerCount(), connectedCount - 1, 1000);
  keys.clear();
  QTest::qWait(400);
  QVERIFY(keys.isEmpty());

  const SDL_JoystickID reconnectedId = SDL_AttachVirtualJoystick(&description);
  QVERIFY2(reconnectedId != 0, SDL_GetError());
  testControllerId.store(reconnectedId);
  QTRY_COMPARE_WITH_TIMEOUT(controller.controllerCount(), connectedCount, 1000);
  SDL_Joystick* reconnectedJoystick = SDL_OpenJoystick(reconnectedId);
  QVERIFY(reconnectedJoystick != nullptr);
  QVERIFY(SDL_SetJoystickVirtualButton(reconnectedJoystick, SDL_GAMEPAD_BUTTON_SOUTH, true));
  SDL_UpdateJoysticks();
  QTRY_VERIFY_WITH_TIMEOUT(!keys.isEmpty(), 1000);
  QCOMPARE(keys.first().at(0).toInt(), static_cast<int>(Qt::Key_Return));
  SDL_CloseJoystick(reconnectedJoystick);
  QVERIFY(SDL_DetachVirtualJoystick(reconnectedId));
  QTRY_COMPARE_WITH_TIMEOUT(controller.controllerCount(), connectedCount - 1, 1000);
  SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}

void CoreTests::thousandGameSearchStaysResponsive() {
  MockGameModel games(nullptr, 1000);
  LibraryFilterModel library;
  library.setSourceModel(&games);
  QElapsedTimer timer;
  timer.start();
  for (int index = 0; index < 100; ++index) {
    library.setSearchText(QString::number(index));
    (void)library.rowCount();
  }
  QVERIFY2(timer.elapsed() < 1000,
           qPrintable(QStringLiteral("100 searches took %1 ms").arg(timer.elapsed())));
}


void CoreTests::pcsx2ScannerImportsCacheGamesAndPlaytime() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/pcsx2");
  const QString flatpakRoot =
      directory.path() + QStringLiteral("/.var/app/net.pcsx2.PCSX2/config/PCSX2");
  createPcsx2Fixture(root);
  createPcsx2Fixture(flatpakRoot, 0);

  const Pcsx2ScanResult result = Pcsx2Scanner::scan({root, flatpakRoot});
  QVERIFY(!result.incomplete);
  QCOMPARE(result.roots, QStringList({root, flatpakRoot}));
  // The same serial appearing in both roots dedupes to the first discovery.
  QCOMPARE(result.games.size(), 1);
  QCOMPARE(result.games.constFirst().title, QStringLiteral("Crash Twinsanity"));
  QCOMPARE(result.games.constFirst().serial, QStringLiteral("SLUS-20909"));
  QCOMPARE(result.games.constFirst().region, QStringLiteral("NTSC-U"));
  QCOMPARE(result.games.constFirst().playtimeSeconds, 14);
  QCOMPARE(result.games.constFirst().lastPlayed, 1700000500);
  QVERIFY(result.games.constFirst().path.endsWith(QStringLiteral(".iso")));
  QVERIFY(!result.games.constFirst().isElf);
  QVERIFY(!result.games.constFirst().flatpak);

  // A second ROM without a cache entry (never scanned by PCSX2) must not appear.
  writeFile(root + QStringLiteral("/roms/Unscanned (USA).chd"), "rom");
  const Pcsx2ScanResult dropped = Pcsx2Scanner::scan({root});
  QCOMPARE(dropped.games.size(), 1);
}

void CoreTests::pcsx2ModelIsRepeatableAndPreservesLocalState() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/pcsx2");
  const QString database = directory.path() + QStringLiteral("/omakade.sqlite3");
  createPcsx2Fixture(root);

  Pcsx2GameModel model(database);
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 1);
  QCOMPARE(model.detectedPaths(), QStringList({root}));
  QVERIFY(model.lastScan() > 0);
  QCOMPARE(model.data(model.index(0), GameRoles::Source).toString(), QStringLiteral("PCSX2"));
  QCOMPARE(model.data(model.index(0), GameRoles::Hours).toInt(), 0);
  model.toggleFavorite(0);
  model.toggleHidden(0);
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 1);
  QVERIFY(model.data(model.index(0), GameRoles::Favorite).toBool());
  QVERIFY(model.data(model.index(0), GameRoles::Hidden).toBool());

  Pcsx2GameModel reloaded(database);
  QCOMPARE(reloaded.detectedPaths(), QStringList({root}));
  QCOMPARE(reloaded.lastScan(), model.lastScan());
}

void CoreTests::malformedPcsx2DataDoesNotReplaceCachedGames() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/pcsx2");
  createPcsx2Fixture(root);
  Pcsx2GameModel model(directory.path() + QStringLiteral("/omakade.sqlite3"));
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 1);
  writeFile(root + QStringLiteral("/cache/gamelist.cache"), "not a cache");
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 1);
  QVERIFY(model.statusText().startsWith(QStringLiteral("PCSX2 scan interrupted")));

  // A 0xFFFFFFFF length must be rejected before int narrowing.
  createPcsx2Fixture(root);
  QFile cacheFile(root + QStringLiteral("/cache/gamelist.cache"));
  QVERIFY(cacheFile.open(QIODevice::ReadOnly));
  QByteArray malformedCache = cacheFile.readAll();
  cacheFile.close();
  QVERIFY(malformedCache.size() > 12);
  // Overwrite the first string length (path) with 0xFFFFFFFF.
  malformedCache[8] = '\xff';
  malformedCache[9] = '\xff';
  malformedCache[10] = '\xff';
  malformedCache[11] = '\xff';
  writeFile(root + QStringLiteral("/cache/gamelist.cache"), malformedCache);
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 1);
  QVERIFY(model.statusText().startsWith(QStringLiteral("PCSX2 scan interrupted")));
}

void CoreTests::pcsx2UnifiedFilterShowsGames() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/pcsx2");
  createPcsx2Fixture(root);
  Pcsx2GameModel model(directory.path() + QStringLiteral("/omakade.sqlite3"));
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 1);
  UnifiedGameModel games;
  games.addSourceModel(&model);
  LibraryFilterModel library;
  library.setSourceModel(&games);
  QCOMPARE(library.rowCount(), 1);
  library.setSourceFilter(QStringLiteral("PCSX2"));
  QCOMPARE(library.rowCount(), 1);
  QCOMPARE(library.get(0).value(QStringLiteral("title")).toString(),
           QStringLiteral("Crash Twinsanity"));
  library.setSourceFilter(QStringLiteral("Ryujinx"));
  QCOMPARE(library.rowCount(), 0);
}

void CoreTests::pcsx2LauncherBuildsSafeCommands() {
  // Serial ids are not launchable: PCSX2 boots disc images by path.
  QVERIFY(!GameLauncher::pcsx2Command(QStringLiteral("SLUS-20909"), false, false).isValid());
  QVERIFY(!GameLauncher::pcsx2Command(QStringLiteral("SCUS-97399"), false, false).isValid());
  const LaunchCommand native =
      GameLauncher::pcsx2Command(QStringLiteral("path:/games/Crash.iso"), false, false);
  QCOMPARE(native.program, QStringLiteral("pcsx2-qt"));
  QCOMPARE(native.arguments, QStringList({QStringLiteral("/games/Crash.iso")}));
  const LaunchCommand flatpak =
      GameLauncher::pcsx2Command(QStringLiteral("path:/games/Crash.iso"), false, true);
  QCOMPARE(flatpak.program, QStringLiteral("flatpak"));
  QCOMPARE(flatpak.arguments.at(1), QStringLiteral("net.pcsx2.PCSX2"));
  QCOMPARE(flatpak.arguments.constLast(), QStringLiteral("/games/Crash.iso"));
  // ELF entries must receive -elf <file>.
  const LaunchCommand elf =
      GameLauncher::pcsx2Command(QStringLiteral("path:/games/homebrew.elf"), true, false);
  QCOMPARE(elf.arguments,
           QStringList({QStringLiteral("-elf"), QStringLiteral("/games/homebrew.elf")}));
  QVERIFY(!GameLauncher::pcsx2Command(QStringLiteral("bad;id"), false, false).isValid());
}

void CoreTests::ryujinxScannerImportsRomsMetadataAndPlaytime() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/ryujinx");
  const QString roms = directory.path() + QStringLiteral("/switch-roms");
  createRyujinxFixture(root, roms);

  const RyujinxScanResult result = RyujinxScanner::scan({root});
  QVERIFY(!result.incomplete);
  QCOMPARE(result.roots, QStringList({root}));
  QCOMPARE(result.games.size(), 1);
  QCOMPARE(result.games.constFirst().titleId, QStringLiteral("0100ABCD12345678"));
  QCOMPARE(result.games.constFirst().title, QStringLiteral("Custom Title"));
  QCOMPARE(result.games.constFirst().playtimeSeconds, 3600);
  QVERIFY(result.games.constFirst().lastPlayed > 0);
  QVERIFY(!result.games.constFirst().flatpak);
}

void CoreTests::ryujinxScannerSkipsConfiguredAddOnsAndUpdates() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/ryujinx");
  const QString roms = directory.path() + QStringLiteral("/switch-roms");
  const QString base = roms + QStringLiteral("/Game [0100ABCD12346000].nsp");
  const QString configuredUpdate =
      roms + QStringLiteral("/Game Update [0100ABCD12346000][v65536].nsp");
  const QString configuredDlc = roms + QStringLiteral("/Game DLC [0100ABCD12347001].nsp");
  const QString titleIdUpdate = roms + QStringLiteral("/Game Patch [0100ABCD12346800].nsp");
  const QString combinedCartridge = roms + QStringLiteral("/Game Bundle [0100ABCD12347800].xci");
  const QString unclassifiedPackage =
      roms + QStringLiteral("/Bonus Pack [0100ABCD12347002].nsp");
  writeFile(root + QStringLiteral("/Config.json"),
            QStringLiteral("{\"version\":70,\"game_dirs\":[\"%1\"]}").arg(roms).toUtf8());
  writeFile(base, "base");
  writeFile(configuredUpdate, "update");
  writeFile(configuredDlc, "dlc");
  writeFile(titleIdUpdate, "update");
  writeFile(combinedCartridge, "combined");
  writeFile(unclassifiedPackage, "ambiguous");
  writeFile(root + QStringLiteral("/games/0100ABCD12346000/updates.json"),
            QStringLiteral("{\"paths\":[\"%1\"],\"selected\":\"%1\"}")
                .arg(configuredUpdate)
                .toUtf8());
  writeFile(root + QStringLiteral("/games/0100ABCD12346000/dlc.json"),
            QStringLiteral("[{\"path\":\"%1\"}]").arg(configuredDlc).toUtf8());
  writeFile(root + QStringLiteral("/games/0100ABCD12346000/gui/metadata.json"),
            "{\"timespan_played\":\"1.06:00:00.5000000\"}");
  writeFile(root + QStringLiteral("/games/0100ABCD12347002/gui/metadata.json"),
            "{\"time_played\":42}");

  const RyujinxScanResult result = RyujinxScanner::scan({root});
  QVERIFY(!result.incomplete);
  QCOMPARE(result.games.size(), 3);
  QStringList paths;
  for (const RyujinxGameRecord& game : result.games) {
    paths.append(game.path);
  }
  QVERIFY(paths.contains(base));
  QVERIFY(paths.contains(combinedCartridge));
  QVERIFY(paths.contains(unclassifiedPackage));
  QVERIFY(!paths.contains(configuredUpdate));
  QVERIFY(!paths.contains(configuredDlc));
  QVERIFY(!paths.contains(titleIdUpdate));
  for (const RyujinxGameRecord& game : result.games) {
    if (game.path == base) {
      QCOMPARE(game.playtimeSeconds, qint64(108000));
    } else if (game.path == unclassifiedPackage) {
      QCOMPARE(game.playtimeSeconds, qint64(42));
    }
  }
}

void CoreTests::ryujinxModelIsRepeatableAndPreservesLocalState() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root =
      directory.path() + QStringLiteral("/.var/app/org.ryujinx.Ryujinx/config/Ryujinx");
  const QString roms = directory.path() + QStringLiteral("/switch-roms");
  const QString database = directory.path() + QStringLiteral("/omakade.sqlite3");
  createRyujinxFixture(root, roms);

  RyujinxGameModel model(database);
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 1);
  QCOMPARE(model.detectedPaths(), QStringList({root}));
  QVERIFY(model.lastScan() > 0);
  QCOMPARE(model.data(model.index(0), GameRoles::Source).toString(), QStringLiteral("Ryujinx"));
  QCOMPARE(model.data(model.index(0), GameRoles::Runner).toString(),
           QStringLiteral("org.ryujinx.Ryujinx"));
  model.toggleFavorite(0);
  model.toggleHidden(0);
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 1);
  QVERIFY(model.data(model.index(0), GameRoles::Favorite).toBool());
  QVERIFY(model.data(model.index(0), GameRoles::Hidden).toBool());
  RyujinxGameModel reloaded(database);
  QCOMPARE(reloaded.rowCount(), 1);
  QVERIFY(reloaded.data(reloaded.index(0), GameRoles::Favorite).toBool());
  QCOMPARE(reloaded.data(reloaded.index(0), GameRoles::Runner).toString(),
           QStringLiteral("org.ryujinx.Ryujinx"));
}

void CoreTests::malformedRyujinxDataDoesNotReplaceCachedGames() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/ryujinx");
  const QString roms = directory.path() + QStringLiteral("/switch-roms");
  createRyujinxFixture(root, roms);
  RyujinxGameModel model(directory.path() + QStringLiteral("/omakade.sqlite3"));
  model.refreshFromRoots({root});
  QVERIFY2(model.rowCount() == 1,
           qPrintable(model.statusText() + QStringLiteral(": ") + model.errorText()));
  writeFile(root + QStringLiteral("/Config.json"), "not json");
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 1);
  QVERIFY(model.statusText().startsWith(QStringLiteral("Ryujinx scan interrupted")));

  // A configured directory that disappeared must keep the cached library.
  writeFile(root + QStringLiteral("/Config.json"),
            QStringLiteral("{\"version\":70,\"game_dirs\":[\"%1/missing\"]}")
                .arg(roms)
                .toUtf8());
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 1);
  QVERIFY(model.statusText().startsWith(QStringLiteral("Ryujinx scan interrupted")));
}

void CoreTests::ryujinxLauncherBuildsSafeCommands() {
  // Title ids are display metadata only: Ryujinx launches ROM file paths.
  QVERIFY(!GameLauncher::ryujinxCommand(QStringLiteral("0100ABCD12345678"),
                                        QStringLiteral("ryujinx-wrapper"))
               .isValid());
  const LaunchCommand wrapper =
      GameLauncher::ryujinxCommand(QStringLiteral("/roms/Zelda.nsp"), QStringLiteral("ryujinx-wrapper"));
  QCOMPARE(wrapper.program, QStringLiteral("ryujinx-wrapper"));
  QCOMPARE(wrapper.arguments, QStringList({QStringLiteral("/roms/Zelda.nsp")}));
  // Native builds may ship the binary under different names.
  const LaunchCommand native =
      GameLauncher::ryujinxCommand(QStringLiteral("/roms/Zelda.nsp"), QStringLiteral("Ryujinx"));
  QCOMPARE(native.program, QStringLiteral("Ryujinx"));
  QCOMPARE(native.arguments, QStringList({QStringLiteral("/roms/Zelda.nsp")}));
  const LaunchCommand flatpak =
      GameLauncher::ryujinxCommand(QStringLiteral("path:/roms/Zelda.nsp"), QString{});
  QCOMPARE(flatpak.program, QStringLiteral("flatpak"));
  QCOMPARE(flatpak.arguments.at(1), QStringLiteral("io.github.ryubing.Ryujinx"));
  QCOMPARE(flatpak.arguments.constLast(), QStringLiteral("/roms/Zelda.nsp"));
  const LaunchCommand legacyFlatpak = GameLauncher::ryujinxCommand(
      QStringLiteral("path:/roms/Zelda.nsp"), QString{}, QStringLiteral("org.ryujinx.Ryujinx"));
  QCOMPARE(legacyFlatpak.arguments.at(1), QStringLiteral("org.ryujinx.Ryujinx"));
  QVERIFY(!GameLauncher::ryujinxCommand(QStringLiteral("bad;id"), QStringLiteral("Ryujinx")).isValid());
  QVERIFY(!GameLauncher::ryujinxCommand(QStringLiteral("0100abcd12345678"), QStringLiteral("Ryujinx"))
               .isValid());
  // Paths with commas, plus signs, and hashes stay launchable.
  QVERIFY(GameLauncher::ryujinxCommand(
              QStringLiteral("/roms/Zelda, Part 2 + [dlc #3].nsp"), QStringLiteral("Ryujinx"))
              .isValid());
  QVERIFY(GameLauncher::ryujinxCommand(
              QStringLiteral("/roms/Zelda: Tears @ Midnight [0100ABCD12345678].xci"),
              QStringLiteral("Ryujinx"))
              .isValid());
  QVERIFY(!GameLauncher::ryujinxCommand(QStringLiteral("/roms/totally-not-a-rom.txt"),
                                        QStringLiteral("Ryujinx"))
               .isValid());
}

QTEST_MAIN(CoreTests)
#include "CoreTests.moc"
