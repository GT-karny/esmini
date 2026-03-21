# ############################### Setting targets ###################################################################################################

set(TARGET_SHARED GT_esminiLib)
set(TARGET_STATIC GT_esminiLib_static)
set(GT_CORE_TARGET gt_core)
set(GT_IO_TARGET gt_io)
set(GT_SCENARIO_TARGET gt_scenario)
set(GT_CONTROL_TARGET gt_control)
set(GT_OSI_TARGET gt_osi)

# ############################### Loading desired rules ##############################################################################################

include(${CMAKE_SOURCE_DIR}/support/cmake/rule/disable_static_analysis.cmake)
include(${CMAKE_SOURCE_DIR}/support/cmake/rule/disable_iwyu.cmake)

# ############################### Mandatory options ##################################################################################################

# OSI support is mandatory for GT_esmini builds (HostVehicleData, GroundTruth).
if(NOT USE_OSI)
    message(STATUS "GT_esmini: Forcing USE_OSI=ON (required for GT_esmini)")
    set(USE_OSI ON CACHE BOOL "Use OSI" FORCE)
    include(${CMAKE_SOURCE_DIR}/support/cmake/external/osi.cmake)
    set_osi_libs()
endif()

# OSG (OpenSceneGraph) is mandatory for GT_esmini builds (3D viewer).
if(NOT USE_OSG)
    message(STATUS "GT_esmini: Forcing USE_OSG=ON (required for GT_esmini)")
    set(USE_OSG ON CACHE BOOL "Use OSG" FORCE)
    include(${CMAKE_SOURCE_DIR}/support/cmake/external/osg.cmake)
    set_osg_libs()
endif()

# PythonDriverController is mandatory for GT_esmini builds.
# This option is intentionally removed to avoid producing non-PythonDriver binaries.
if(DEFINED GT_ENABLE_EMBEDDED_PYTHON)
    if(NOT GT_ENABLE_EMBEDDED_PYTHON)
        message(FATAL_ERROR
            "GT_ENABLE_EMBEDDED_PYTHON option is removed. "
            "PythonDriverController support is always enabled and cannot be configured.")
    endif()
    # Clean up legacy cache entries from older build trees.
    unset(GT_ENABLE_EMBEDDED_PYTHON CACHE)
    unset(GT_ENABLE_EMBEDDED_PYTHON)
endif()

# Prefer bundled embeddable Python (primarily on Windows).
set(GT_EMBEDDED_PYTHON_HOME "${CMAKE_SOURCE_DIR}/thirdparty/python-embed/python-3.12.10-embed-amd64")
if(EXISTS "${GT_EMBEDDED_PYTHON_HOME}")
    set(Python3_ROOT_DIR "${GT_EMBEDDED_PYTHON_HOME}" CACHE PATH "Embedded Python root")
    set(Python3_FIND_STRATEGY LOCATION)
    set(Python3_FIND_REGISTRY NEVER)
endif()
find_package(Python3 REQUIRED COMPONENTS Development)
message(STATUS "GT_esmini: Embedded Python enabled (${Python3_EXECUTABLE}, version ${Python3_VERSION})")
message(STATUS "GT_esmini: Python include: ${Python3_INCLUDE_DIRS}")
message(STATUS "GT_esmini: Python library: ${Python3_LIBRARIES}")

# ############################### Optional: SDL2 for racing wheel FFB support ########################################################################

option(GT_ENABLE_SDL2 "Enable SDL2 for racing wheel input and force feedback" OFF)

if(GT_ENABLE_SDL2)
    set(SDL2_DIR "${CMAKE_SOURCE_DIR}/thirdparty/SDL2/cmake")
    find_package(SDL2 REQUIRED)
    message(STATUS "GT_esmini: SDL2 enabled (${SDL2_VERSION})")
endif()

# ############################### Setting target files ###############################################################################################

set(GT_CORE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/GT_esminiLib.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/GT_esminiRMLib.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/ConfigLoader.cpp)

set(GT_CORE_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/core/GT_esminiLib.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/core/GT_esminiRMLib.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/core/ConfigLoader.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/core/IConfigLoader.hpp)

set(GT_IO_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/GT_UDP.cpp)

set(GT_IO_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/io/GT_UDP.hpp)

set(GT_SCENARIO_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/scenario/ExtraAction.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/scenario/ExtraCondition.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/scenario/ExtraEntities.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/scenario/GT_ScenarioReader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/scenario/TrafficSignalController.cpp)

