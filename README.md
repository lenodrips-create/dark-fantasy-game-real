# Dark Fantasy Game (Real)

Simple starter for the dark fantasy game.

## Current Status
- Stickman character controlled with **WASD**
- Basic ground plane (placeholder for the real level)
- The original Blender file will become the first level (keep it **untouched**)

## How to add the Blender level
The Blender file is ~25 MB, so you must add it with Git LFS (or upload it through the GitHub website).

### Option A – Recommended (Git LFS)
```bash
# 1. Clone the repo
git clone https://github.com/lenodrips-create/dark-fantasy-game-real.git
cd dark-fantasy-game-real

# 2. Install & set up LFS
git lfs install
git lfs track "*.blend"
git add .gitattributes

# 3. Copy your original Blender file (do NOT edit it)
cp /path/to/your/Untitled.blend levels/first_level.blend

# 4. Commit & push
git add levels/first_level.blend
git commit -m "Add original Blender level (untouched)"
git push
```

### Option B – GitHub website
1. Go to the repo → Create new file or Upload files
2. Put the file in the `levels/` folder and name it `first_level.blend`
3. Commit

## Controls
- **W** Forward
- **A** Left
- **S** Backward
- **D** Right

## Run the current prototype
Just open `index.html` in a browser.
