# GT_esmini Test Directory
include(${CMAKE_SOURCE_DIR}/support/cmake/common/unittest.cmake)

set(GT_EMBEDDED_PYTHON_HOME "${CMAKE_SOURCE_DIR}/thirdparty/python-embed/python-3.12.10-embed-amd64")

# Unit Tests
set(UNIT_TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_ScenarioReaderParsing.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_SimpleJson.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_ConfigLoader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_CommonControlHelpers.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_HeadlightLogic.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_ControllerRealDriverUtils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_LonProfilePlanner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/realdriver/test_LonProfilePlannerModule.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/realdriver/test_ControllerRealDriverUtilsModule.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/realdriver/test_AdasSlotTable.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/virtualdriver/test_PIDPurePursuitDriver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/virtualdriver/test_AdSteeringEnvelope.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/virtualdriver/test_ResumeMergeProfile.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/virtualdriver/test_AutoIndicatorPolicy.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/virtualdriver/test_TrafficPolicies.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/virtualdriver/test_StopLineSignalCatalog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/virtualdriver/test_RouteSignalScan.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/virtualdriver/test_VirtualDriverTelemetryJson.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/virtualdriver/test_PolicyDetail.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/virtualdriver/test_AdasFunctionReport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/virtualdriver/test_ControllerVirtualDriverActivation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/road/test_OdrSideModel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/road/test_OdrForkPatches.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/road/test_OdrLaneTypeSync.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/road/test_OdrLaneExtras.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/road/test_OdrSignalSemantics.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/road/test_OdrJunctionExtras.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/road/test_OdrVirtualJunction.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/road/test_OdrObjectExtras.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/road/test_OdrJunctionGeom.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/road/test_OdrLaneLayers.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/road/test_OdrRailroad.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/road/test_OdrResyncGuards.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/road/test_OdrLhtRoadTransitions.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/manualdrive/test_OverrideManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/manualdrive/test_FfbTargetServo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/manualdrive/test_EngineModel.cpp
)

set(UNIT_TEST_TARGET test_ScenarioReaderParsing)

unittest(${UNIT_TEST_TARGET} "${UNIT_TEST_SOURCES}"
    ${TARGET_STATIC}
    ScenarioEngine
    CommonMini
    RoadManager
    ${FMT_LIBRARIES}
)

target_include_directories(${UNIT_TEST_TARGET} PRIVATE
    ${CMAKE_SOURCE_DIR}/GT_esmini/include
    # feature:F7 scenario-driven handover — test_ControllerVirtualDriverActivation.cpp
    # includes upstream Controller.hpp directly (base-class Activate/Active
    # semantics), which is only PRIVATE on ScenarioEngine/GT_esminiLib_static
    # and so does not propagate via target_link_libraries. Same set GT_Sim's
    # own target_include_directories uses (GT_esmini/CMakeLists.txt) minus the
    # viewer/player paths this test target has no need of.
    ${SCENARIO_ENGINE_PATH}/SourceFiles
    ${SCENARIO_ENGINE_PATH}/OSCTypeDefs
    ${COMMON_MINI_PATH}
    ${CONTROLLERS_PATH}
)

# [GT_ODR] test_OdrSideModel.cpp (inside the umbrella gate binary) reads a real repo control file
# via GT_ODR_REPO_ROOT (plan P1 miniature acceptance criterion).
target_compile_definitions(${UNIT_TEST_TARGET} PRIVATE GT_ODR_REPO_ROOT="${CMAKE_SOURCE_DIR}")

# [GT_ODR] Separate, slower ctest target: TrafficLight-classification + load-status probe over the
# whole xodr asset universe (plan P1 / P3 audit-universe seed). Golden-diff assertion mode, with a
# GT_ODR_PROBE_UPDATE=1 baseline-capture mode. Uses the same unittest() macro pattern; the macro's
# executable target name IS the first argument, so definitions attach directly to it.
set(ODR_PROBE_TARGET test_OdrAssetProbe)
unittest(${ODR_PROBE_TARGET} "${CMAKE_CURRENT_SOURCE_DIR}/unit/road/test_OdrAssetProbe.cpp"
    ${TARGET_STATIC}
    ScenarioEngine
    CommonMini
    RoadManager
    ${FMT_LIBRARIES}
)
target_include_directories(${ODR_PROBE_TARGET} PRIVATE
    ${CMAKE_SOURCE_DIR}/GT_esmini/include
)
target_compile_definitions(${ODR_PROBE_TARGET} PRIVATE GT_ODR_REPO_ROOT="${CMAKE_SOURCE_DIR}")

