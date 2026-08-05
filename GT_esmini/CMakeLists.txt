# ############################### Setting targets ###################################################################################################

set(TARGET_SHARED GT_esminiLib)
set(TARGET_STATIC GT_esminiLib_static)

# ############################### Loading desired rules ##############################################################################################

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

# Embedded-Python PythonDriverController stack (v0.8-frozen feature).
# OFF by default: dev builds need no Python3 dev headers and produce no python312.dll
# dependency. Distribution packages keep the frozen stack — scripts/build_package.ps1
# configures with -DGT_ENABLE_EMBEDDED_PYTHON=ON explicitly.
option(GT_ENABLE_EMBEDDED_PYTHON "Build the frozen embedded-Python PythonDriverController stack (requires Python3 dev headers)" OFF)

if(GT_ENABLE_EMBEDDED_PYTHON)
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
else()
    message(STATUS "GT_esmini: Embedded Python (PythonDriverController) DISABLED — frozen stack excluded")
endif()

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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/ConfigLoader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/IdleJitter.cpp)

set(GT_CORE_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/core/GT_esminiLib.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/core/GT_esminiRMLib.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/core/ConfigLoader.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/core/IConfigLoader.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/core/IdleJitter.hpp)

set(GT_IO_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/GT_UDP.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/GT_ScenarioVariablesReporter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/GT_VirtualDriverReporter.cpp)

set(GT_IO_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/io/GT_UDP.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/io/GT_ScenarioVariablesReporter.hpp)

set(GT_SCENARIO_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/scenario/ExtraAction.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/scenario/ExtraCondition.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/scenario/ExtraEntities.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/scenario/VehicleLightBridge.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/scenario/GT_ScenarioReader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/scenario/TrafficSignalController.cpp)

set(GT_SCENARIO_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/scenario/ExtraAction.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/scenario/ExtraCondition.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/scenario/ExtraEntities.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/scenario/VehicleLightBridge.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/scenario/GT_ScenarioReader.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/scenario/TrafficSignalController.hpp)

set(GT_CONTROL_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/AutoLightController.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/HeadlightLogic.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/RealVehicle.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/ControllerRealDriver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/realdriver/DriverOutputPort.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/realdriver/LatPathPlanner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/realdriver/LonProfilePlanner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/realdriver/RealDriverCoordinator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/ObservedVehiclePhysics.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/VehiclePhysicsManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/HeadingCorrectionManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/ControllerKinematic.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/ControllerRouteDrive.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/ControllerManualDrive.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/ControllerVirtualDriver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/virtualdriver/VirtualDriverConfig.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/virtualdriver/TrajectoryShortPlanner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/virtualdriver/ManeuverAwareSpeedPlanner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/virtualdriver/PIDPurePursuitDriver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/virtualdriver/AdSteeringEnvelope.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/virtualdriver/ResumeMergeProfile.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/virtualdriver/AutoIndicatorPolicy.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/virtualdriver/VirtualDriverTelemetryJson.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/virtualdriver/AdasFunctionReport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/virtualdriver/RouteLanePlan.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/virtualdriver/LaneChangeInitiation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/virtualdriver/OvertakeManeuver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/virtualdriver/policies/RouteSignalScan.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/virtualdriver/policies/RouteCrosswalkScan.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/virtualdriver/policies/JunctionStopGuard.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/virtualdriver/policies/LeadVehicleAware.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/virtualdriver/policies/StopLineSignalCatalog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/virtualdriver/policies/TrafficLightAware.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/virtualdriver/policies/StopYieldSignAware.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/virtualdriver/policies/ConflictPointResolver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/virtualdriver/policies/CrosswalkPedestrianAware.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/virtualdriver/policies/AebSafety.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/virtualdriver/policies/SignalJunctionResolver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/common/DomainOwnershipLedger.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/common/ModuleDirectory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/common/ShiftLogic.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/common/RealVehicleBackend.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/manualdrive/ManualDriveConfig.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/manualdrive/OverrideManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/manualdrive/FfbTargetServo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/manualdrive/HeadlessFfbInput.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/manualdrive/EngineModel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/manualdrive/AutoTransmission.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/manualdrive/HVDStateApplier.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/manualdrive/KickdownDetector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/manualdrive/PedalArbitrator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/manualdrive/ManualDriveCoordinator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/manualdrive/UdpTransport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/manualdrive/TcpTransport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/manualdrive/NetworkInputBridge.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/manualdrive/NetworkPhysicsBridge.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/manualdrive/ScriptedInputSource.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/manualdrive/AdasCoexistenceStack.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/manualdrive/AccLonController.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/manualdrive/SpeedLimiter.cpp)

