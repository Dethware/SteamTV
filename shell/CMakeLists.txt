cmake_minimum_required(VERSION 3.21)

project(tvshell
    VERSION 0.1.0
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Pinned to what SteamOS 3.9.5 ships (see toolchains/steamos-3.9).
find_package(Qt6 6.11 REQUIRED COMPONENTS DBus Gui Multimedia Network Quick)
find_package(SDL3 3.4 REQUIRED CONFIG)
find_package(PkgConfig REQUIRED)
pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb xcb-xtest)
pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse)
find_package(Threads REQUIRED)
# Reading Switch game files (switch/format).
find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto)
find_package(ZLIB REQUIRED)

qt_standard_project_setup(REQUIRES 6.11)

# All QML modules in one place in the build tree, under their URIs (TvShell/Photos, …),
# so that the tools find them; at run time they come from the binary's resources.
set(QT_QML_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml)

# Vendored (3rdparty/miniaudio/README.md). Audio backends are loaded at runtime.
add_library(miniaudio STATIC
    3rdparty/miniaudio/miniaudio.c
)
target_include_directories(miniaudio
    SYSTEM PUBLIC
        3rdparty/miniaudio
)
target_link_libraries(miniaudio
    PRIVATE
        Threads::Threads
        ${CMAKE_DL_LIBS}
        m
)

# Code the shell shares with the programs it shows (../shared).
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../shared shared)

# One module per topic, each a static library with its C++ and QML (see DEVELOPMENT.md).
add_subdirectory(common)
add_subdirectory(session)
add_subdirectory(power)
# Before apps, whose Home shows a Switch game's icon.
add_subdirectory(switch)
add_subdirectory(apps)
add_subdirectory(audio)
add_subdirectory(photos)
add_subdirectory(steam)
add_subdirectory(youtube)
add_subdirectory(controllers)
add_subdirectory(assistant)

qt_add_executable(tvshell
    main.cpp
)

# The shell itself: what assembles the topics into the screen.
qt_add_qml_module(tvshell
    URI TvShell
    QML_FILES
        Main.qml
        TopBar.qml
        SettingsPage.qml
    DEPENDENCIES
        TARGET tvshell_common
        TARGET tvshell_session
        TARGET tvshared_input
        TARGET tvshell_power
        TARGET tvshell_audio
        TARGET tvshell_photos
        TARGET tvshell_steam
        TARGET tvshell_switch
        TARGET tvshell_apps
        TARGET tvshell_youtube
        TARGET tvshell_controllers
        TARGET tvshell_assistant
)

target_link_libraries(tvshell
    PRIVATE
        Qt6::Gui
        Qt6::Quick
        PkgConfig::XCB
        # main.cpp sets up the network layer with the photos' rule.
        tvshell_common
        tvshell_photos
        tvshell_commonplugin
        tvshell_sessionplugin
        tvshared_inputplugin
        tvshell_powerplugin
        tvshell_audioplugin
        tvshell_photosplugin
        tvshell_steamplugin
        tvshell_switchplugin
        tvshell_appsplugin
        tvshell_youtubeplugin
        tvshell_controllersplugin
        tvshell_assistantplugin
)
