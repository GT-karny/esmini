#!/bin/bash
#
# Build SUMO's netconvert (and duarouter) from source, pinned to the SAME version
# esmini embeds, and stage the binaries under thirdparty/ -- NOT into the repo.
#
# WHY FROM SOURCE, AND WHY THIS VERSION
# -------------------------------------
# esmini links libsumo from SUMO **1.6.0** (externals/sumo/, USE_SUMO=ON by
# default). A netconvert from any current SUMO release writes net format 1.20
# and vehicle classes that 1.6.0 does not know, and the embedded runtime then
# dies with `Unknown vehicle class` / exit 255
# (GT_esmini/docs/features/sumo_background_traffic.md section 5).
# Pinning netconvert to v1_6_0 removes that mismatch by construction -- no
# net.xml downgrade sanitizer needed.
#
# It also gives us a source tree, which is required for --lht: SUMO's OpenDRIVE
# importer has no left-hand-traffic support at all, and the fix is a source
# change (see .claude/skills/sumo-authoring/references/netconvert_traps.md, LHT).
#
# WHY NOT REUSE generate_sumo_libs.sh
# -----------------------------------
# That script builds the libsumo libraries esmini links, and its output is
# already vendored in externals/sumo/. It cannot be reused here as-is:
#   * it pins `-T v141` (the VS2017 toolset). Not installed on the current
#     machine -- only VS2022 toolsets (14.34/14.36/14.43) are present, so the
#     configure step would fail outright. This script uses the default toolset.
#   * externals/sumo/v10/ ships the zlib/xerces .lib files but NOT their
#     headers, so those cannot be reused for a fresh compile either.
# It is left untouched; this is a sibling, not a replacement.
#
# LICENSING
# ---------
# SUMO is EPL-2.0 and esmini already links it statically into GT_Sim.exe, with
# 3rd_party_terms_and_licenses/sumo_LICENSE.txt shipped in the distribution.
# The binaries this script produces stay under thirdparty/ (git-ignored) and are
# deliberately NOT added to the distribution ZIP. That matters most for --lht:
# a patched netconvert is a "Modified Work" under EPL-2.0, and not distributing
# it avoids the accompanying source/notice obligations entirely.
#
# USAGE
#   scripts/generate_sumo_netconvert.sh [--lht] [--jobs N] [--work-dir DIR]
#
#   --lht        apply the left-hand-traffic fix to the OpenDRIVE importer
#                before building (see apply_lht_patch below)
#   --jobs N     parallel build jobs (default 8)
#   --work-dir   scratch dir for sources/builds (default thirdparty/sumo-build)
#
# OUTPUT
#   thirdparty/sumo-tools/bin/netconvert.exe
#   thirdparty/sumo-tools/bin/duarouter.exe
#   thirdparty/sumo-tools/tools/     (randomTrips.py and friends, from the tree)
#   thirdparty/sumo-tools/BUILD_INFO (what was built, and whether --lht was on)
#
# RUNNING netconvert AFTERWARDS -- READ THIS
#   Always pass --xml-validation never.
#   SUMO validates XML against the schema named in xsi:noNamespaceSchemaLocation,
#   which for its OWN built-in type map is the remote URL
#   http://sumo.dlr.de/xsd/types_file.xsd. With no network access the fetch fails
#   and netconvert aborts with:
#       The types could not be loaded from 'built in type map'.
#   That message points at the type map, which is present and correct; the actual
#   failure is the schema fetch. Every parse fails the same way ("line/column 1/0"
#   with an empty message), so it looks like a broken xerces -- it is not: xerces'
#   own SAXCount parses the very same file fine.
#
# Re-running is safe: each stage is skipped when its output already exists.
# Delete the work dir to force a clean rebuild.

set -euo pipefail

