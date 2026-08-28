cmake_minimum_required(VERSION 3.24)

project(DualSlot VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Keep both upstream projects as unmodified library-only dependencies.
set(LIBMGBA_ONLY ON CACHE BOOL "Build only libmGBA" FORCE)
set(M_CORE_GBA ON CACHE BOOL "Build mGBA's GBA core" FORCE)
set(M_CORE_GB ON CACHE BOOL "Build mGBA's GB core" FORCE)
set(BUILD_QT OFF CACHE BOOL "Do not build mGBA Qt frontend" FORCE)
set(BUILD_SDL OFF CACHE BOOL "Do not build mGBA SDL frontend" FORCE)
set(BUILD_SHARED OFF CACHE BOOL "Build static dependencies" FORCE)

set(BUILD_QT_SDL OFF CACHE BOOL "Do not build the melonDS frontend" FORCE)
set(ENABLE_OGLRENDERER OFF CACHE BOOL "Use melonDS software rendering" FORCE)
set(ENABLE_GDBSTUB OFF CACHE BOOL "Do not include the melonDS GDB stub" FORCE)
# GCC 15 on MinGW currently ICEs while LTO-linking melonDS DMA.cpp.
set(ENABLE_LTO_RELEASE OFF CACHE BOOL "Disable unstable MinGW LTO" FORCE)
set(ENABLE_LTO OFF CACHE BOOL "Disable dependency LTO" FORCE)
set(MELONDS_EMBED_BUILD_INFO OFF CACHE BOOL "Do not embed upstream git data" FORCE)

add_subdirectory(mgba-master EXCLUDE_FROM_ALL)
add_subdirectory(melonDS-master EXCLUDE_FROM_ALL)
add_subdirectory(dualslot)
