cmake_minimum_required(VERSION 3.21)

# ============================================================
# vcpkg
# ============================================================

if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)

    set(
        CMAKE_TOOLCHAIN_FILE
        "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING ""
    )

endif()

# ------------------------------------------------------------
# IMPORTANT:
# Use static vcpkg libraries so fmt and spdlog are linked
# directly into SkyLab.dll and do not require separate DLLs.
# ------------------------------------------------------------

set(
    VCPKG_TARGET_TRIPLET
    "x64-windows-static"
    CACHE STRING
    "vcpkg target triplet"
    FORCE
)

project(SkyLab VERSION 1.0.0 LANGUAGES CXX)

# ============================================================
# MSVC Runtime
# ============================================================
# x64-windows-static vcpkg uses the static MSVC runtime (/MT).
# Keep every SkyLab dependency and source file on the same CRT.

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        CACHE STRING
        "MSVC runtime library"
        FORCE
    )
endif()

# ============================================================
# General configuration
# ============================================================

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)

set(ENABLE_SKYRIM_SE OFF CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)

set(BUILD_TESTS OFF CACHE BOOL "" FORCE)

include(FetchContent)

# ============================================================
# Verify vcpkg static configuration
# ============================================================

if(NOT VCPKG_TARGET_TRIPLET STREQUAL "x64-windows-static")

    message(FATAL_ERROR
        "SkyLab requires the x64-windows-static vcpkg triplet.\n"
        "Current triplet: ${VCPKG_TARGET_TRIPLET}"
    )

endif()

message(STATUS "Using vcpkg triplet: ${VCPKG_TARGET_TRIPLET}")

# ============================================================
# Dependencies
# ============================================================

FetchContent_Declare(
    rapidcsv
    GIT_REPOSITORY https://github.com/d99kris/rapidcsv.git
    GIT_TAG v9.07
    GIT_SHALLOW TRUE
)

# Nifly handles the Skyrim SE/AE block layout and builds valid BSTriShape
# files for imported static meshes. Keep its own optional test suite disabled.
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    nifly
    GIT_REPOSITORY https://github.com/ousnius/nifly.git
    GIT_TAG f445ebc
)

FetchContent_MakeAvailable(
    rapidcsv
    nifly
)

set(
    RAPIDCSV_INCLUDE_DIRS
    "${rapidcsv_SOURCE_DIR}/src"
    CACHE PATH "" FORCE
)

# ------------------------------------------------------------
# Static spdlog + fmt
# ------------------------------------------------------------

find_package(
    fmt
    CONFIG
    REQUIRED
)

find_package(
    spdlog
    CONFIG
    REQUIRED
)

find_package(
    directxtk
    CONFIG
    REQUIRED
)

# ============================================================
# CommonLibSSE-NG
# ============================================================

set(COMMONLIBSSE_VERSION "v11.0.0")

set(
    COMMONLIBSSE_SRC
    "${CMAKE_BINARY_DIR}/_deps/commonlibsse-src"
)

set(
    COMMONLIBSSE_BIN
    "${CMAKE_BINARY_DIR}/_deps/commonlibsse-build"
)

# ------------------------------------------------------------
# Clone maintained CommonLibSSE-NG fork
# ------------------------------------------------------------

if(NOT EXISTS "${COMMONLIBSSE_SRC}/CMakeLists.txt")

    message(STATUS
        "CommonLibSSE-NG not found. Cloning ${COMMONLIBSSE_VERSION}..."
    )

    file(
        MAKE_DIRECTORY
        "${CMAKE_BINARY_DIR}/_deps"
    )

    execute_process(
        COMMAND
            git clone
            --branch "${COMMONLIBSSE_VERSION}"
            --depth 1
            https://github.com/alandtse/CommonLibSSE-NG.git
            "${COMMONLIBSSE_SRC}"

        RESULT_VARIABLE COMMONLIBSSE_CLONE_RESULT

        OUTPUT_VARIABLE COMMONLIBSSE_CLONE_OUTPUT
        ERROR_VARIABLE COMMONLIBSSE_CLONE_ERROR

        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE
    )

    if(NOT COMMONLIBSSE_CLONE_RESULT EQUAL 0)

        message(FATAL_ERROR
            "Failed to clone CommonLibSSE-NG.\n"
            "Git output:\n${COMMONLIBSSE_CLONE_OUTPUT}\n"
            "Git error:\n${COMMONLIBSSE_CLONE_ERROR}"
        )

    endif()

endif()

