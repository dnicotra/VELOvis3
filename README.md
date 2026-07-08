# dear_raylib

A starter **template** for cross-platform graphics apps built with
[raylib](https://www.raylib.com/), [Dear ImGui](https://github.com/ocornut/imgui),
and [rlImGui](https://github.com/raylib-extras/rlImGui).

One codebase builds **native** desktop apps (Windows, Linux, macOS) **and**
**WebAssembly** for the browser, with a polished HTML shell that gives you a
console pane, a fullscreen button, and correct HiDPI scaling.

Dependencies are managed by [**CPM.cmake**](https://github.com/cpm-cmake/CPM.cmake) —
a single, vendored CMake file. There is nothing to install before building: CPM
fetches and caches raylib, Dear ImGui, and rlImGui on first configure.

---

## What you get

- **raylib 6.0** application skeleton (`App` class, native/web frame loop).
- **Dear ImGui 1.92.7** wired up through rlImGui, with a working controls window.
- **Seamless native + Emscripten** — same `src/`, selected at configure time.
- **HiDPI-correct web shell** — the canvas framebuffer is sized in physical
  pixels while ImGui and the CSS layout stay in logical pixels.
- **Console + fullscreen** in the browser via `src/shell.html`.
- **GitHub Pages workflow** that builds the web target and deploys it on push.

---

## Why CPM.cmake?

This stack needs **rlImGui**, which is not published in vcpkg or Conan. CPM.cmake
fetches it (and everything else) straight from Git, pins exact versions, caches
sources across projects, and cross-compiles to Emscripten with no extra toolchain
files — so the native and web builds stay a single `cmake` invocation each.

To reuse downloads across projects and clean builds, point CPM at a shared cache:

```sh
export CPM_SOURCE_CACHE=$HOME/.cache/CPM          # macOS / Linux
```
```powershell
setx CPM_SOURCE_CACHE "$env:USERPROFILE\.cache\CPM"   # Windows (once, new shells)
```

---

## Prerequisites

| Tool | Version |
|------|---------|
| CMake | 3.20+ |
| A C++17 compiler | MSVC / GCC / Clang |
| Git | any recent |
| Emscripten | for web builds only (see below) |

---

## Desktop build

```sh
cmake -B build-desktop -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build-desktop --config Release --parallel
```

Run it:

```sh
./build-desktop/dear_raylib              # Linux / macOS
.\build-desktop\Release\dear_raylib.exe  # Windows (MSVC)
```

Or use the helper: `./scripts/build-desktop.ps1 -Run`.

---

## Web build (Emscripten)

### 1 — Install & activate emsdk (once)

```sh
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk && ./emsdk install latest && ./emsdk activate latest
source ./emsdk_env.sh                     # macOS / Linux, each session
```
```powershell
.\emsdk_env.ps1                           # Windows, each session
```

### 2 — Configure & build

```sh
emcmake cmake -B build-web -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build-web --parallel
```

Helpers: `./scripts/build-web.sh --serve` or `.\scripts\build-web.ps1 -Serve`.

### 3 — Serve (browsers block `file://` for WASM)

```sh
cd build-web && python3 -m http.server 8080
```

Open <http://localhost:8080/dear_raylib.html>.

---

## Make it your own

1. **Rename the app**: change `set(APP_NAME dear_raylib)` at the top of
   `CMakeLists.txt`, the `env: APP_NAME` in `.github/workflows/deploy.yml`, the
   `InitWindow(...)` title in `src/App.cpp`, and `<title>` in `src/shell.html`.
2. **Write your app**: replace the bouncing-circle demo in `src/App.cpp` /
   `src/App.hpp`. The frame loop, input, web resize, and DPI handling are already
   done for you.
3. **Add assets**: drop files in `assets/`. On the web they are preloaded into
   the virtual filesystem under `assets/` automatically.
4. **Add dependencies**: add another `CPMAddPackage(...)` block in
   `CMakeLists.txt`.

---

## Project structure

```
.
├── CMakeLists.txt          # build system; CPM handles all dependencies
├── cmake/
│   └── CPM.cmake           # vendored package manager (single file)
├── src/
│   ├── main.cpp            # entry point — native while-loop / Emscripten callback
│   ├── App.hpp             # App + AppState declarations
│   ├── App.cpp             # frame loop, input, DPI/resize, draw, ImGui
│   └── shell.html          # Emscripten HTML shell (canvas + console + fullscreen)
├── assets/                 # bundled into the web build's virtual filesystem
├── scripts/                # build-desktop / build-web helpers
└── .github/workflows/
    └── deploy.yml          # build web target + deploy to GitHub Pages
```

---

## Pinned dependency versions

| Library | Version |
|---------|---------|
| raylib | `6.0` |
| Dear ImGui | `v1.92.7` |
| rlImGui | `Raylib_6_0` branch |
| CPM.cmake | `0.42.3` |