# PythonDriver Unit Tests (requires Python3::Python target from parent CMakeLists.txt)
if(TARGET Python3::Python)
    set(PYTHONDRIVER_TEST_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/pythondriver/test_PythonDriverBridgeLink.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/pythondriver/test_PythonDriverBridge.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/pythondriver/test_DataFlow.cpp
    )
    set(PYTHONDRIVER_TEST_TARGET test_PythonDriverBridge)

    unittest(${PYTHONDRIVER_TEST_TARGET} "${PYTHONDRIVER_TEST_SOURCES}"
        ${TARGET_STATIC}
        ScenarioEngine
        CommonMini
        RoadManager
        Python3::Python
        ${FMT_LIBRARIES}
    )

    target_include_directories(${PYTHONDRIVER_TEST_TARGET} PRIVATE
        ${CMAKE_SOURCE_DIR}/GT_esmini/include
    )

    target_compile_definitions(${PYTHONDRIVER_TEST_TARGET} PRIVATE GT_ENABLE_EMBEDDED_PYTHON)

    # Add GT_EMBEDDED_PYTHON_HOME if available (for embedded Python path in tests)
    if(EXISTS "${GT_EMBEDDED_PYTHON_HOME}")
        target_compile_definitions(${PYTHONDRIVER_TEST_TARGET} PRIVATE
            GT_EMBEDDED_PYTHON_HOME="${GT_EMBEDDED_PYTHON_HOME}")
    endif()

    message(STATUS "GT_esmini: Added ${PYTHONDRIVER_TEST_TARGET} (embedded Python tests)")
endif()

# Integration Test Loader
set(LOADER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/integration/GT_Loader.cpp
)
set(LOADER_TARGET GT_Loader)

add_executable(${LOADER_TARGET} ${LOADER_SOURCES})
target_link_libraries(${LOADER_TARGET} PRIVATE 
    ${TARGET_SHARED} # Link against GT_esminiLib DLL/LIB
)
target_include_directories(${LOADER_TARGET} PRIVATE
    ${CMAKE_SOURCE_DIR}/GT_esmini/include
    ${CMAKE_SOURCE_DIR}/EnvironmentSimulator/Libraries/esminiLib
)
add_custom_command(
    TARGET ${LOADER_TARGET}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:${TARGET_SHARED}>
        $<TARGET_FILE_DIR:${LOADER_TARGET}>
    COMMENT "Copying GT_esminiLib runtime DLL beside GT_Loader"
)

# GT_esminiLib.dll imports python312.dll (embedded Python) and, with GT_ENABLE_SDL2,
# SDL2.dll. Without them beside the test binaries every test process dies at load
# time (0xc0000135) and Windows pops a system-error dialog per test. Stage them.
if(WIN32 AND GT_ENABLE_EMBEDDED_PYTHON AND EXISTS "${GT_EMBEDDED_PYTHON_HOME}/python312.dll")
    add_custom_command(
        TARGET ${LOADER_TARGET}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GT_EMBEDDED_PYTHON_HOME}/python312.dll"
            "${GT_EMBEDDED_PYTHON_HOME}/python3.dll"
            $<TARGET_FILE_DIR:${LOADER_TARGET}>
        COMMENT "Staging embedded Python runtime DLLs beside GT test binaries"
    )
endif()
if(WIN32 AND GT_ENABLE_SDL2 AND EXISTS "${CMAKE_SOURCE_DIR}/thirdparty/SDL2/lib/x64/SDL2.dll")
    add_custom_command(
        TARGET ${LOADER_TARGET}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/thirdparty/SDL2/lib/x64/SDL2.dll"
            $<TARGET_FILE_DIR:${LOADER_TARGET}>
        COMMENT "Staging SDL2.dll beside GT test binaries"
    )
endif()