SUMO_TAG="v1_6_0"
ZLIB_VER="1.2.12"
XERCES_VER="3.2.2"
JOBS=8
LHT=0

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK_DIR="${REPO_ROOT}/thirdparty/sumo-build"
OUT_DIR="${REPO_ROOT}/thirdparty/sumo-tools"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --lht)      LHT=1; shift ;;
        --jobs)     JOBS="$2"; shift 2 ;;
        --work-dir) WORK_DIR="$2"; shift 2 ;;
        -h|--help)  sed -n '2,55p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

if [[ "${OSTYPE}" != "msys" ]]; then
    echo "This script currently targets Windows/MSYS (Git Bash). OSTYPE=${OSTYPE}" >&2
    exit 2
fi

GENERATOR="Visual Studio 17 2022"
# No -T: the VS2017 (v141) toolset generate_sumo_libs.sh asks for is not present
# here. netconvert is a standalone executable and does not have to share an ABI
# with the vendored libsumo, so the default toolset is fine.
GEN_ARGS=(-A x64)

# SUMO 1.6.0, zlib 1.2.x and xerces 3.2.2 all declare cmake_minimum_required
# below 3.5. CMake 4.x REMOVED compatibility with those and errors out during
# configure ("Compatibility with CMake < 3.5 has been removed"). This restores
# just enough policy compatibility to configure them; without it every one of
# the three configure steps below fails before compiling a single file.
POLICY_ARG=(-DCMAKE_POLICY_VERSION_MINIMUM=3.5)

log() { echo "=== $* ==="; }

mkdir -p "${WORK_DIR}"
cd "${WORK_DIR}"

# --------------------------------------------------------------------------
# zlib
# --------------------------------------------------------------------------
ZLIB_DIR="${WORK_DIR}/zlib-${ZLIB_VER}"
ZLIB_INSTALL="${ZLIB_DIR}/install"
if [[ ! -f "${ZLIB_INSTALL}/lib/zlibstatic.lib" ]]; then
    log "building zlib ${ZLIB_VER}"
    if [[ ! -d "${ZLIB_DIR}" ]]; then
        zipname="zlib${ZLIB_VER//./}.zip"   # zlib1212.zip
        curl -fsSL "https://zlib.net/fossils/zlib-${ZLIB_VER}.tar.gz" -o "zlib-${ZLIB_VER}.tar.gz"
        tar xzf "zlib-${ZLIB_VER}.tar.gz"
    fi
    mkdir -p "${ZLIB_DIR}/build" && cd "${ZLIB_DIR}/build"
    cmake .. -G "${GENERATOR}" "${GEN_ARGS[@]}" "${POLICY_ARG[@]}" \
        -DCMAKE_INSTALL_PREFIX="${ZLIB_INSTALL}"
    cmake --build . -j "${JOBS}" --config Release --target install
    cd "${WORK_DIR}"
else
    log "zlib already built, skipping"
fi

# --------------------------------------------------------------------------
# xerces-c
# --------------------------------------------------------------------------
XERCES_DIR="${WORK_DIR}/xerces-c-${XERCES_VER}"
# NOT "${XERCES_DIR}/install": the xerces source tarball ships a plain-text file
# called INSTALL in its root, and Windows filesystems are case-insensitive, so an
# `install` prefix collides with it and CMake fails with
# "file cannot create directory: .../INSTALL/lib/pkgconfig. Maybe need
# administrative privileges." -- which reads like a permissions problem and is not
# one. generate_sumo_libs.sh picked `xerces-install` for the same reason.
XERCES_INSTALL="${XERCES_DIR}/xerces-install"
if [[ ! -f "${XERCES_INSTALL}/lib/xerces-c_3.lib" ]]; then
    log "building xerces-c ${XERCES_VER}"
    if [[ ! -d "${XERCES_DIR}" ]]; then
        curl -fsSL "https://archive.apache.org/dist/xerces/c/3/sources/xerces-c-${XERCES_VER}.tar.gz" \
            -o "xerces-c-${XERCES_VER}.tar.gz"
        tar xzf "xerces-c-${XERCES_VER}.tar.gz"
    fi
    mkdir -p "${XERCES_DIR}/build" && cd "${XERCES_DIR}/build"
    cmake .. -G "${GENERATOR}" "${GEN_ARGS[@]}" "${POLICY_ARG[@]}" \
        -DCMAKE_INSTALL_PREFIX="${XERCES_INSTALL}" \
        -DBUILD_SHARED_LIBS=ON
    cmake --build . -j "${JOBS}" --config Release --target install
    cd "${WORK_DIR}"
