# webapp: one website as a program of its own on the TV. See DEVELOPMENT.md.

cmake_minimum_required(VERSION 3.21)

project(webapp
    VERSION 0.1.0
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Pinned to what SteamOS 3.9.5 ships (see toolchains/steamos-3.9).
find_package(Qt6 6.11 REQUIRED COMPONENTS DBus Gui Quick WebEngineQuick)
find_package(SDL3 3.4 REQUIRED CONFIG)
find_package(PkgConfig REQUIRED)
pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb)

qt_standard_project_setup(REQUIRES 6.11)

set(QT_QML_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../shared shared)

qt_add_executable(webapp
    main.cpp
)

qt_add_qml_module(webapp
    URI WebApp
    QML_FILES
        Main.qml
    RESOURCES
        host-app.js
        host-main.js
    SOURCES
        MediaKind.cpp
        MediaKind.h
        OnScreen.cpp
        OnScreen.h
        ShellClient.cpp
        ShellClient.h
        Site.cpp
        Site.h
        SiteSettings.cpp
        SiteSettings.h
    DEPENDENCIES
        QtQuick
        TARGET tvshared_input
)

# The generated type registration includes the headers by name.
target_include_directories(webapp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(webapp
    PRIVATE
        Qt6::DBus
        Qt6::Gui
        Qt6::Quick
        Qt6::WebEngineQuick
        PkgConfig::XCB
        tvshared_input
        tvshared_inputplugin
)