set(GT_CONTROL_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/AutoLightController.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/HeadlightLogic.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/RealVehicle.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/ControllerRealDriver.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/ControllerRealDriverUtils.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/realdriver/DriverOutputPort.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/realdriver/LatPathPlanner.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/realdriver/LonProfilePlanner.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/realdriver/RealDriverCoordinator.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/ObservedVehiclePhysics.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/VehiclePhysicsManager.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/HeadingCorrectionManager.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/ControllerKinematic.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/ControllerManualDrive.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/ControllerVirtualDriver.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/virtualdriver/VirtualDriverConfig.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/ManualDriveTypes.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/IInputSource.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/IFFBSink.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/ITransport.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/NullFFBSink.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/StubInputSource.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/ManualDriveConfig.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/OverrideManager.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/FfbTargetServo.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/HeadlessFfbInput.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/EngineModel.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/AutoTransmission.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/common/ShiftLogic.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/common/VehicleCommand.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/common/PhysicsInitParams.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/common/IPhysicsBackend.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/common/JunctionTurn.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/common/ModuleDirectory.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/common/RealVehicleBackend.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/common/TransitionDynamics.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/common/SimpleJson.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/virtualdriver/VirtualDriverTypes.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/virtualdriver/IShortPlanner.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/virtualdriver/IDriverModel.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/virtualdriver/IMidLongPlanner.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/virtualdriver/ITrafficPolicy.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/virtualdriver/PolicyDetail.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/virtualdriver/AdasFunctionReport.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/virtualdriver/IIndicatorPolicy.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/virtualdriver/TrajectoryShortPlanner.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/virtualdriver/ManeuverAwareSpeedPlanner.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/virtualdriver/PIDPurePursuitDriver.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/virtualdriver/AdSteeringEnvelope.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/virtualdriver/ResumeMergeProfile.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/virtualdriver/AutoIndicatorPolicy.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/virtualdriver/TrafficPolicyManager.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/virtualdriver/RouteLanePlan.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/virtualdriver/LaneChangeInitiation.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/virtualdriver/policies/RouteSignalScan.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/virtualdriver/policies/RouteCrosswalkScan.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/virtualdriver/policies/JunctionStopGuard.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/virtualdriver/policies/LeadVehicleAware.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/virtualdriver/policies/StopLineSignalCatalog.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/virtualdriver/policies/TrafficLightAware.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/virtualdriver/policies/StopYieldSignAware.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/virtualdriver/policies/ConflictPointResolver.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/virtualdriver/policies/CrosswalkPedestrianAware.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/virtualdriver/policies/AebSafety.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/virtualdriver/policies/SignalJunctionResolver.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/HVDStateApplier.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/KickdownDetector.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/AccLonController.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/SpeedLimiter.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/PedalArbitrator.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/ManualDriveCoordinator.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/UdpTransport.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/TcpTransport.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/NetworkInputBridge.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/NetworkPhysicsBridge.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/ScriptedInputSource.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/AdasCoexistenceStack.hpp)

# Embedded Python controller sources (conditional on GT_ENABLE_EMBEDDED_PYTHON)
if(GT_ENABLE_EMBEDDED_PYTHON)
    list(APPEND GT_CONTROL_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/control/ControllerPythonDriver.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/control/pythondriver/PythonDriverBridge.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/control/pythondriver/PythonDriverCoordinator.cpp)
    list(APPEND GT_CONTROL_HEADERS
        ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/ControllerPythonDriver.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/pythondriver/PythonDriverBridge.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/pythondriver/PythonDriverCoordinator.hpp)
endif()