if(NOT EXISTS "${COMMONLIBSSE_SRC}/CMakeLists.txt")

    message(FATAL_ERROR
        "CommonLibSSE-NG CMakeLists.txt was not found at:\n"
        "${COMMONLIBSSE_SRC}"
    )

endif()

# ------------------------------------------------------------
# CommonLib configuration
# ------------------------------------------------------------

set(ENABLE_SKYRIM_SE OFF CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)

set(BUILD_TESTS OFF CACHE BOOL "" FORCE)

# ------------------------------------------------------------
# Add CommonLibSSE-NG
# ------------------------------------------------------------

add_subdirectory(
    "${COMMONLIBSSE_SRC}"
    "${COMMONLIBSSE_BIN}"
)

# ============================================================
# ImGui
# ============================================================

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.9b-docking
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(imgui)

add_library(
    imgui
    STATIC

    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_demo.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"

    "${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp"
)

target_include_directories(
    imgui
    PUBLIC

    "${imgui_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}/backends"
)

target_link_libraries(
    imgui
    PUBLIC

    d3d11
    dxgi
    d3dcompiler
)

# ============================================================
# SkyLab DLL
# ============================================================

file(
    GLOB_RECURSE
    SKYLAB_SOURCES
    CONFIGURE_DEPENDS

    "${CMAKE_SOURCE_DIR}/plugin/src/*.cpp"
    "${CMAKE_SOURCE_DIR}/plugin/src/*.h"
)

if(NOT SKYLAB_SOURCES)

    message(FATAL_ERROR
        "No SkyLab source files were found under:\n"
        "${CMAKE_SOURCE_DIR}/plugin/src"
    )

endif()

add_library(
    SkyLab
    SHARED

    ${SKYLAB_SOURCES}
)

target_include_directories(
    SkyLab
    PRIVATE

    "${CMAKE_SOURCE_DIR}/plugin/src"

    "${imgui_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}/backends"
)

target_precompile_headers(
    SkyLab
    PRIVATE

    "${CMAKE_SOURCE_DIR}/plugin/src/PCH.h"
)

# ============================================================
# Link SkyLab
# ============================================================

target_link_libraries(
    SkyLab
    PRIVATE

    # CommonLib
    CommonLibSSE::CommonLibSSE

    # ImGui
    imgui

    # Static fmt
    fmt::fmt

    # Static spdlog
    spdlog::spdlog

    # NIF read/write and Skyrim SE/AE mesh creation
    nifly
)

target_compile_definitions(
    SkyLab
    PRIVATE

    NOMINMAX
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
)

if(MSVC)

    target_compile_options(
        SkyLab
        PRIVATE

        /W4
        /permissive-
        /Zc:preprocessor
        /MP
    )

    target_link_options(
        SkyLab
        PRIVATE

        /WX:NO
    )

endif()

set_target_properties(
    SkyLab
    PROPERTIES

    OUTPUT_NAME "SkyLab"
)

# ============================================================
# Deploy DLL
# ============================================================

set(
    SKYLAB_PLUGIN_DIR
    "${CMAKE_SOURCE_DIR}/contrib/SKSE/Plugins"
)

add_custom_command(
    TARGET SkyLab
    POST_BUILD

    COMMAND
        "${CMAKE_COMMAND}"
        -E
        make_directory
        "${SKYLAB_PLUGIN_DIR}"

    COMMAND
        "${CMAKE_COMMAND}"
        -E
        copy_if_different

        "$<TARGET_FILE:SkyLab>"
        "${SKYLAB_PLUGIN_DIR}/SkyLab.dll"

    COMMENT
        "Deploying SkyLab.dll to contrib/SKSE/Plugins"
)

# ============================================================
# Status
# ============================================================

message(STATUS "")
message(STATUS "========================================")
message(STATUS " SkyLab configuration")
message(STATUS "========================================")
message(STATUS " Skyrim AE:          ON")
message(STATUS " Skyrim SE:          OFF")
message(STATUS " Skyrim VR:          OFF")
message(STATUS " C++ standard:       C++23")
message(STATUS " vcpkg triplet:      ${VCPKG_TARGET_TRIPLET}")
message(STATUS " CommonLibSSE-NG:    ${COMMONLIBSSE_VERSION}")
message(STATUS " fmt:                12.2.0 (STATIC)")
message(STATUS " spdlog:             v1.17.0 (STATIC)")
message(STATUS " rapidcsv:           v9.07")
message(STATUS " ImGui:              v1.92.9b-docking")
message(STATUS " Output:             SkyLab.dll")
message(STATUS " Deploy directory:   ${SKYLAB_PLUGIN_DIR}")
message(STATUS "========================================")
message(STATUS "")
