# This is a minimalistic example of how to create build files for an application making use of the esmini shared library.
#
# Make sure that the headerfile esminiLib.hpp and library esminiLib (exact filename depending on platform) is available, either in this folder or in
# the original location (relative this folder).
#
# NOTE (GT_esmini): This is the GT variant of EnvironmentSimulator/Libraries/esminiJS,
# relocated out of the upstream core to keep the core esminiJS vanilla (audit BND-1 / R5-U1).
# Relative paths below differ from the upstream original: this project lives at
# GT_esmini/web/wasm, so the repo root is 3 levels up. Core esmini modules are reached via
# ../../../EnvironmentSimulator/..., repo-root externals via ../../../externals/... (unchanged,
# both old and new locations are 3 levels deep), and GT_esmini sources via ../../... .
message(
    STATUS "CMake version: "
           ${CMAKE_VERSION})
cmake_minimum_required(
    VERSION 3.5.0
    FATAL_ERROR)

set(TARGET
    esmini)
set(CMAKE_CXX_STANDARD
    17)
# set(CMAKE_BUILD_TYPE "Debug") set(CMAKE_CXX_FLAGS_DEBUG  "$ENV{CXXFLAGS} -O0 -Wall -g")             # 调试包不优化

project(${TARGET})

set(SCENARIOENGINE_INCLUDE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../EnvironmentSimulator/Modules/ScenarioEngine/SourceFiles"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../EnvironmentSimulator/Modules/ScenarioEngine/OSCTypeDefs")
set(CONTROLLERS_INCLUDE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../EnvironmentSimulator/Modules/Controllers")
set(ROADMANAGER_INCLUDE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../EnvironmentSimulator/Modules/RoadManager")
set(PLAYERBASE_INCLUDE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../EnvironmentSimulator/Modules/PlayerBase")
set(COMMON_MINI_INCLUDE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../EnvironmentSimulator/Modules/CommonMini")
set(PUGIXML_INCLUDE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../externals/pugixml")
set(EXPR_INCLUDE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../externals/expr")
set(FMT_INCLUDE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../externals/fmt/include")

set(YAML_INCLUDE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../externals/yaml")

set(EXPR_INCLUDE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../externals/expr")

file(
    GLOB
    CONTROLLERS_SOURCES
    "../../../EnvironmentSimulator/Modules/Controllers/*.cpp")

file(
    GLOB
    ROADMANAGER_SOURCES
    "../../../EnvironmentSimulator/Modules/RoadManager/*.cpp")

# GT_esmini LHT/RHT patch: swap RoadManager.cpp with GT_RoadManager.cpp (mirrors
# the swap in EnvironmentSimulator/Modules/RoadManager/CMakeLists.txt, which
# this file bypasses via its own glob).
list(
    REMOVE_ITEM
    ROADMANAGER_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../EnvironmentSimulator/Modules/RoadManager/RoadManager.cpp")
list(
    APPEND
    ROADMANAGER_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/road/GT_RoadManager.cpp")
message(STATUS "esminiJS: swapping RoadManager.cpp with GT_RoadManager.cpp")

file(
    GLOB
    PLAYERBASE_SOURCES
    "../../../EnvironmentSimulator/Modules/PlayerBase/*.cpp")

file(
    GLOB
    SCENARIOENGINE_SOURCES
    "../../../EnvironmentSimulator/Modules/ScenarioEngine/SourceFiles/*.[hc]pp"
    "../../../EnvironmentSimulator/Modules/ScenarioEngine/SourceFiles/*.[hc]"
    "../../../EnvironmentSimulator/Modules/ScenarioEngine/OSCTypeDefs/*.[hc]pp")

file(
    GLOB
    COMMONMINI_SOURCES
    "../../../EnvironmentSimulator/Modules/CommonMini/*.cpp")

# Drop the version.cpp that a regular esmini build generates into the source
# tree (via version.cmake): the WASM build supplies its own version symbols
# through the generated esmini_version.cpp below, so keeping both would cause
# duplicate-symbol link errors (ESMINI_GIT_REV, ESMINI_BUILD_VERSION, ...).
list(
    REMOVE_ITEM
    COMMONMINI_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../EnvironmentSimulator/Modules/CommonMini/version.cpp")

file(
    GLOB
    PUGIXML_SOURCES
    "../../../externals/pugixml/*.cpp")

# Define FMT source files (os.cc excluded: OS-specific I/O not available in WASM)
set(FMT_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../externals/fmt/src/format.cc")

list(
    REMOVE_ITEM
    CONTROLLERS_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../EnvironmentSimulator/Modules/Controllers/ControllerSumo.cpp")
list(
    REMOVE_ITEM
    CONTROLLERS_INCLUDE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../EnvironmentSimulator/Modules/Controllers/ControllerSumo.hpp")

# Remove GUI-related files not suitable for WASM
list(
    REMOVE_ITEM
    PLAYERBASE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../EnvironmentSimulator/Modules/PlayerBase/Plot.cpp")

list(
    REMOVE_ITEM
    SCENARIOENGINE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../EnvironmentSimulator/Modules/ScenarioEngine/SourceFiles/OSIReporter.hpp")
list(
    REMOVE_ITEM
    SCENARIOENGINE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../EnvironmentSimulator/Modules/ScenarioEngine/SourceFiles/OSIReporter.cpp")

# Remove OSI-related files
list(
    REMOVE_ITEM
    SCENARIOENGINE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../EnvironmentSimulator/Modules/ScenarioEngine/SourceFiles/OSITrafficCommand.hpp")
list(
    REMOVE_ITEM
    SCENARIOENGINE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../EnvironmentSimulator/Modules/ScenarioEngine/SourceFiles/OSITrafficCommand.cpp")

# GT_esmini extension include path (for gt_esmini/scenario/*.hpp)
set(GT_ESMINI_INCLUDE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include")

# GT_esmini scenario extension sources (WASM-compatible, no OSI/OSG/Python deps)
set(GT_SCENARIO_WASM_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/scenario/ExtraAction.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/scenario/ExtraCondition.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/scenario/ExtraEntities.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/scenario/GT_ScenarioReader.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/scenario/TrafficSignalController.cpp")

# GT_esmini route embind binding (LaneIndependentRouter -> GTRouteJS)
# gt_embind_route.cpp now lives alongside this CMakeLists.
set(GT_ROUTE_WASM_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/gt_embind_route.cpp")

include_directories(
    ${SCENARIOENGINE_INCLUDE_DIRS}
    ${CONTROLLERS_INCLUDE_DIR}
    ${ROADMANAGER_INCLUDE_DIR}
    ${PLAYERBASE_INCLUDE_DIR}
    ${COMMON_MINI_INCLUDE_DIR}
    ${PUGIXML_INCLUDE_DIR}
    ${EXPR_INCLUDE_DIR}
    ${FMT_INCLUDE_DIR}
    ${YAML_INCLUDE_DIR}
    ${GT_ESMINI_INCLUDE_DIR}
    esminijs.hpp)

if(EMSCRIPTEN)
    # Generate version symbols required by CommonMini.cpp
    # (normally generated by version.cmake in the main esmini build)
    file(
        WRITE
        "${CMAKE_CURRENT_BINARY_DIR}/esmini_version.cpp"
        "const char* ESMINI_GIT_TAG = \"wasm\";\n\
const char* ESMINI_GIT_REV = \"0\";\n\
const char* ESMINI_GIT_BRANCH = \"feature/esmini-js-editor-playback\";\n\
const char* ESMINI_BUILD_VERSION = \"wasm-1.0\";\n")

    message(
        "build "
        ${TARGET}
        ".js")
    set(CMAKE_EXECUTABLE_SUFFIX
        ".js")
    add_executable(
        ${TARGET}
        embind.cpp
        embind_rm.cpp
        esminijs.cpp
        "${CMAKE_CURRENT_BINARY_DIR}/esmini_version.cpp"
        ${CONTROLLERS_SOURCES}
        ${ROADMANAGER_SOURCES}
        ${PLAYERBASE_SOURCES}
        ${SCENARIOENGINE_SOURCES}
        ${COMMONMINI_SOURCES}
        ${PUGIXML_SOURCES}
        ${FMT_SOURCES}
        ${GT_SCENARIO_WASM_SOURCES}
        ${GT_ROUTE_WASM_SOURCES})

    set_target_properties(
        ${TARGET}
        PROPERTIES
            LINK_FLAGS
            "\
      --bind \
      -s ENVIRONMENT=web,worker \
      -s MODULARIZE=1 \
      -s EXPORT_NAME=\"esmini\" \
      -s EXPORTED_RUNTIME_METHODS=[\"cwrap\",\"FS\"] \
      -s FORCE_FILESYSTEM=1 \
      -s ALLOW_MEMORY_GROWTH=1 \
      -s WASM_ASYNC_COMPILATION=0 \
      -s WASM=1 \
      -s SINGLE_FILE=1 \
      -s EXCEPTION_CATCHING_ALLOWED=[..] \
  ")
endif()
