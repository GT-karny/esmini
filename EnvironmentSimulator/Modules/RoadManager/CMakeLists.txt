# ############################### Setting targets ####################################################################

set(TARGET
    RoadManager)

# ############################### Loading desired rules ##############################################################

include(${CMAKE_SOURCE_DIR}/support/cmake/rule/disable_iwyu.cmake)
include(${CMAKE_SOURCE_DIR}/support/cmake/rule/enable_fpic.cmake)

if(MSVC)
    add_definitions("/wd4482")
endif()

# ############################### Setting target files ###############################################################

set(SOURCES
    odrSpiral.cpp
    LaneIndependentRouter.cpp)

# GT_esmini modification: Swap RoadManager.cpp with GT_RoadManager.cpp (LHT/RHT fixes)
message(STATUS "GT_esmini: Swapping RoadManager.cpp with GT_RoadManager.cpp")
list(
    APPEND
    SOURCES
    "${CMAKE_SOURCE_DIR}/GT_esmini/src/road/GT_RoadManager.cpp")

# [GT_ODR:cmake] R1 exception (approved 2026-07-02, plan GT_esmini/docs/opendrive_16_19_support_plan.md P1 / §10.1): compile the GT OpenDRIVE
# side-model module INTO this upstream RoadManager static target. Explicit list (NOT a glob) so it stays in lockstep with the manifest and never
# drifts. These sources are NOT added to GT_esmini/CMakeLists.txt's GT_esminiLib source lists, so there is no double compilation / duplicate symbols
# (GT_esminiLib links RoadManager, which already carries them).
list(
    APPEND
    SOURCES
    "${CMAKE_SOURCE_DIR}/GT_esmini/src/road/odr_side/OdrSideModel.cpp"
    "${CMAKE_SOURCE_DIR}/GT_esmini/src/road/odr_side/OdrSideParser.cpp"
    "${CMAKE_SOURCE_DIR}/GT_esmini/src/road/odr_side/OdrCoverageAudit.cpp"
    "${CMAKE_SOURCE_DIR}/GT_esmini/src/road/odr_side/OdrLaneExtras.cpp"
    "${CMAKE_SOURCE_DIR}/GT_esmini/src/road/odr_side/OdrSignalExtras.cpp"
    "${CMAKE_SOURCE_DIR}/GT_esmini/src/road/odr_side/OdrJunctionExtras.cpp"
    "${CMAKE_SOURCE_DIR}/GT_esmini/src/road/odr_side/OdrObjectExtras.cpp"
    "${CMAKE_SOURCE_DIR}/GT_esmini/src/road/odr_side/OdrJunctionGeom.cpp"
    "${CMAKE_SOURCE_DIR}/GT_esmini/src/road/odr_side/OdrLaneLayers.cpp"
    "${CMAKE_SOURCE_DIR}/GT_esmini/src/road/odr_side/OdrRailroad.cpp")

add_library(
    pugixml_lib
    ${EXTERNALS_PUGIXML_PATH}/pugixml.cpp)

set(INCLUDES
    RoadManager.hpp
    odrSpiral.h
    LaneIndependentRouter.hpp)

# ############################### Creating library ###################################################################

add_library(
    ${TARGET}
    STATIC
    ${SOURCES}
    ${SRC_ADDITIONAL}
    ${INCLUDES})

target_link_libraries(
    ${TARGET}
    PRIVATE CommonMini
            pugixml_lib
            project_options
            fmt)

target_include_directories(
    ${TARGET}
    PRIVATE ${COMMON_MINI_PATH}
            # [GT_ODR:cmake] GT side-model public headers (gt_esmini/road/OdrSideModel.hpp).
            ${CMAKE_SOURCE_DIR}/GT_esmini/include)

target_include_directories(
    ${TARGET}
    SYSTEM
    PUBLIC ${EXTERNALS_PUGIXML_PATH}
           ${EXTERNALS_FMT_INCLUDES}
           ${ROAD_MANAGER_PATH})

disable_iwyu(${TARGET})
enable_fpic(${TARGET})
