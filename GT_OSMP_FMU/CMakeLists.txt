cmake_minimum_required(VERSION 3.10)

set(TARGET esmini_fmu)
# [GT_MOD] BLD-1: no project() call here. This directory is consumed exclusively via
# add_subdirectory() from the repository root (the standalone GT_OSMP_FMU/build flow is
# superseded). A nested project() spawns a separate sub-solution under the VS generator,
# which breaks `cmake --build build --target esmini_fmu` path resolution (MSB1009: CMake
# invokes MSBuild from the build root with a bare esmini_fmu.vcxproj filename).

execute_process(
    # fetch latest esmini tag (release)
    COMMAND git describe --abbrev=0
    OUTPUT_VARIABLE VERSION
    ERROR_QUIET)

# Strip prefix "v" and any trailing line break
# [GT_MOD] START: Fix VERSION parsing if git describe fails or returns empty
if(VERSION)
    string(LENGTH "${VERSION}" VERSION_LEN)
    if(VERSION_LEN GREATER 1)
        string(SUBSTRING "${VERSION}" 1 -1 VERSION)
    endif()
    string(STRIP "${VERSION}" VERSION)
else()
    set(VERSION "0.0.0")
endif()
# [GT_MOD] END

set(OSIVERSION "3.5.0")
set(OSMPVERSION "1.4.0")

set(STATIC_LINKING TRUE)

if(LINUX)
    message("LINUX")
endif()

if(UNIX AND NOT APPLE)
    # assume Linux
    set(LINUX TRUE)
endif()

if(MSVC)
    set(EXT_INCL_DIR v10)
    set(CONFIG_FOLDER "Release")
    set(ADDITIONAL_LIBS Ws2_32 winmm)
    set(LIB_PREFIX "lib")
    set(LIB_POSTFIX "")
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(FMI_BINARIES_PLATFORM "win64")
    else()
        set(FMI_BINARIES_PLATFORM "win32")
    endif()
elseif(APPLE)
    set(EXT_INCL_DIR mac)
    set(CONFIG_FOLDER "")
    set(ADDITIONAL_LIBS "")
    set(LIB_PREFIX "")
    set(LIB_POSTFIX .a)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(FMI_BINARIES_PLATFORM "darwin64")
    else()
        set(FMI_BINARIES_PLATFORM "darwin32")
    endif()
elseif(LINUX)
    set(EXT_INCL_DIR linux)
    set(CONFIG_FOLDER "")
    set(ADDITIONAL_LIBS "")
    set(LIB_PREFIX "")
    set(LIB_POSTFIX .a)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(FMI_BINARIES_PLATFORM "linux64")
    else()
        set(FMI_BINARIES_PLATFORM "linux32")
    endif()
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(LINK_WITH_SHARED_OSI
    OFF
    CACHE BOOL "Link FMU with shared OSI library instead of statically linking")
set(PUBLIC_LOGGING
    OFF
    CACHE BOOL "Enable logging via FMI logger")
set(PRIVATE_LOGGING
    OFF
    CACHE BOOL "Enable private logging to file")
if(WIN32)
    set(PRIVATE_LOG_PATH_SOURCE
        "C:/TEMP/OSMPDummySourceLog.log"
        CACHE FILEPATH "Path to write private log file to")
else()
    set(PRIVATE_LOG_PATH_SOURCE
        "/tmp/OSMPDummySourceLog.log"
        CACHE FILEPATH "Path to write private log file to")
endif()
if(PRIVATE_LOGGING)
    file(TO_NATIVE_PATH ${PRIVATE_LOG_PATH_SOURCE} PRIVATE_LOG_PATH_SOURCE_NATIVE)
    string(REPLACE "\\" "\\\\" PRIVATE_LOG_PATH ${PRIVATE_LOG_PATH_SOURCE_NATIVE})
endif()
set(VERBOSE_FMI_LOGGING
    OFF
    CACHE BOOL "Enable detailed FMI function logging")
set(DEBUG_BREAKS
    OFF
    CACHE BOOL "Enable debugger traps for debug builds of FMU")

string(TIMESTAMP FMUTIMESTAMP UTC)
string(MD5 FMUGUID modelDescription.in.xml)
configure_file(modelDescription.in.xml modelDescription.xml @ONLY)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../EnvironmentSimulator/Libraries/esminiLib
                    ${CMAKE_CURRENT_SOURCE_DIR}/../externals/osi/${EXT_INCL_DIR}/include
                    # [GT_MOD] START: Add GT_esmini include
                    ${CMAKE_CURRENT_SOURCE_DIR}/../GT_esmini/include
                    ${CMAKE_CURRENT_SOURCE_DIR}/../EnvironmentSimulator/Modules/CommonMini
                    ${CMAKE_CURRENT_SOURCE_DIR}/../EnvironmentSimulator/Modules/ScenarioEngine/SourceFiles
                    ${CMAKE_CURRENT_SOURCE_DIR}/../EnvironmentSimulator/Modules/ScenarioEngine/OSCTypeDefs
                    ${CMAKE_CURRENT_SOURCE_DIR}/../EnvironmentSimulator/Modules/PlayerBase
                    ${CMAKE_CURRENT_SOURCE_DIR}/../EnvironmentSimulator/Modules/ViewerBase
                    ${CMAKE_CURRENT_SOURCE_DIR}/../EnvironmentSimulator/Modules/Controllers
                    ${CMAKE_CURRENT_SOURCE_DIR}/../EnvironmentSimulator/Modules/RoadManager
                    ${CMAKE_CURRENT_SOURCE_DIR}/../externals/pugixml
                    # [GT_MOD] END
                    )

