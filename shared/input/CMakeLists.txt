# TvShared.Input: the controller as keys, and keys that arrive fresh. See ../DEVELOPMENT.md.

qt_add_library(tvshared_input STATIC)

qt_add_qml_module(tvshared_input
    URI TvShared.Input
    SOURCES
        GamepadInput.cpp
        GamepadInput.h
        FreshKeys.cpp
        FreshKeys.h
    DEPENDENCIES
        # QWindow, the type of their window property.
        QtQuick
)

# The generated type registration includes the headers by name.
target_include_directories(tvshared_input PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(tvshared_input
    PUBLIC
        Qt6::Gui
        Qt6::Qml
        SDL3::SDL3
)
