# TvShell.Session: see ../DEVELOPMENT.md.

qt_add_library(tvshell_session STATIC)

qt_add_qml_module(tvshell_session
    URI TvShell.Session
    SOURCES
        BaseLayer.cpp
        BaseLayer.h
        ShellService.cpp
        ShellService.h
        SteamLog.cpp
        SteamLog.h
    DEPENDENCIES
        # QWindow, the type of their window property.
        QtQuick
)

# The generated type registration includes the headers by name.
target_include_directories(tvshell_session PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(tvshell_session
    PUBLIC
        Qt6::DBus
        Qt6::Gui
        Qt6::Qml
        PkgConfig::XCB
)
