cmake_minimum_required(VERSION 3.20)

# ─── Project ───────────────────────────────────────────────────────────────────
# Rename APP_NAME to start your own project. It is used as the executable name,
# the window title, and the web output filename (APP_NAME.html / .js / .wasm).
set(APP_NAME VELOvis3)
project(${APP_NAME} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default to Release if the user did not pick a build type (single-config gens).
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ─── Package manager: CPM.cmake ────────────────────────────────────────────────
# CPM is a thin, caching wrapper over CMake's FetchContent. It fetches each
# dependency from Git (including rlImGui, which is not in vcpkg/Conan), pins
# versions, and cross-compiles to Emscripten with no extra toolchain steps.
#
# Tip: set the CPM_SOURCE_CACHE environment variable to a shared folder so the
# downloaded sources are reused across projects and clean builds, e.g.
#   setx CPM_SOURCE_CACHE %USERPROFILE%\.cache\CPM      (Windows, once)
#   export CPM_SOURCE_CACHE=$HOME/.cache/CPM            (macOS / Linux)
include(cmake/CPM.cmake)

# ─── Emscripten / Web setup ────────────────────────────────────────────────────
# raylib must know it is building for the web BEFORE it is configured, otherwise
# it looks for a native OpenGL/GLFW installation.
if(EMSCRIPTEN)
    set(PLATFORM "Web" CACHE STRING "" FORCE)
endif()

# ─── raylib ────────────────────────────────────────────────────────────────────
CPMAddPackage(
    NAME raylib
    GITHUB_REPOSITORY raysan5/raylib
    GIT_TAG 6.0
    OPTIONS
        "BUILD_EXAMPLES OFF"
        "BUILD_GAMES OFF"
)

# ─── Dear ImGui ────────────────────────────────────────────────────────────────
# DOWNLOAD_ONLY: fetch the source tree only; we compile the units ourselves so we
# control exactly which backend/translation units are built.
CPMAddPackage(
    NAME imgui
    GITHUB_REPOSITORY ocornut/imgui
    GIT_TAG v1.92.7
    DOWNLOAD_ONLY YES
)

# ─── rlImGui (raylib <-> Dear ImGui bridge) ────────────────────────────────────
# DOWNLOAD_ONLY: rlImGui's own CMake uses find_package, which won't resolve here.
CPMAddPackage(
    NAME rlimgui
    GITHUB_REPOSITORY raylib-extras/rlImGui
    GIT_TAG Raylib_6_0
    DOWNLOAD_ONLY YES
)

# ─── Dear ImGui static library ─────────────────────────────────────────────────
add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp   # provides ImGui::ShowDemoWindow (drop if unused)
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

# ─── rlImGui static library ────────────────────────────────────────────────────
add_library(rlimgui_lib STATIC
    ${rlimgui_SOURCE_DIR}/rlImGui.cpp
)
target_include_directories(rlimgui_lib PUBLIC
    ${rlimgui_SOURCE_DIR}
    ${raylib_SOURCE_DIR}/src    # rlImGui.cpp needs rlgl.h, which lives here
)
target_link_libraries(rlimgui_lib PUBLIC raylib imgui_lib)

# ─── nlohmann/json (header-only) ───────────────────────────────────────────────
CPMAddPackage(
    NAME nlohmann_json
    GITHUB_REPOSITORY nlohmann/json
    GIT_TAG v3.11.3
    OPTIONS
        "JSON_BuildTests OFF"
)

# ─── Main executable ───────────────────────────────────────────────────────────
add_executable(${APP_NAME}
    src/main.cpp
    src/App.cpp
    src/EventReader.cpp
    src/GpuBatch.cpp
    src/EventScene.cpp
)
target_include_directories(${APP_NAME} PRIVATE src)
target_link_libraries(${APP_NAME} PRIVATE
    raylib
    imgui_lib
    rlimgui_lib
    nlohmann_json::nlohmann_json
)

# ─── Emscripten / Web build ────────────────────────────────────────────────────
if(EMSCRIPTEN)
    # Emit APP_NAME.html (+ .js + .wasm) instead of a bare binary.
    set(CMAKE_EXECUTABLE_SUFFIX ".html")

    # Wasm-native exception handling: without this, emscripten's default
    # (JS-based, catching disabled) means any C++ throw — e.g. from
    # EventReader on a missing/malformed file — aborts the whole runtime
    # instead of being catchable.
    target_compile_options(${APP_NAME} PRIVATE -fwasm-exceptions)

    target_link_options(${APP_NAME} PRIVATE
        -fwasm-exceptions
        # Custom HTML shell: console pane, fullscreen, HiDPI-correct canvas sizing.
        "SHELL:--shell-file ${CMAKE_SOURCE_DIR}/src/shell.html"
        # Emscripten's bundled GLFW 3 — required by raylib on the web.
        "SHELL:-s USE_GLFW=3"
        "SHELL:-s WASM=1"
        # Grow the WebAssembly heap on demand, starting at 64 MB.
        "SHELL:-s ALLOW_MEMORY_GROWTH=1"
        "SHELL:-s INITIAL_MEMORY=67108864"
        "SHELL:-s FORCE_FILESYSTEM=1"
        # Needed by raylib for WebGL function-pointer lookup.
        "SHELL:-s GL_ENABLE_GET_PROC_ADDRESS=1"
    )

    # Bundle the assets/ folder into the virtual filesystem if it has content.
    file(GLOB ASSET_FILES "${CMAKE_SOURCE_DIR}/assets/*")
    list(FILTER ASSET_FILES EXCLUDE REGEX "\\.gitkeep$")
    if(ASSET_FILES)
        target_link_options(${APP_NAME} PRIVATE
            "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/assets@assets")
    endif()
endif()
