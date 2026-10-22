# Dark Fantasy Game

A third-person prototype written in C with [raylib](https://www.raylib.com/). You play an armored knight with a halberd who starts in the graveyard of the Blender map `levels/GroundWithHills.glb` and can walk out through the canyon, across the field and up to the village. The purple hooded figure wanders around the graveyard.

**Play it in your browser:** https://lenodrips-create.github.io/dark-fantasy-game-real/ (click the game to grab the mouse)

## Controls

| Key | Action |
| --- | --- |
| **W A S D** | Move (relative to where the camera looks) |
| **Mouse** | Look around (orbit the camera; move it up to look at the sky) |
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

The browser version is the same C code compiled to WebAssembly with [Emscripten](https://emscripten.org/docs/getting_started/downloads.html). GitHub Pages serves `index.html`, which loads `web/dark_fantasy.js` + `web/dark_fantasy.wasm` and downloads the map and sky from `levels/` and the knight from `characters/`.

After changing the C code, rebuild the web files and commit them:

```sh
emcmake cmake -S . -B build-web
cmake --build build-web
```

This writes straight into `web/`. Replacing files in `levels/` or `characters/` needs no rebuild, the web version picks them up on the next page load (a new file name needs adding to the list in `index.html`).

## Changing the map

Export from Blender with **File > Export > glTF 2.0**, format **glTF Binary (.glb)**, and save it over `levels/GroundWithHills.glb`.

- Use **+Y Up** (the exporter default). Object positions, rotations and scales are applied when the map loads.
- Every mesh is solid: the player stands on it and bumps into it. Materials using **Alpha Blend** or **Alpha Clip** (leaves, tree line cards) are treated as foliage and don't block anything.
- The spawn point is `SPAWN_POINT` at the top of `src/main.c`. It's currently the middle aisle of the graveyard, facing north toward the cliff gap.
- `levels/GroundWithHills.glb` was changed after export: the graveyard was widened (ground 28 units across instead of 14, cliffs moved out to match, graves and trees spread out), and all 12 gravestones were replaced with the `Gravestone.obj` model (1.8 units tall, "R.I.P" side facing south). Your Blender file has neither change, so a fresh export brings back the old graveyard unless you make them in Blender too.

## Characters

- **The knight** (`characters/Armor_Halberd.obj`, textured by `Armor_Halberd.png` through `Armor_Halberd.mtl`) is 1.4 units tall. The model has no skeleton, so `src/knight.c` splits its vertices into torso, thighs, shins, upper arms and forearms (the halberd goes with the right hand) and poses them with joint rotations every frame, the way N64-era characters were animated. The split and the joint positions are measured from this model, so a different model needs new numbers there.
- **The walk** is modeled on Banjo's run in Banjo-Kazooie (Mumbo's Mountain): a forward lean, big quick strides with a high knee lift and a kick back, bent arms pumping against the legs, a bounce on every step and a side-to-side roll. The shoulders turn against the hips with every step, without rocking side to side. The halberd arm moves less so the weapon stays roughly level. Steps are slow and long, about 1.85 units each. It's hand-built from watching the clip, not extracted from the video.
- **The hooded figure** is the `Figure` object from the Blender map. The game finds it by its material (`PoeCollector`), lifts it out of the level and makes it stroll between random spots on the graveyard ground, waiting a few seconds at each and picking somewhere else if a grave or tree blocks it (`src/wanderer.c`). It walks slowly with small, quick steps on its two thin legs and swings its arms against them (the staff arm less). Its legs (below the robe) and arms (sleeves, hands and staff) are split off from the rest of the mesh and swung from the hips and shoulders. The knight can't walk through it.

## Sky

The sky is `levels/sky.jpg`. It's wrapped around the world: the image covers half the horizon and is mirrored for the other half, with its bottom edge just under the horizon and its top at the zenith. It's fixed to the world's directions, so it pans when you turn the camera and never moves when you walk. Swap in another image to change it.

## Code layout

| File | What it does |
| --- | --- |
| `src/main.c` | Window, input, third-person mouse camera, HUD |
| `src/player.c` | Character movement, collision, jumping and the walk cycle timing |
| `src/knight.c` | Loads the knight and animates it |
| `src/wanderer.c` | The hooded figure's roaming |
| `src/world.c` | Loads the `.glb`, moonlight shader, the sky, collision raycasts against the map |
| `characters/` | The knight model and its texture |
| `index.html`, `web/` | Browser version served by GitHub Pages |

The knight is 1.4 units tall, the hooded figure 3. The gravestones are 1.2 to 2.5.