link_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../bin
    ${CMAKE_CURRENT_SOURCE_DIR}/../externals/osi/${EXT_INCL_DIR}/lib/release
    ${CMAKE_CURRENT_SOURCE_DIR}/../externals/sumo/${EXT_INCL_DIR}/lib
    ${CMAKE_CURRENT_SOURCE_DIR}/../externals/osg/${EXT_INCL_DIR}/lib
    ${CMAKE_CURRENT_SOURCE_DIR}/../externals/osg/${EXT_INCL_DIR}/lib/osgPlugins-3.6.5
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/EnvironmentSimulator/Modules/ScenarioEngine/${CONFIG_FOLDER}
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/EnvironmentSimulator/Modules/PlayerBase/${CONFIG_FOLDER}
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/EnvironmentSimulator/Modules/RoadManager/${CONFIG_FOLDER}
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/EnvironmentSimulator/Modules/Controllers/${CONFIG_FOLDER}
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/EnvironmentSimulator/Modules/CommonMini/${CONFIG_FOLDER}
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/EnvironmentSimulator/Modules/ViewerBase/${CONFIG_FOLDER}
    # [GT_MOD] START: Add GT_esmini build dir
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/GT_esmini/${CONFIG_FOLDER}
    # [GT_MOD] END
    )

include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../externals/fmi2/headers)

# [GT_MOD] BLD-1/SUB-3 repair: the FMU no longer re-compiles a hand-copied GT source
# list (which had drifted from GT_esmini/CMakeLists.txt and left GT_* symbols
# unresolved). It links the in-tree GT_esminiLib_static instead. That static lib is the
# god-TU build of GT_esmini (GT_esminiLib.cpp #includes esminiLib.cpp), so it provides
# BOTH the GT_* extension symbols AND the SE_* core API, and it transitively carries the
# PlayerBase/ScenarioEngine/Controllers/RoadManager/CommonMini/ViewerBase/RoadGeom +
# OSG/SUMO/OSI/fmt/pugixml usage requirements (link + include). No separate core
# source/lib list is needed here — single source of truth is GT_esmini/CMakeLists.txt.
add_library(${TARGET} SHARED esmini.cpp)
set_target_properties(${TARGET} PROPERTIES PREFIX "")
target_compile_definitions(${TARGET} PRIVATE "FMU_SHARED_OBJECT")
set_target_properties(${TARGET} PROPERTIES OUTPUT_NAME "esmini")
target_compile_definitions(${TARGET} PRIVATE GT_ESMINI_STATIC)
target_compile_definitions(${TARGET} PRIVATE _USE_OSI)
# Objects of the static lib are linked into this shared lib; keep them PIC (Linux).
set_target_properties(${TARGET} PROPERTIES POSITION_INDEPENDENT_CODE ON)

message("Link esmini via GT_esminiLib_static (in-tree static GT library)")
target_link_libraries(${TARGET} PRIVATE GT_esminiLib_static ${ADDITIONAL_LIBS})

add_custom_command(
    TARGET ${TARGET}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_CURRENT_BINARY_DIR}/buildfmu"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/buildfmu/sources"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/buildfmu/binaries/${FMI_BINARIES_PLATFORM}"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_BINARY_DIR}/modelDescription.xml" "${CMAKE_CURRENT_BINARY_DIR}/buildfmu"
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${TARGET}> $<$<PLATFORM_ID:Windows>:$<$<CONFIG:Debug>:$<TARGET_PDB_FILE:${TARGET}>>>
            "${CMAKE_CURRENT_BINARY_DIR}/buildfmu/binaries/${FMI_BINARIES_PLATFORM}"
    COMMAND
        ${CMAKE_COMMAND} -E chdir "${CMAKE_CURRENT_BINARY_DIR}/buildfmu" ${CMAKE_COMMAND} -E tar "cfv" "../esmini.fmu" --format=zip
        "modelDescription.xml" "${CMAKE_CURRENT_BINARY_DIR}/buildfmu/sources"
        "${CMAKE_CURRENT_BINARY_DIR}/buildfmu/binaries/${FMI_BINARIES_PLATFORM}")
