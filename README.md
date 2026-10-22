# Dark Fantasy Game

A third-person prototype written in C with [raylib](https://www.raylib.com/). You play a stickman who starts in the graveyard of the Blender map `levels/GroundWithHills.glb` and can walk out through the canyon, across the field and up to the village.

**Play it in your browser:** https://lenodrips-create.github.io/dark-fantasy-game-real/ (click the game to grab the mouse)

## Controls

| Key | Action |
| --- | --- |
| **W A S D** | Move (relative to where the camera looks) |
| **Mouse** | Look around (orbit the camera) |
| **Mouse wheel** | Zoom in / out |
| **Shift** | Sprint |
| **Space** | Jump |
| **R** | Back to the spawn point |
| **Esc** | Free the mouse; press again to quit (click to grab the mouse again) |

## Building

You need a C compiler and CMake 3.16 or newer. The first build downloads and compiles raylib 5.5 automatically, so you need an internet connection the first time.

```sh
cmake -S . -B build
cmake --build build --config Release
```

Then run it:

- **Windows (Visual Studio):** `build\Release\dark_fantasy.exe`
- **macOS / Linux / MinGW:** `build/dark_fantasy`

The build copies `levels/` next to the executable, so you can also start the game by double-clicking it.

### Platform notes

- **Windows:** install Visual Studio 2022 with the "Desktop development with C++" workload (it includes CMake). You can also open the folder in Visual Studio and it will pick up `CMakeLists.txt`.
- **macOS:** `xcode-select --install`, then `brew install cmake`.
- **Linux (Debian/Ubuntu):** `sudo apt install build-essential cmake libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev`

### Web version

The browser version is the same C code compiled to WebAssembly with [Emscripten](https://emscripten.org/docs/getting_started/downloads.html). GitHub Pages serves `index.html`, which loads `web/dark_fantasy.js` + `web/dark_fantasy.wasm` and downloads the map from `levels/`.

After changing the C code, rebuild the web files and commit them:

```sh
emcmake cmake -S . -B build-web
cmake --build build-web
```

This writes straight into `web/`. Replacing the map in `levels/` needs no rebuild, the web version picks it up on the next page load.

## Changing the map

Export from Blender with **File > Export > glTF 2.0**, format **glTF Binary (.glb)**, and save it over `levels/GroundWithHills.glb`.

- Use **+Y Up** (the exporter default). Object positions, rotations and scales are applied when the map loads.
- Every mesh is solid: the player stands on it and bumps into it. Materials using **Alpha Blend** or **Alpha Clip** (leaves, tree line cards) are treated as foliage and don't block anything.
- The spawn point is `SPAWN_POINT` at the top of `src/main.c`. It's currently the middle aisle of the graveyard, facing north toward the cliff gap.

## Code layout

| File | What it does |
| --- | --- |
| `src/main.c` | Window, input, third-person mouse camera, HUD |
| `src/player.c` | Stickman movement, collision, jumping and the procedural walk animation |
| `src/world.c` | Loads the `.glb`, moonlight + fog shader, collision raycasts against the map |
| `index.html`, `web/` | Browser version served by GitHub Pages |

The stickman is about 2.8 units tall. The tallest gravestones are about 2.5.