else
    log "xerces-c already built, skipping"
fi

# --------------------------------------------------------------------------
# SUMO source
# --------------------------------------------------------------------------
SUMO_DIR="${WORK_DIR}/sumo"
if [[ ! -d "${SUMO_DIR}" ]]; then
    log "cloning SUMO ${SUMO_TAG}"
    git clone https://github.com/eclipse-sumo/sumo.git --depth 1 --branch "${SUMO_TAG}" "${SUMO_DIR}"
else
    log "SUMO source already present, skipping clone"
fi

# --------------------------------------------------------------------------
# Optional: left-hand-traffic fix for the OpenDRIVE importer
# --------------------------------------------------------------------------
apply_lht_patch() {
    local f="${SUMO_DIR}/src/netimport/NIImporter_OpenDrive.cpp"
    if grep -q "GT_LHT_PATCH" "${f}"; then
        log "LHT patch already applied, skipping"
        return 0
    fi
    log "applying LHT patch to NIImporter_OpenDrive.cpp"
    "${REPO_ROOT}/DriverScript/.venv/Scripts/python.exe" \
        "${REPO_ROOT}/scripts/patch_sumo_lefthand.py" --file "${f}"
}
[[ "${LHT}" -eq 1 ]] && apply_lht_patch

# --------------------------------------------------------------------------
# MSVC compatibility (always -- this is what makes 1.6.0 build at all here)
# --------------------------------------------------------------------------
log "applying MSVC compatibility patch"
"${REPO_ROOT}/DriverScript/.venv/Scripts/python.exe"     "${REPO_ROOT}/scripts/patch_sumo_msvc_compat.py" --sumo-root "${SUMO_DIR}"

# --------------------------------------------------------------------------
# netconvert + duarouter
# --------------------------------------------------------------------------
BUILD_DIR="${SUMO_DIR}/build-netconvert"
mkdir -p "${BUILD_DIR}" && cd "${BUILD_DIR}"

# SUMO 1.6.0 predates C++17 and still derives from std::binary_function, which
# C++17 REMOVED and modern MSVC STLs no longer expose by default:
#   src/microsim/MSLane.h(95): error C2143: syntax error: ',' missing before '<'
# SUMO's own CMakeLists only sets -std=c++11 on the GCC/Clang branch, so MSVC is
# left on whatever its default is. Pin C++14 explicitly AND re-enable the
# deprecated adaptor typedefs -- the two are not redundant: the standard flag
# governs the language, the macro governs whether the MSVC STL still declares
# unary_function/binary_function at all.
# (generate_sumo_libs.sh sidesteps this by pinning the VS2017 toolset, -T v141;
# that toolset is not installed here, hence the source-level compatibility route.)
# NOTE the leading defaults. Passing -DCMAKE_CXX_FLAGS REPLACES CMake's own MSVC
# defaults ("/DWIN32 /D_WINDOWS /W3 /GR /EHsc") rather than appending to them, and
# dropping /DWIN32 sends src/foreign/tcpip/socket.cpp down its POSIX branch:
#   socket.cpp(21): fatal error C1083: cannot open include file 'sys/socket.h'
# which reads like a missing dependency and is actually a missing define.
# XERCES_STATIC_LIBRARY is REQUIRED because xerces is built static above. Without
# it the xerces headers declare everything __declspec(dllimport); MSVC still
# links that against a static lib without complaint, but the library's static
# initialisers never run. The failure surfaces far away and looks like corrupt
# input rather than a link problem: EVERY XML parse fails at "line/column 1/0",
# including netconvert's own built-in type map ("The types could not be loaded
# from 'built in type map'").
# xerces is built SHARED. A static xerces was tried first and rejected: it links
# cleanly, but every XML parse then fails at "line/column 1/0" with an EMPTY
# message -- its transcoder/message loader never initialise. Adding
# XERCES_STATIC_LIBRARY did not help. Shared is the configuration xerces tests.
CXX_COMPAT="/DWIN32 /D_WINDOWS /W3 /GR /EHsc /std:c++14 /D_HAS_DEPRECATED_ADAPTOR_TYPEDEFS=1 /D_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS"