set(GT_SCENARIO_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/scenario/ExtraAction.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/scenario/ExtraCondition.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/scenario/ExtraEntities.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/scenario/GT_ScenarioReader.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/scenario/TrafficSignalController.hpp)

set(GT_CONTROL_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/AutoLightController.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/RealVehicle.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/ControllerRealDriver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/realdriver/DriverOutputPort.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/realdriver/LatPathPlanner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/realdriver/LonProfilePlanner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/realdriver/RealDriverCoordinator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/TerrainTracker.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/DriverInputReceiver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/VehicleStateUpdater.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/EsminiStateApplier.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/ControlDecisionEngine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/ObservedVehiclePhysics.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/VehiclePhysicsManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/ControllerManualDrive.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/manualdrive/ManualDriveConfig.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/manualdrive/OverrideManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/manualdrive/RealVehicleBackend.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/manualdrive/HVDStateApplier.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/manualdrive/ManualDriveCoordinator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/manualdrive/UdpTransport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/manualdrive/TcpTransport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/manualdrive/NetworkInputBridge.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/manualdrive/NetworkPhysicsBridge.cpp)

set(GT_CONTROL_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/AutoLightController.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/RealVehicle.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/ControllerRealDriver.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/ControllerRealDriverUtils.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/realdriver/DriverOutputPort.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/realdriver/LatPathPlanner.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/realdriver/LonProfilePlanner.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/realdriver/RealDriverCoordinator.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/TerrainTracker.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/DriverInputReceiver.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/VehicleStateUpdater.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/EsminiStateApplier.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/ControlDecisionEngine.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/ObservedVehiclePhysics.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/VehiclePhysicsManager.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/ControllerManualDrive.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/ManualDriveTypes.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/IInputSource.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/IPhysicsBackend.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/IFFBSink.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/ITransport.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/NullFFBSink.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/StubInputSource.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/ManualDriveConfig.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/OverrideManager.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/RealVehicleBackend.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/HVDStateApplier.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/ManualDriveCoordinator.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/UdpTransport.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/TcpTransport.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/NetworkInputBridge.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/NetworkPhysicsBridge.hpp)

# Embedded Python controller sources (always enabled)
list(APPEND GT_CONTROL_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/ControllerPythonDriver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/pythondriver/PythonDriverBridge.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/pythondriver/PythonDriverCoordinator.cpp)
list(APPEND GT_CONTROL_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/ControllerPythonDriver.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/pythondriver/PythonDriverBridge.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/pythondriver/PythonDriverCoordinator.hpp)

# SDL2 racing wheel sources (conditional)
if(GT_ENABLE_SDL2)
    list(APPEND GT_CONTROL_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/control/manualdrive/SDL2WheelInput.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/control/manualdrive/SDLFFBSink.cpp)
    list(APPEND GT_CONTROL_HEADERS
        ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/SDL2WheelInput.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/SDLFFBSink.hpp)
endif()

set(GT_OSI_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/osi/GT_HostVehicleReporter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/osi/HVDEstimator.cpp)

set(GT_OSI_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/osi/GT_HostVehicleReporter.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/osi/IHostVehicleDataProvider.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/osi/IGroundTruthPublisher.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/osi/HVDEstimator.hpp)

# ############################### Creating library ##################################################################################################
add_library(${GT_CORE_TARGET} STATIC ${GT_CORE_SOURCES} ${GT_CORE_HEADERS})
add_library(${GT_IO_TARGET} STATIC ${GT_IO_SOURCES} ${GT_IO_HEADERS})
add_library(${GT_SCENARIO_TARGET} STATIC ${GT_SCENARIO_SOURCES} ${GT_SCENARIO_HEADERS})
add_library(${GT_CONTROL_TARGET} STATIC ${GT_CONTROL_SOURCES} ${GT_CONTROL_HEADERS})
add_library(${GT_OSI_TARGET} STATIC ${GT_OSI_SOURCES} ${GT_OSI_HEADERS})

set(GT_INTERNAL_TARGETS
    ${GT_CORE_TARGET}
    ${GT_IO_TARGET}
    ${GT_SCENARIO_TARGET}
    ${GT_CONTROL_TARGET}
    ${GT_OSI_TARGET})

add_library(${TARGET_SHARED} SHARED)
add_library(${TARGET_STATIC} STATIC)

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

target_compile_definitions(${TARGET_STATIC} PRIVATE GT_ESMINI_STATIC _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN NOGDI)
target_compile_definitions(${TARGET_SHARED} PRIVATE _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN NOGDI)

