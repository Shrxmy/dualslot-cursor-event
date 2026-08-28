find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets)
find_package(SDL2 CONFIG QUIET)
if(NOT TARGET SDL2::SDL2)
    find_package(SDL2 REQUIRED)
    add_library(dualslot_sdl2 INTERFACE)
    target_include_directories(dualslot_sdl2 INTERFACE ${SDL2_INCLUDE_DIRS} ${SDL2_INCLUDE_DIR})
    target_link_libraries(dualslot_sdl2 INTERFACE ${SDL2_LIBRARIES} ${SDL2_LIBRARY})
    add_library(SDL2::SDL2 ALIAS dualslot_sdl2)
endif()

add_executable(dualslot WIN32
    src/main.cpp
    src/Types.h
    src/IEmuSession.h
    src/RomDetector.cpp
    src/RomDetector.h
    src/FirmwareStore.cpp
    src/FirmwareStore.h
    src/MgbaSession.cpp
    src/MgbaSession.h
    src/MelonSession.cpp
    src/MelonSession.h
    src/MelonPlatform.cpp
    src/MelonPlatformBridge.h
    src/SlotManager.cpp
    src/SlotManager.h
    src/EmuThread.cpp
    src/EmuThread.h
    src/DsShell.cpp
    src/DsShell.h
    src/MainWindow.cpp
    src/MainWindow.h
    ${CMAKE_SOURCE_DIR}/melonDS-master/src/frontend/ScreenLayout.cpp
)

set_target_properties(dualslot PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
)

target_include_directories(dualslot PRIVATE
    src
    ${CMAKE_SOURCE_DIR}/melonDS-master/src/frontend
)

target_link_libraries(dualslot PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    SDL2::SDL2
    mgba
    core
)

target_compile_definitions(dualslot PRIVATE
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
    DUALSLOT_VERSION="${PROJECT_VERSION}"
    ENABLE_VFS
    ENABLE_DIRECTORIES
    BUILD_STATIC
)

install(TARGETS dualslot RUNTIME DESTINATION .)