# --- FFB Override Replay Harness (feature:F7, permanent diagnostic tool) ---
#
# Standalone CLI, NOT a ctest/gtest target -- like GT_Loader above, a plain
# add_executable() beside (not inside) the unittest() umbrella binary, because
# it is a manually-invoked measurement tool (multiple --jsonl runs x multiple
# detector configs, printed as a table) rather than a pass/fail assertion
# suite. See the tool's own file header for the full rationale.
#
# Unlike GT_Loader (which drives the public C API and links ${TARGET_SHARED}),
# this tool reaches into gt_esmini::OverrideManager directly -- an internal
# class never exported from GT_esminiLib.dll -- so it links ${TARGET_STATIC}
# instead. ${TARGET_STATIC}'s own link libraries (ScenarioEngine, CommonMini,
# RoadManager, OSI, ...), even the PRIVATE ones, propagate automatically to
# any executable that links a STATIC library target in CMake (this is exactly
# what lets GT_OSMP_FMU/CMakeLists.txt link the same static lib alone -- see
# this repo's CLAUDE.md Protocol B note), so no other GT_esmini/upstream
# library needs to be named again here. EXTERNALS_OSI_INCLUDES IS added
# explicitly below regardless: IFFBSink.hpp (pulled in via OverrideManager.hpp)
# includes an OSI proto header, and unlike the unittest() macro above, a plain
# add_executable() does not supply that include path on its own.
set(FFB_REPLAY_TARGET ffb_override_replay)
add_executable(${FFB_REPLAY_TARGET} ${CMAKE_CURRENT_SOURCE_DIR}/tools/ffb_override_replay.cpp)
target_link_libraries(${FFB_REPLAY_TARGET} PRIVATE ${TARGET_STATIC})
target_include_directories(${FFB_REPLAY_TARGET} PRIVATE
    ${CMAKE_SOURCE_DIR}/GT_esmini/include
)
target_include_directories(${FFB_REPLAY_TARGET} SYSTEM PRIVATE
    ${EXTERNALS_OSI_INCLUDES}
)
message(STATUS "GT_esmini: Added ${FFB_REPLAY_TARGET} (feature:F7 FFB override replay tool)")

# Register Integration Tests (audit R3/TST re-authoring)
#
# Registration is explicit (no GLOB): every test states the expectations GT_Loader
# must verify (audit TST-3 — "did not crash" is no longer a pass). Light type
# indices follow GT_GetLightState: 1=lowBeam, 3=fog, 6=brake, 8=indicatorLeft,
# 9=indicatorRight, 10=reversing.
set(GT_INTEGRATION_PATH_ARGS
    --path ${CMAKE_SOURCE_DIR}/resources/xodr
    --path ${CMAKE_SOURCE_DIR}/resources/xosc
    --path ${CMAKE_SOURCE_DIR}/resources
    # F6: the test scenarios dir carries straight_tunnel.xodr (tunnel test asset).
    --path ${CMAKE_CURRENT_SOURCE_DIR}/scenarios)

function(gt_integration_test TEST_NAME SCENARIO)
    add_test(
        NAME GT_esmini_Integration_${TEST_NAME}
        COMMAND ${LOADER_TARGET}
                ${CMAKE_CURRENT_SOURCE_DIR}/scenarios/${SCENARIO}
                ${GT_INTEGRATION_PATH_ARGS}
                ${ARGN}
    )
endfunction()

# --- AutoLight / LightStateAction set (active feature) ---
# autolight_test: scenario sets fog ON at init; AutoLight must raise brake lights
# during the deceleration event (t>2) and reversing lights during reverse (t>6).
gt_integration_test(autolight_test autolight_test.xosc
    --duration 15 --expect-object Ego --expect-min-sim-time 9.5
    --expect-light-on 3 --expect-light-on 6 --expect-light-on 10
    --expect-move 20)

# Scenario-driven LightStateAction tests run with AutoLight ENABLED: they verify that a
# native LightStateAction retains ownership (ScenarioLightRegistry latch) and AutoLight no
# longer overwrites scenario-driven indicators/reversing every frame (audit R3/TST fix in
# AutoLightController::ApplyAutoLight — SCENARIO > MANUAL > AUTO + edge trigger).
# Brake ON at t>1 while driving at 10 m/s.
gt_integration_test(test_brake_lights test_brake_lights.xosc
    --duration 6 --expect-object Ego --expect-min-sim-time 5.5
    --expect-light-on 6 --expect-move 20)

# Flashing indicators: left at t>1, right at t>4.
gt_integration_test(test_indicators test_indicators.xosc
    --duration 6 --expect-object Ego --expect-min-sim-time 5.5
    --expect-light-on 8 --expect-light-on 9)

# Two lights in one parallel event: lowBeam + fog at t>2.
gt_integration_test(test_multiple_lights test_multiple_lights.xosc
    --duration 6 --expect-object Ego --expect-min-sim-time 5.5
    --expect-light-on 1 --expect-light-on 3)

# Graceful degradation: invalid vehicleLightType / missing mandatory 'mode' must be
# skipped with a warning (upstream parseOSCPrivateAction returns null for the action),
# the scenario still loads and runs, and no light is ever switched on.
# AutoLight disabled so only scenario actions could touch the lights.
gt_integration_test(test_error_invalid_value test_error_invalid_value.xosc
    --duration 4 --no-autolight --expect-object Ego --expect-min-sim-time 3.5
    --expect-light-never-on 6)
