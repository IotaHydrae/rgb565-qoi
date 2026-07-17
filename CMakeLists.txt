cmake_minimum_required(VERSION 3.10)
project(rgb565-qoi VERSION 1.0.0 LANGUAGES C)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(RGB565_QOI_BUILD_EXAMPLES "Build example programs"     OFF)
option(RGB565_QOI_BUILD_TESTS    "Build test suite"           OFF)
option(RGB565_QOI_BUILD_TOOLS    "Build conversion tools"     OFF)

# ---------------------------------------------------------------------------
# Library target
# ---------------------------------------------------------------------------
add_library(rgb565_qoi STATIC
    src/rgb565_qoi.c
)

target_include_directories(rgb565_qoi
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(rgb565_qoi PUBLIC c_std_99)

# The library is deliberately freestanding-friendly.
# No -ffreestanding here, so the build host can link it with hosted
# programs (examples / tests).  Users targeting bare-metal should
# compile with -ffreestanding themselves.

# ---------------------------------------------------------------------------
# Installation
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS rgb565_qoi
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)

# ---------------------------------------------------------------------------
# Sub-directories
# ---------------------------------------------------------------------------
if(RGB565_QOI_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(RGB565_QOI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(RGB565_QOI_BUILD_TOOLS)
    add_subdirectory(tools)
endif()
