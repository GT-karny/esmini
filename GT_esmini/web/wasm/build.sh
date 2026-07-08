#!/usr/bin/env bash
#
# Build the GT variant of esminiJS (WASM) from this relocated standalone project.
#
# Prerequisites (this machine):
#   - emsdk installed at E:\emsdk  (activate with: source /e/emsdk/emsdk_env.sh
#     under Git Bash, or run E:\emsdk\emsdk_env.bat in a cmd/PowerShell shell first)
#   - Ninja generator (the proven local toolchain; see CMakeCache of the original
#     EnvironmentSimulator/Libraries/esminiJS/build):
#       CMAKE_GENERATOR        = Ninja
#       CMAKE_TOOLCHAIN_FILE   = E:/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake
#
# Usage (Git Bash, with emsdk_env already sourced):
#   ./build.sh
#
# Output: build/esmini.js  (single-file WASM module, SINGLE_FILE=1)
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

if [ ! -d "build/" ]; then
    mkdir "build/"
fi

cd build/

# emcmake/emmake wrap CMake/Make with the emscripten toolchain file and
# select the right compilers. Generator is Ninja (proven locally).
emcmake cmake -G Ninja ..
emmake ninja

echo ""
echo "Built: $SCRIPT_DIR/build/esmini.js"