gt_integration_test(test_error_missing_attribute test_error_missing_attribute.xosc
    --duration 4 --no-autolight --expect-object Ego --expect-min-sim-time 3.5
    --expect-light-never-on 6)

# --- F6 AutoLight environment-driven headlights (active feature) ---
# Enabled per-run via --autolight-headlights (config/auto_light.json defaults OFF).
# Light indices: 1=lowBeam, 2=highBeam.
#
# (a) Night Environment (Sun illuminance below threshold) => low beam ON.
gt_integration_test(f6_night_lowbeam f6_night_lowbeam.xosc
    --autolight-headlights --duration 6 --expect-object Ego --expect-min-sim-time 5.5
    --expect-light-on 1 --expect-move 20)

# (a') Regression pin: --autolight-headlights must imply the AutoLight master switch.
# GT_Loader enables the master by default (GT_EnableAutoLight), which masked the bug where
# GT_Sim / in-process runs with only --autolight-headlights never ran the F6 rule at all
# (AutoLightManager::Update early-returns when the master is off). --no-autolight suppresses
# the loader's default so the flag has to stand on its own.
gt_integration_test(f6_headlights_flag_implies_master f6_night_lowbeam.xosc
    --no-autolight --autolight-headlights --duration 4 --expect-object Ego --expect-min-sim-time 3.5
    --expect-light-on 1)

# (b) Daytime Environment (bright illuminance) => low and high beam stay OFF.
gt_integration_test(f6_day_no_lowbeam f6_day_no_lowbeam.xosc
    --autolight-headlights --duration 6 --expect-object Ego --expect-min-sim-time 5.5
    --expect-light-never-on 1 --expect-light-never-on 2 --expect-move 20)

# (c) OpenDRIVE <tunnel> over s=[200,300]: low beam ON inside, OFF again after exit.
gt_integration_test(f6_tunnel_lowbeam f6_tunnel_lowbeam.xosc
    --autolight-headlights --duration 25 --expect-object Ego --expect-min-sim-time 24
    --expect-light-on 1 --expect-light-off-at-end 1 --expect-light-off-at-end 2
    --expect-move 100)

# (d) High beam: night + clear road ahead => low beam AND high beam ON (reuses night road).
gt_integration_test(f6_highbeam_clear f6_night_lowbeam.xosc
    --autolight-headlights --duration 8 --expect-object Ego --expect-min-sim-time 7.5
    --expect-light-on 1 --expect-light-on 2)

# (d') High beam dims: night + lead vehicle ahead within range => low beam ON, high beam never ON.
gt_integration_test(f6_highbeam_lead_dim f6_highbeam_lead_dim.xosc
    --autolight-headlights --duration 8 --expect-object Ego --expect-min-sim-time 7.5
    --expect-light-on 1 --expect-light-never-on 2)

# --- OSI ground truth over a junction (active feature) ---
# The static ground truth (lanes, lane pairings, junction intersections) is built on the first OSI
# request, so an OSI-path defect surfaces at t=0 as an unplayable scenario. Neither is caught unless
# a test actually asks for the ground truth -- hence --osi.
#
# LHT: the junction lane-pairing builder used to guard the LHT arm on the connecting-road lane id
# (to_) while dereferencing the incoming-road lookup (from_), so on a left-hand-traffic junction the
# dereferenced lane went unchecked. The road pins the trigger: road 1's lane -1 is a sidewalk, so
# that lookup returns null and the pre-fix code crashed here.
gt_integration_test(osi_junction_lht lht_junction_osi_intersection.xosc
    --osi --duration 6 --expect-object Ego --expect-min-sim-time 4.0 --expect-move 20)

# RHT counterpart over the same junction topology: pins that the rewritten guard did not change the
# right-hand-traffic path (the arm that was always correct).
gt_integration_test(osi_junction_rht rht_junction_osi_intersection.xosc
    --osi --duration 6 --expect-object Ego --expect-min-sim-time 4.0 --expect-move 20)

# Edge case: a connecting-road driving lane whose <roadMark> carries a <type> child but no <line>
# child (standard conformant OpenDRIVE). The lane then routes through the free-boundary branch and
# the roadMark 4-step chain laneroadmark->GetLaneRoadMarkTypeByIdx(0)->GetLaneRoadMarkTypeLineByIdx(0)
# resolves to null (the stand-in-line synthesis is skipped because a <type> already exists). The
# pre-fix builder dereferenced that null at t=0; this pins the guarded free-boundary path.
gt_integration_test(osi_junction_roadmark_edge osi_junction_roadmark_edge.xosc
    --osi --duration 6 --expect-object Ego --expect-min-sim-time 4.0 --expect-move 20)

