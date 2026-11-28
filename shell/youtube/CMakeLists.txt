# TvShell.YouTube: YouTube's options in Settings; YouTube itself is a web app
# (../apps/webapp). See ../DEVELOPMENT.md.

qt_add_library(tvshell_youtube STATIC)

qt_add_qml_module(tvshell_youtube
    URI TvShell.YouTube
    SOURCES
        YouTubeSettings.cpp
        YouTubeSettings.h
)

# The generated type registration includes the headers by name.
target_include_directories(tvshell_youtube PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(tvshell_youtube
    PUBLIC
        Qt6::Core
        Qt6::Qml
)