foreach(TARGET IN ITEMS ${GT_INTERNAL_TARGETS} ${TARGET_SHARED} ${TARGET_STATIC})

    target_link_libraries(${TARGET} PRIVATE project_options)

    target_include_directories(
        ${TARGET}
        PRIVATE ${SCENARIO_ENGINE_PATH}/SourceFiles
                ${SCENARIO_ENGINE_PATH}/OSCTypeDefs
                ${VIEWER_BASE_PATH}
                ${PLAYER_BASE_PATH}
                ${COMMON_MINI_PATH}
                ${CONTROLLERS_PATH}
                ${CMAKE_CURRENT_SOURCE_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR}/include)

    target_include_directories(
        ${TARGET}
        SYSTEM
        PUBLIC ${ROAD_MANAGER_PATH}
               ${EXTERNALS_OSI_INCLUDES}
               ${EXTERNALS_OSG_INCLUDES}
               ${EXTERNALS_PUGIXML_PATH}
               ${EXTERNALS_FMT_INCLUDES})

    if(USE_OSG)
        target_link_libraries(${TARGET} PRIVATE ${OSG_LIBRARIES})
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

foreach(TARGET IN ITEMS ${GT_INTERNAL_TARGETS})
    target_compile_definitions(${TARGET} PRIVATE GT_ESMINI_STATIC)
endforeach()

