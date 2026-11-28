# TvShell.Apps: see ../DEVELOPMENT.md.

qt_add_library(tvshell_apps STATIC)

qt_add_qml_module(tvshell_apps
    URI TvShell.Apps
    QML_FILES
        HomePlace.qml
        AppTile.qml
        JellyfinLogo.qml
        YouTubeLogo.qml
    SOURCES
        AppSettings.cpp
        AppSettings.h
        ManagedApp.cpp
        ManagedApp.h
    DEPENDENCIES
        TARGET tvshell_common
        TARGET tvshell_switch
)

# The generated type registration includes the headers by name.
target_include_directories(tvshell_apps PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(tvshell_apps
    PUBLIC
        Qt6::DBus
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        PkgConfig::XCB
        tvshell_common
        tvshell_switch
)