# --- Built-in sample scenarios (resources/xosc, shipped in the Web UI sample project) ---
# Rot guard for the F6 demo samples: sample assets that never run break silently
# (the original 38-broken-integration-tests lesson).
add_test(
    NAME GT_esmini_Integration_sample_night_drive
    COMMAND ${LOADER_TARGET}
            ${CMAKE_SOURCE_DIR}/resources/xosc/night_drive.xosc
            ${GT_INTEGRATION_PATH_ARGS}
            --autolight-headlights --duration 12 --expect-object Ego --expect-min-sim-time 11.5
            --expect-light-on 1 --expect-light-on 2 --expect-light-off-at-end 2 --expect-move 20)
add_test(
    NAME GT_esmini_Integration_sample_day_tunnel_drive
    COMMAND ${LOADER_TARGET}
            ${CMAKE_SOURCE_DIR}/resources/xosc/day_tunnel_drive.xosc
            ${GT_INTEGRATION_PATH_ARGS}
            --autolight-headlights --duration 26 --expect-object Ego --expect-min-sim-time 25.5
            --expect-light-on 1 --expect-light-off-at-end 1 --expect-light-off-at-end 2 --expect-move 100)

# --- ControllerRealDriver set (active feature, UDP-driven; runs with default/no input) ---
# Assert scenario load, Ego presence and full completion of the 10 s StopTrigger window.
set(GT_REALDRIVER_SCENARIOS
    realdriver_f01_udp_input_decode
    realdriver_f02_lightmask_mapping
    realdriver_f03_brake_reverse_auto_light
    realdriver_f04_lon_profile_output
    realdriver_f05_waypoint_output
    realdriver_f06_assign_route
    realdriver_f07_lane_change
    realdriver_f08_lane_offset
    realdriver_f09_follow_trajectory
    realdriver_f10_longitudinal_distance_action
    realdriver_f11_speed_profile_action
    realdriver_f12_synchronize_action
    realdriver_f13_hvd_adas_passthrough
    realdriver_f14_driverscript_acc
    realdriver_f15_driverscript_lateral_lkas)
foreach(SCENARIO ${GT_REALDRIVER_SCENARIOS})
    gt_integration_test(${SCENARIO} ${SCENARIO}.xosc
        --duration 15 --expect-object Ego --expect-min-sim-time 9.5)
endforeach()

# --- PythonDriverController set (FROZEN, v0.8) ---
# PythonDriverController is only compiled with GT_ENABLE_EMBEDDED_PYTHON=ON, so these
# tests are only registered for embedded-Python builds (audit MSC-5: frozen-feature
# tests are no longer default-registered). They are NOT part of any green gate.
if(TARGET Python3::Python)
    set(GT_PYTHONDRIVER_SCENARIOS
        pythondriver_f01_udp_input_decode
        pythondriver_f02_lightmask_mapping
        pythondriver_f03_brake_reverse_auto_light
        pythondriver_f03a_autolight_brake_reverse
        pythondriver_f03b_autolight_indicator_route
        pythondriver_f04_lon_profile_output
        pythondriver_f05_waypoint_output
        pythondriver_f06_assign_route
        pythondriver_f07_lane_change
        pythondriver_f08_lane_offset
        pythondriver_f09_follow_trajectory
        pythondriver_f10_longitudinal_distance_action
        pythondriver_f11_speed_profile_action
        pythondriver_f12_synchronize_action
        pythondriver_f13_hvd_adas_passthrough
        pythondriver_f14_driverscript_acc
        pythondriver_f15_driverscript_lateral_lkas)
    foreach(SCENARIO ${GT_PYTHONDRIVER_SCENARIOS})
        gt_integration_test(${SCENARIO} ${SCENARIO}.xosc
            --duration 15 --expect-object Ego --expect-min-sim-time 9.5)
    endforeach()
    list(LENGTH GT_PYTHONDRIVER_SCENARIOS GT_PYTHONDRIVER_COUNT)
    message(STATUS "GT_esmini: Registered ${GT_PYTHONDRIVER_COUNT} frozen PythonDriver integration tests (embedded Python build)")
endif()

message(STATUS "GT_esmini: Added ${UNIT_TEST_TARGET} and ${LOADER_TARGET}")