target_include_directories(${TARGET_SHARED} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(${TARGET_STATIC} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(${GT_IO_TARGET} PRIVATE ${GT_CORE_TARGET} ${SOCK_LIB})
target_link_libraries(${GT_SCENARIO_TARGET} PRIVATE ${GT_CORE_TARGET} ScenarioEngine CommonMini RoadManager ${TIME_LIB} ${SOCK_LIB})
target_link_libraries(${GT_CONTROL_TARGET} PRIVATE
    ${GT_CORE_TARGET}
    ${GT_IO_TARGET}
    ${GT_SCENARIO_TARGET}
    PlayerBase
    ScenarioEngine
    CommonMini
    Controllers
    RoadManager
    ${TIME_LIB}
    ${SOCK_LIB})
target_link_libraries(${GT_OSI_TARGET} PRIVATE ${GT_CORE_TARGET} ${GT_SCENARIO_TARGET} ScenarioEngine CommonMini RoadManager ${TIME_LIB} ${SOCK_LIB})
if(USE_OSG)
    target_link_libraries(${GT_CONTROL_TARGET} PRIVATE ViewerBase RoadGeom ${OSG_LIBRARIES})
    target_link_libraries(${GT_OSI_TARGET} PRIVATE ViewerBase RoadGeom ${OSG_LIBRARIES})
endif()

if(GT_ENABLE_SDL2)
    foreach(TARGET IN ITEMS ${GT_CONTROL_TARGET} ${GT_CORE_TARGET} ${TARGET_SHARED} ${TARGET_STATIC})
        target_compile_definitions(${TARGET} PRIVATE GT_ENABLE_SDL2)
        target_link_libraries(${TARGET} PRIVATE SDL2::SDL2)
    endforeach()
endif()

target_sources(${TARGET_SHARED} PRIVATE
    ${GT_CORE_SOURCES}
    ${GT_IO_SOURCES}
    ${GT_SCENARIO_SOURCES}
    ${GT_CONTROL_SOURCES}
    ${GT_OSI_SOURCES}
    ${GT_CORE_HEADERS}
    ${GT_IO_HEADERS}
    ${GT_SCENARIO_HEADERS}
    ${GT_CONTROL_HEADERS}
    ${GT_OSI_HEADERS})
target_sources(${TARGET_STATIC} PRIVATE
    ${GT_CORE_SOURCES}
    ${GT_IO_SOURCES}
    ${GT_SCENARIO_SOURCES}
    ${GT_CONTROL_SOURCES}
    ${GT_OSI_SOURCES}
    ${GT_CORE_HEADERS}
    ${GT_IO_HEADERS}
    ${GT_SCENARIO_HEADERS}
    ${GT_CONTROL_HEADERS}
    ${GT_OSI_HEADERS})

target_link_libraries(${TARGET_SHARED} PRIVATE
    PlayerBase
    ScenarioEngine
    CommonMini
    Controllers
    RoadManager
    ${TIME_LIB}
    ${SOCK_LIB})
target_link_libraries(${TARGET_STATIC} PRIVATE
    PlayerBase
    ScenarioEngine
    CommonMini
    Controllers
    RoadManager
    ${TIME_LIB}
    ${SOCK_LIB})

if(USE_OSG)
    target_link_libraries(${TARGET_SHARED} PRIVATE ViewerBase RoadGeom ${OSG_LIBRARIES})
    target_link_libraries(${TARGET_STATIC} PRIVATE ViewerBase RoadGeom ${OSG_LIBRARIES})
endif()

if(USE_SUMO)
    target_link_libraries(${TARGET_SHARED} PRIVATE ${SUMO_LIBRARIES})
    target_link_libraries(${TARGET_STATIC} PRIVATE ${SUMO_LIBRARIES})
endif()

if(USE_OSI)
    target_link_libraries(${TARGET_SHARED} PRIVATE ${OSI_LIBRARIES})
    target_link_libraries(${TARGET_STATIC} PRIVATE ${OSI_LIBRARIES})
endif()

foreach(TARGET IN ITEMS ${GT_CONTROL_TARGET} ${TARGET_SHARED} ${TARGET_STATIC})
    target_link_libraries(${TARGET} PRIVATE Python3::Python)
    target_compile_definitions(${TARGET} PRIVATE GT_ENABLE_EMBEDDED_PYTHON)
    if(EXISTS "${GT_EMBEDDED_PYTHON_HOME}")
        target_compile_definitions(${TARGET} PRIVATE GT_EMBEDDED_PYTHON_HOME="${GT_EMBEDDED_PYTHON_HOME}")
    endif()
endforeach()
# Also add compile definition to gt_core (for GT_esminiLib.cpp registration)
target_compile_definitions(${GT_CORE_TARGET} PRIVATE GT_ENABLE_EMBEDDED_PYTHON)
# Add esminiLib include path for SE_GetOSIGroundTruth / SE_GetODRFilename
target_include_directories(${GT_CONTROL_TARGET} PRIVATE
    ${CMAKE_SOURCE_DIR}/EnvironmentSimulator/Libraries/esminiLib)
target_include_directories(${TARGET_SHARED} PRIVATE
    ${CMAKE_SOURCE_DIR}/EnvironmentSimulator/Libraries/esminiLib)
target_include_directories(${TARGET_STATIC} PRIVATE
    ${CMAKE_SOURCE_DIR}/EnvironmentSimulator/Libraries/esminiLib)

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

option(GT_ESMINI_ENABLE_TESTS "Build GT_esmini unit and integration tests" ${BUILD_TESTING})
if(GT_ESMINI_ENABLE_TESTS)
    if(USE_GTEST AND BUILD_TESTING)
        add_subdirectory(test)
    else()
        message(STATUS "GT_esmini tests requested but USE_GTEST=OFF or BUILD_TESTING=OFF; skipping GT_esmini/test")
    endif()
endif()

# GT_Sim Executable
set(GT_SIM_TARGET GT_Sim)
set(GT_SIM_SOURCES GT_Sim/main.cpp)

add_executable(${GT_SIM_TARGET} ${GT_SIM_SOURCES})

target_link_libraries(${GT_SIM_TARGET} PRIVATE GT_esminiLib)

# Include directories needed for GT_Sim (esminiLib headers)
target_include_directories(${GT_SIM_TARGET} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
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

# ############################### Config Copy (dev build) ##########################################################################################
# Copy GT_esmini/config/ → build/GT_esmini/config/ (sibling of Release/)
# ConfigLoader resolves to <exe_dir>/../config/, which maps to build/GT_esmini/config/ in dev builds.

add_custom_command(
    TARGET ${TARGET_SHARED}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/config"
        "${CMAKE_CURRENT_BINARY_DIR}/config"
    COMMENT "Copying GT_esmini config files to build output"
)

# ############################### DriverScript DLL Copy ############################################################################################
# Copy esminiRMLib and GT_esminiLib to DriverScript/bin/ for Python script distribution

add_custom_command(
    TARGET ${TARGET_SHARED}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/DriverScript/bin"
    COMMAND ${CMAKE_COMMAND} -E copy
        "$<TARGET_FILE:esminiRMLib>"
        "${CMAKE_SOURCE_DIR}/DriverScript/bin/"
    COMMAND ${CMAKE_COMMAND} -E copy
        "$<TARGET_FILE:${TARGET_SHARED}>"
        "${CMAKE_SOURCE_DIR}/DriverScript/bin/"
    COMMENT "Copying esminiRMLib and GT_esminiLib to DriverScript/bin/"
)
