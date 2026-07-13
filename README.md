# VELOvis3

**VELOvis3** is an interactive three-dimensional event display for simulated
$B_s^0 \to \phi\phi$ decays in the LHCb VELO under Run 3 detector conditions.

For LHCb users, it provides a compact view of VELO hits, Monte Carlo particle
trajectories, and the Run 3 VELO module geometry. For non-experts, it offers an
accessible way to explore particle collisions in 3D and create high-quality
scientific visuals.

## Features

- interactive navigation through the event;
- independent hit, track, and detector layers;
- colouring by particle ID, charge, $p$, $p_T$, $\eta$, module, and decay origin;
- automatic legends and adjustable colour ranges;
- configurable background, transparency, point size, and track width;
- screenshot export in PNG, BMP, and QOI formats;
- native desktop and browser versions.

Events are loaded from JSON files in `assets/events/`.

## Controls

| Input | Action |
|---|---|
| Left drag | Look around |
| Right or middle drag | Pan |
| Mouse wheel | Move in or out |
| `W`, `A`, `S`, `D`, `Q`, `E` | Move through the scene |
| Hold `Shift` | Precision movement |
| `F12` | Save a screenshot |

## Build

```sh
cmake -B build-desktop -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build-desktop --config Release --parallel
```

For the browser build, configure the same project with Emscripten.

The display is built with [raylib](https://www.raylib.com/) and
[Dear ImGui](https://github.com/ocornut/imgui).

Its native/WebAssembly application shell can also be reused as a template for
other scientific visualisation tools.

## Citation and licence

If VELOvis3 contributes to scientific work, please cite it using
[`CITATION.cff`](CITATION.cff).

VELOvis3 is distributed under the [Apache License 2.0](LICENSE).
