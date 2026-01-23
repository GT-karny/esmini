# ############################### Setting targets ###################################################################################################

set(TARGET_SHARED GT_esminiLib)
set(TARGET_STATIC GT_esminiLib_static)

# ############################### Loading desired rules ##############################################################################################

include(${CMAKE_SOURCE_DIR}/support/cmake/rule/disable_static_analysis.cmake)
include(${CMAKE_SOURCE_DIR}/support/cmake/rule/disable_iwyu.cmake)

# ############################### Setting target files ###############################################################################################

set(SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/ExtraAction.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ExtraEntities.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GT_ScenarioReader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AutoLightController.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RealVehicle.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ControllerRealDriver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TerrainTracker.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GT_HostVehicleReporter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GT_esminiLib.cpp)

set(INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/ExtraAction.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ExtraEntities.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GT_ScenarioReader.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AutoLightController.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RealVehicle.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ControllerRealDriver.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TerrainTracker.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GT_HostVehicleReporter.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GT_esminiLib.hpp)

# ############################### Creating library ##################################################################################################

add_library(${TARGET_SHARED} SHARED ${SOURCES} ${INCLUDES})

# embed $origin (location of exe file) as execution dyn lib search paths
set(RPATH_DIRS "$ORIGIN")

if(DYN_PROTOBUF)
    # add OSI library folder to execution lib search paths
    set(RPATH_DIRS ${RPATH_DIRS}:${EXTERNALS_OSI_LIBRARY_PATH}/$<IF:$<CONFIG:Debug>,debug,release>)
endif()

set_target_properties(
    ${TARGET_SHARED}
    PROPERTIES BUILD_WITH_INSTALL_RPATH true
               INSTALL_RPATH "${RPATH_DIRS}")

add_library(${TARGET_STATIC} STATIC ${SOURCES} ${INCLUDES})
target_compile_definitions(${TARGET_STATIC} PRIVATE GT_ESMINI_STATIC _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN NOGDI)
target_compile_definitions(${TARGET_SHARED} PRIVATE _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN NOGDI)

foreach(TARGET IN ITEMS ${TARGET_SHARED} ${TARGET_STATIC})

    target_link_libraries(${TARGET} PRIVATE project_options)

    target_include_directories(
        ${TARGET}
        PRIVATE ${SCENARIO_ENGINE_PATH}/SourceFiles
                ${SCENARIO_ENGINE_PATH}/OSCTypeDefs
                ${VIEWER_BASE_PATH}
                ${PLAYER_BASE_PATH}
                ${COMMON_MINI_PATH}
                ${CONTROLLERS_PATH}
                ${CMAKE_CURRENT_SOURCE_DIR})

    target_include_directories(
        ${TARGET}
        SYSTEM
        PUBLIC ${ROAD_MANAGER_PATH}
               ${EXTERNALS_OSI_INCLUDES}
               ${EXTERNALS_OSG_INCLUDES}
               ${EXTERNALS_PUGIXML_PATH})

    target_link_libraries(
        ${TARGET}
        PRIVATE PlayerBase
                ScenarioEngine
                CommonMini
                Controllers
                RoadManager
                ${TIME_LIB}
                ${SOCK_LIB})

    if(USE_OSG)
        target_link_libraries(${TARGET} PRIVATE ViewerBase RoadGeom ${OSG_LIBRARIES})
    endif()

    if(USE_SUMO)
        target_link_libraries(${TARGET} PRIVATE ${SUMO_LIBRARIES})
    endif()

    if(USE_OSI)
        target_link_libraries(${TARGET} PRIVATE ${OSI_LIBRARIES})
        target_compile_definitions(${TARGET} PRIVATE _USE_OSI)
    endif()

    disable_static_analysis(${TARGET})
    disable_iwyu(${TARGET})

endforeach()

# ############################### Install############################################################################################################

install(
    TARGETS ${TARGET_SHARED} ${TARGET_STATIC}
    DESTINATION "${INSTALL_PATH}")

if(BUILD_EXAMPLES)
    install(
        TARGETS ${TARGET_SHARED} ${TARGET_STATIC}
        DESTINATION "${CODE_EXAMPLES_BIN_PATH}")
endif(BUILD_EXAMPLES)

# ############################### Add test subdirectory #########################################################################################

# if(USE_GTEST)
#     add_subdirectory(test)
# endif()

# GT_Sim Executable
set(GT_SIM_TARGET GT_Sim)
set(GT_SIM_SOURCES GT_Sim/main.cpp)

add_executable(${GT_SIM_TARGET} ${GT_SIM_SOURCES})

target_link_libraries(${GT_SIM_TARGET} PRIVATE GT_esminiLib)

# Include directories needed for GT_Sim (esminiLib headers)
target_include_directories(${GT_SIM_TARGET} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SCENARIO_ENGINE_PATH}/SourceFiles
    ${SCENARIO_ENGINE_PATH}/OSCTypeDefs
    ${VIEWER_BASE_PATH}
    ${PLAYER_BASE_PATH}
    ${COMMON_MINI_PATH}
    ${CONTROLLERS_PATH}
    ${CMAKE_SOURCE_DIR}/EnvironmentSimulator/Libraries/esminiLib
)

install(
    TARGETS ${GT_SIM_TARGET}
    DESTINATION "${INSTALL_PATH}")

if(BUILD_EXAMPLES)
    install(
        TARGETS ${GT_SIM_TARGET}
        DESTINATION "${CODE_EXAMPLES_BIN_PATH}")
endif(BUILD_EXAMPLES)