# SDL2 racing wheel sources (conditional)
if(GT_ENABLE_SDL2)
    list(APPEND GT_CONTROL_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/control/manualdrive/SDL2WheelInput.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/control/manualdrive/SDL2KeyboardInput.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/control/manualdrive/SDLFFBSink.cpp)
    list(APPEND GT_CONTROL_HEADERS
        ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/SDL2WheelInput.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/include/gt_esmini/control/manualdrive/SDL2KeyboardInput.hpp
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
# NOTE: the former per-module static libs (gt_core/gt_io/gt_scenario/gt_control/gt_osi)
# were removed (audit BLD-4): nothing consumed them and every GT source was compiled
# three times (gt_* + GT_esminiLib + GT_esminiLib_static). The module split lives on in
# the GT_*_SOURCES variables.

# GT-only: GT extension targets are Windows-only AND require the full external stack. GT is coupled to
# the MSVC-vendored OSI 3.7.0 package (externals/osi/v11); on Linux/macOS the toolchain resolves the
# older downloaded OSI 3.5.0 (GT_HostVehicleReporter uses OSI 3.7.0 vehicle_motion) and the frozen
# embedded-Python stack pulls <windows.h>. Cross-platform GT support is tracked in issue #36.
#
# The external-module guard matters for the slim test-no-external-modules CI job (USE_OSG/OSI/SUMO=OFF):
# on Windows that job would otherwise try to build GT (GT_OSIReporter needs OSI, GT_RoadGen needs OSG,
# GT_esminiLib_static links transitive core/SUMO) and fail. Gating here (and the matching GT_OSMP_FMU
# guard in the root CMakeLists) keeps that slim job upstream-only on every OS. The configure logic above
# (options, the "Embedded Python ... DISABLED" line the embedded-python CI job asserts) still runs
# everywhere; only the target creation below is gated.
if(NOT WIN32
   OR NOT USE_OSI
   OR NOT USE_OSG
   OR NOT USE_SUMO)
    message(STATUS "GT_esmini: GT extension targets skipped (need Windows + USE_OSI/OSG/SUMO; issue #36); upstream targets still build")
    return()
endif()

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

foreach(TARGET IN ITEMS ${TARGET_SHARED} ${TARGET_STATIC})

    target_link_libraries(${TARGET} PRIVATE project_options)

    # dladdr (module-directory resolution on POSIX). Empty/no-op on Windows.
    target_link_libraries(${TARGET} PRIVATE ${CMAKE_DL_LIBS})

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

    disable_iwyu(${TARGET})

endforeach()

target_include_directories(${TARGET_SHARED} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(${TARGET_STATIC} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(GT_ENABLE_SDL2)
    foreach(TARGET IN ITEMS ${TARGET_SHARED} ${TARGET_STATIC})
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

if(USE_IMPLOT)
    target_link_libraries(${TARGET_SHARED} PRIVATE ${IMPLOT_LIBRARIES})
    target_link_libraries(${TARGET_STATIC} PRIVATE ${IMPLOT_LIBRARIES})
endif()

if(USE_SUMO)
    target_link_libraries(${TARGET_SHARED} PRIVATE ${SUMO_LIBRARIES})
    target_link_libraries(${TARGET_STATIC} PRIVATE ${SUMO_LIBRARIES})
endif()

if(USE_OSI)
    target_link_libraries(${TARGET_SHARED} PRIVATE ${OSI_LIBRARIES})
    target_link_libraries(${TARGET_STATIC} PRIVATE ${OSI_LIBRARIES})
endif()

if(GT_ENABLE_EMBEDDED_PYTHON)
    foreach(TARGET IN ITEMS ${TARGET_SHARED} ${TARGET_STATIC})
        target_link_libraries(${TARGET} PRIVATE Python3::Python)
        target_compile_definitions(${TARGET} PRIVATE GT_ENABLE_EMBEDDED_PYTHON)
        if(EXISTS "${GT_EMBEDDED_PYTHON_HOME}")
            target_compile_definitions(${TARGET} PRIVATE GT_EMBEDDED_PYTHON_HOME="${GT_EMBEDDED_PYTHON_HOME}")
        endif()
    endforeach()
endif()
# Add esminiLib include path for SE_GetOSIGroundTruth / SE_GetODRFilename
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

# ############################### GT_RoadGen Executable ############################################################################################
# Standalone, per-road MULTITHREADED OpenDRIVE -> .osgb road-surface generator.
# GT_Sim invokes it (cache-keyed by xodr hash) and injects the result via --model so the
# core viewer skips its slow single-threaded road-mesh generation.
# Isolated target: it does NOT link the core RoadGeom/ViewerBase libs, so the forked
# `roadgeom` namespace in GT_RoadGeom.* cannot clash with upstream.
if(USE_OSG)
    find_package(Threads REQUIRED)

    set(GT_ROADGEN_TARGET GT_RoadGen)
    add_executable(${GT_ROADGEN_TARGET}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/roadgen/GT_RoadGeom.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/roadgen/GT_trafficlightmodel.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/roadgen/GT_RoadGen_main.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/roadgen/GT_RoadGeom.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/roadgen/GT_trafficlightmodel.hpp)

    set_target_properties(${GT_ROADGEN_TARGET} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

    # OSG_LIBRARY_STATIC: roadgeom.cpp statically registers OSG plugins (USE_OSGPLUGIN/serializers).
    target_compile_definitions(${GT_ROADGEN_TARGET} PRIVATE OSG_LIBRARY_STATIC _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN NOGDI)

    target_include_directories(${GT_ROADGEN_TARGET} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/roadgen
        ${COMMON_MINI_PATH})
    target_include_directories(${GT_ROADGEN_TARGET} SYSTEM PRIVATE
        ${ROAD_MANAGER_PATH}
        ${EXTERNALS_OSG_INCLUDES}
        ${EXTERNALS_PUGIXML_PATH}
        ${EXTERNALS_FMT_INCLUDES})

    target_link_libraries(${GT_ROADGEN_TARGET} PRIVATE
        RoadManager
        CommonMini
        Threads::Threads
        ${TIME_LIB}
        ${SOCK_LIB})
    if(USE_OSG)
        target_link_libraries(${GT_ROADGEN_TARGET} PRIVATE ${OSG_LIBRARIES})
    endif()

    disable_iwyu(${GT_ROADGEN_TARGET})

    install(
        TARGETS ${GT_ROADGEN_TARGET}
        DESTINATION "${INSTALL_PATH}")
endif()

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
