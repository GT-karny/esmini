# GT_esmini esminiJS (WASM) — GT variant

This is the **GT_esmini variant of esminiJS**: the Emscripten/WASM binding of the
esmini scenario + RoadManager engine, with GT extensions wired in (GT_RoadManager
LHT/RHT swap, GT scenario extension actions, traffic-signal + vehicle-light
introspection, and the lane-change-aware route binding).

## Origin

Relocated out of the upstream core directory
`EnvironmentSimulator/Libraries/esminiJS/` to restore Clean Core separation
(audit **BND-1 / R5-U1**). The GT modifications were originally applied directly
on top of upstream esmini **v3.0.2** (fork merge-base `e504e8ae`), which was a
"Clean Core" violation. After this relocation, the core `esminiJS` directory is
**vanilla upstream** again and merges as a clean take-theirs against
upstream/master (v3.3.0).

Files here (all GT content; the upstream core copies are untouched / vanilla):

| File | Notes |
| :--- | :--- |
| `CMakeLists.txt` | Standalone emscripten project. Identical to the GT core copy except relative paths (this dir is 3 levels under the repo root). |
| `esminijs.cpp` / `esminijs.hpp` | `OpenScenario` class: XOSC sanitizer (R5-U3: rewraps bare `<PrivateAction><LightStateAction>` into the native `AppearanceAction` wrapper; light actions are now PARSED+EXECUTED by the native v3.3.0 `LightStateAction`, no longer stripped), `GT_ScenarioReader::ParseExtensionActions`, step API, vehicle-light (via `VehicleLightBridge::ReadLight`) / traffic-signal introspection. |
| `embind.cpp` | embind bindings for the step API, `StoryBoardEvent`/`ConditionEvent`, and the introspection functions. |
| `embind_rm.cpp` | `RoadManagerJS` wasm wrapper (coordinate conversion, no running scenario needed). |
| `gt_embind_route.cpp` | `GTRouteJS` — LaneIndependentRouter route binding (already lived here). |
| `build.sh` | Build helper (Git Bash + emsdk). |

## Path layout (relative to this directory)

- Core esmini modules: `../../../EnvironmentSimulator/Modules/...`
- Repo-root externals (pugixml / fmt / expr / yaml): `../../../externals/...`
- GT_esmini sources / headers: `../../src/...`, `../../include`
- GT route binding: `${CMAKE_CURRENT_SOURCE_DIR}/gt_embind_route.cpp`

## How to build

Proven local toolchain (from the original core build's `CMakeCache.txt`):

- Generator: **Ninja**
- Toolchain file: `E:/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake`
- emsdk at `E:\emsdk`

### Git Bash

```bash
source /e/emsdk/emsdk_env.sh      # activate emsdk (PATH for emcc/emcmake/emmake)
cd GT_esmini/web/wasm
./build.sh                        # -> build/esmini.js
```

### Windows (cmd / PowerShell)

```bat
E:\emsdk\emsdk_env.bat
cd GT_esmini\web\wasm
mkdir build & cd build
emcmake cmake -G Ninja ..
emmake ninja
```

Or invoke the emscripten toolchain file directly without the `emcmake` wrapper:

```bat
cmake -G Ninja ^
  -DCMAKE_TOOLCHAIN_FILE=E:/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake ^
  -S . -B build
cmake --build build
```

Output is a single-file module `build/esmini.js` (`-s SINGLE_FILE=1`,
`MODULARIZE=1`, `EXPORT_NAME="esmini"`).

## Notes

- The core `EnvironmentSimulator/Libraries/esminiJS/` is now vanilla upstream and
  must NOT carry GT edits.
- This copy is pinned to v3.0.2-era core APIs and the GT_RoadManager fork; a
  post-merge rebuild against upstream v3.3.0 may need API updates (out of scope
  for the relocation task).
- `build/` is gitignored (see `.gitignore`).
