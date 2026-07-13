# VELOvis3

**VELOvis3** is an interactive three-dimensional event display for the LHCb Vertex Locator (VELO). It is designed to inspect particle-detector events, explore the spatial distribution of hits and tracks, and produce configurable scientific visualizations for presentations and publications.

The display runs as a native desktop application on Windows, Linux, and macOS, or directly in a browser through WebAssembly.

## Scientific visualization features

VELOvis3 displays the main components of a VELO event as independently configurable layers:

- **VELO hits**, shown at their reconstructed three-dimensional positions;
- **particle trajectories**, constructed from the hits associated with Monte Carlo particles;
- **VELO detector modules**, rendered as a translucent detector overlay.

Hits can be coloured by:

- detector depth along the beam axis;
- VELO module;
- particle ID;
- absolute particle ID;
- particle charge.

Tracks can be coloured by:

- particle ID or absolute particle ID;
- charge sign;
- transverse momentum $p_T$;
- momentum $p$;
- pseudorapidity $\eta$;
- long-track status;
- origin from a beauty-hadron decay.

Each layer provides controls for visibility, colour, size or line width, and transparency. Categorical colour schemes are accompanied by an automatic legend, while continuous quantities use an interactive colour bar whose displayed range can be adjusted.

Additional features include:

- interactive three-dimensional camera navigation;
- automatic framing of each event;
- configurable background and field of view;
- preservation of display settings when switching events;
- screenshot export in PNG, BMP, and QOI formats;
- high-DPI rendering on desktop and in the browser.

## Event data

Events are read from JSON files in `assets/events/`. Each file contains:

- hit coordinates `x`, `y`, and `z`;
- a module prefix sum associating hits with VELO modules;
- Monte Carlo particle information;
- the indices of the hits associated with each particle.

The Monte Carlo information can include particle ID, charge, momentum, transverse momentum, pseudorapidity, track type, detector acceptance, and decay-origin flags.

When the application starts, it scans `assets/events/` and loads the first available event. Other events can be selected from the control panel.

## Controls

| Input | Action |
|---|---|
| Left mouse drag | Change viewing direction |
| Right or middle mouse drag | Pan the camera |
| Mouse wheel | Move towards or away from the event |
| `W`, `A`, `S`, `D` | Move through the scene |
| `Q`, `E` | Move vertically |
| Hold `Shift` | Precision movement |
| `F12` | Save a screenshot |
| `Ctrl+Q` | Quit the desktop application |

## Build and run

### Requirements

- CMake 3.20 or newer;
- a C++17 compiler;
- Git;
- Emscripten for browser builds.

Dependencies are downloaded automatically during configuration.

### Desktop

```sh
cmake -B build-desktop -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build-desktop --config Release --parallel
```

Run the application:

```sh
./build-desktop/VELOvis3
```

On Windows with Visual Studio generators:

```powershell
.\build-desktop\Release\VELOvis3.exe
```

### Browser

After installing and activating Emscripten:

```sh
emcmake cmake -B build-web -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build-web --parallel
```

Serve the generated files through a local web server:

```sh
cd build-web
python3 -m http.server 8080
```

Then open `http://localhost:8080/VELOvis3.html`.

The GitHub Actions workflow also builds the WebAssembly target and deploys it through GitHub Pages when Pages is enabled for the repository.

## Implementation

The three-dimensional rendering is implemented with [raylib](https://www.raylib.com/), while the interactive controls use [Dear ImGui](https://github.com/ocornut/imgui) through [rlImGui](https://github.com/raylib-extras/rlImGui).

The cross-platform application infrastructure can also be reused as a starter template for native and WebAssembly scientific graphics applications.

## Citation

If VELOvis3 contributes to scientific work, please cite the software using the metadata in [`CITATION.cff`](CITATION.cff). GitHub provides formatted citation entries through the **Cite this repository** menu.

A suitable acknowledgement is:

> Scientific visualizations were produced using VELOvis3, version 1.1 (Davide Nicotra).

Visualizations generated with VELOvis3 are not automatically covered by the software licence. Users remain responsible for the copyright and licensing status of their figures and of the underlying event data.

## Licence

VELOvis3 is distributed under the [Apache License 2.0](LICENSE).
