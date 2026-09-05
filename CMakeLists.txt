cmake_minimum_required(VERSION 3.24)

project(Omakade VERSION 1.6.1 LANGUAGES CXX)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
pkg_check_modules(LIBSECRET REQUIRED IMPORTED_TARGET libsecret-1)
pkg_check_modules(LIBZIP REQUIRED IMPORTED_TARGET libzip)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Concurrent Core Gui Network Qml Quick QuickControls2 Sql Test)
qt_standard_project_setup(REQUIRES 6.8)

add_library(omakade_core STATIC
  src/achievements/AchievementModel.cpp
  src/achievements/AchievementModel.h
  src/achievements/RetroAchievementsApi.cpp
  src/achievements/RetroAchievementsApi.h
  src/achievements/RetroAchievementsHasher.cpp
  src/achievements/RetroAchievementsHasher.h
  src/achievements/RetroAchievementsService.cpp
  src/achievements/RetroAchievementsService.h
  src/achievements/SteamAchievementApi.cpp
  src/achievements/SteamAchievementApi.h
  src/achievements/SteamAccountService.cpp
  src/achievements/SteamAccountService.h
  src/app/AppSettings.cpp
  src/app/AppSettings.h
  src/app/SingleInstance.cpp
  src/app/SingleInstance.h
  src/input/ControllerInput.cpp
  src/input/ControllerInput.h
  src/input/CouchCursorManager.cpp
  src/input/CouchCursorManager.h
  src/library/LibraryFilterModel.cpp
  src/library/LibraryFilterModel.h
  src/library/GameRoles.h
  src/library/BattleNetGameModel.cpp
  src/library/BattleNetGameModel.h
  src/library/FaugusGameModel.cpp
  src/library/FaugusGameModel.h
  src/library/HeroicGameModel.cpp
  src/library/HeroicGameModel.h
  src/library/LutrisGameModel.cpp
  src/library/LutrisGameModel.h
  src/library/RetroArchGameModel.cpp
  src/library/RetroArchGameModel.h
  src/library/Pcsx2GameModel.cpp
  src/library/Pcsx2GameModel.h
  src/library/RyujinxGameModel.cpp
  src/library/RyujinxGameModel.h
  src/library/MockGameModel.cpp
  src/library/MockGameModel.h
  src/library/SteamGameModel.cpp
  src/library/SteamGameModel.h
  src/library/SteamOwnedGamesApi.cpp
  src/library/SteamOwnedGamesApi.h
  src/library/UnifiedGameModel.cpp
  src/library/UnifiedGameModel.h
  src/launch/GameLauncher.cpp
  src/launch/GameLauncher.h
  src/launch/LaunchCommand.h
  src/launch/PlayRequest.cpp
  src/launch/PlayRequest.h
  src/launch/SteamLauncher.cpp
  src/launch/SteamLauncher.h
  src/metadata/IgdbApi.cpp
  src/metadata/IgdbApi.h
  src/metadata/GameInsightsService.cpp
  src/metadata/GameInsightsService.h
  src/sources/FlatpakInstall.h
  src/sources/steam/SteamScanner.cpp
  src/sources/steam/SteamScanner.h
  src/sources/steam/ValveKeyValues.cpp
  src/sources/steam/ValveKeyValues.h
  src/sources/lutris/LutrisScanner.cpp
  src/sources/lutris/LutrisScanner.h
  src/sources/retroarch/RetroArchScanner.cpp
  src/sources/retroarch/RetroArchScanner.h
  src/sources/pcsx2/Pcsx2Scanner.cpp
  src/sources/pcsx2/Pcsx2Scanner.h
  src/sources/ryujinx/RyujinxScanner.cpp
  src/sources/ryujinx/RyujinxScanner.h
  src/sources/heroic/HeroicScanner.cpp
  src/sources/heroic/HeroicScanner.h
  src/sources/faugus/FaugusScanner.cpp
  src/sources/faugus/FaugusScanner.h
  src/sources/battlenet/BattleNetScanner.cpp
  src/sources/battlenet/BattleNetScanner.h
  src/streaming/SunshineIntegration.cpp
  src/streaming/SunshineIntegration.h
  src/theme/OmarchyTheme.cpp
  src/theme/OmarchyTheme.h
)

target_include_directories(omakade_core PUBLIC src)
target_link_libraries(omakade_core PUBLIC
  PkgConfig::SDL3
  PkgConfig::LIBSECRET
  PkgConfig::LIBZIP
  Qt6::Concurrent
  Qt6::Core
  Qt6::Gui
  Qt6::Network
  Qt6::Sql
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(omakade_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

qt_add_executable(omakade
  src/app/main.cpp
)

qt_add_qml_module(omakade
  URI Omakade
  VERSION 1.0
  QML_FILES
    qml/Main.qml
    qml/components/GameCard.qml
    qml/components/CouchLibraryView.qml
    qml/components/CouchKeyboard.qml
    qml/components/CouchBrowsePanel.qml
    qml/components/GlassButton.qml
    qml/components/LibraryView.qml
    qml/screens/GameDetails.qml
)

qt_add_resources(omakade "omakade_icons"
  PREFIX "/icons"
  FILES
    resources/icons/io.github.tsouth89.Omakade.svg
)

set_target_properties(omakade PROPERTIES
  OUTPUT_NAME omakade
  MACOSX_BUNDLE_GUI_IDENTIFIER io.github.tsouth89.Omakade
  WIN32_EXECUTABLE TRUE
)

target_compile_definitions(omakade PRIVATE
  OMAKADE_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(omakade PRIVATE
  omakade_core
  Qt6::Core
  Qt6::Gui
  Qt6::Qml
  Qt6::Quick
  Qt6::QuickControls2
)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(omakade PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(GNUInstallDirs)
install(TARGETS omakade
  BUNDLE DESTINATION .
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES packaging/io.github.tsouth89.Omakade.desktop
  DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)
install(FILES resources/icons/io.github.tsouth89.Omakade.svg
  DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
)
install(FILES packaging/io.github.tsouth89.Omakade.metainfo.xml
  DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo
)
install(FILES LICENSE COPYRIGHT
  DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/omakade
)
install(FILES README.md PRIVACY.md SUPPORT.md CHANGELOG.md docs/COMPATIBILITY.md
  DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/omakade
)

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