log "configuring SUMO ${SUMO_TAG} (netconvert)"
cmake .. -G "${GENERATOR}" "${GEN_ARGS[@]}" "${POLICY_ARG[@]}" \
    -DCMAKE_CXX_FLAGS="${CXX_COMPAT}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DZLIB_INCLUDE_DIR="${ZLIB_INSTALL}/include" \
    -DZLIB_LIBRARY="${ZLIB_INSTALL}/lib/zlibstatic.lib" \
    -DXercesC_INCLUDE_DIR="${XERCES_INSTALL}/include" \
    -DXercesC_LIBRARY="${XERCES_INSTALL}/lib/xerces-c_3.lib" \
    -DXercesC_VERSION="${XERCES_VER}" \
    -DENABLE_PYTHON_BINDINGS=OFF \
    -DENABLE_JAVA_BINDINGS=OFF \
    -DCHECK_OPTIONAL_LIBS=OFF \
    -DPROJ_LIBRARY= \
    -DFOX_CONFIG=

# Only the two applications we need. Building ALL_BUILD would additionally
# compile the microsim/GUI targets, which we already have vendored and which
# roughly triples the build time for no benefit here.
for target in netconvert duarouter; do
    log "building ${target}"
    cmake --build . -j "${JOBS}" --config Release --target "${target}"
done

# --------------------------------------------------------------------------
# Stage
# --------------------------------------------------------------------------
log "staging into ${OUT_DIR}"
mkdir -p "${OUT_DIR}/bin"
for exe in netconvert duarouter; do
    # SUMO's CMake sets RUNTIME_OUTPUT_DIRECTORY to <sumo>/bin, so the binaries do
    # NOT land under the build tree -- searching BUILD_DIR finds nothing.
    src="$(find "${SUMO_DIR}/bin" "${BUILD_DIR}" -name "${exe}.exe" 2>/dev/null | head -1)"
    if [[ -z "${src}" ]]; then
        echo "ERROR: ${exe}.exe not found under ${BUILD_DIR}" >&2
        exit 1
    fi
    cp "${src}" "${OUT_DIR}/bin/"
    echo "  ${exe}.exe <- ${src}"
done

# xerces is a DLL now, so it must sit next to the executables or they will not start.
for dll in "${XERCES_INSTALL}"/bin/*.dll; do
    [[ -f "${dll}" ]] && cp "${dll}" "${OUT_DIR}/bin/" && echo "  $(basename "${dll}") <- ${dll}"
done

# tools/ carries randomTrips.py + sumolib, which B2 (demand generation) needs.
rm -rf "${OUT_DIR}/tools"
cp -r "${SUMO_DIR}/tools" "${OUT_DIR}/tools"

cat > "${OUT_DIR}/BUILD_INFO" <<INFO
sumo_tag=${SUMO_TAG}
zlib=${ZLIB_VER}
xerces=${XERCES_VER}
generator=${GENERATOR} (default toolset; v141 intentionally NOT used)
lht_patch=$([[ "${LHT}" -eq 1 ]] && echo yes || echo no)
built_from=${SUMO_DIR}
INFO

log "done"
cat "${OUT_DIR}/BUILD_INFO"
echo
echo "netconvert version:"
"${OUT_DIR}/bin/netconvert.exe" --version | head -3 || true
