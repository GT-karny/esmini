# GT_esmini Test Directory
include(${CMAKE_SOURCE_DIR}/support/cmake/common/unittest.cmake)

set(GT_EMBEDDED_PYTHON_HOME "${CMAKE_SOURCE_DIR}/thirdparty/python-embed/python-3.12.10-embed-amd64")

# Unit Tests
set(UNIT_TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_ScenarioReaderParsing.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_SimpleJson.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_CommonControlHelpers.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_ControllerRealDriverUtils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_LonProfilePlanner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/realdriver/test_LonProfilePlannerModule.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/realdriver/test_ControllerRealDriverUtilsModule.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/virtualdriver/test_PIDPurePursuitDriver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/virtualdriver/test_AutoIndicatorPolicy.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/virtualdriver/test_TrafficPolicies.cpp
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

# Register Integration Tests (audit R3/TST re-authoring)
#
# Registration is explicit (no GLOB): every test states the expectations GT_Loader
# must verify (audit TST-3 — "did not crash" is no longer a pass). Light type
# indices follow GT_GetLightState: 1=lowBeam, 3=fog, 6=brake, 8=indicatorLeft,
# 9=indicatorRight, 10=reversing.
set(GT_INTEGRATION_PATH_ARGS
    --path ${CMAKE_SOURCE_DIR}/resources/xodr
    --path ${CMAKE_SOURCE_DIR}/resources/xosc
    --path ${CMAKE_SOURCE_DIR}/resources)

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

# Scenario-driven LightStateAction tests run with --no-autolight: they verify the
# NATIVE action path, and AutoLightController currently overwrites scenario-driven
# indicators/reversing every frame (known conflict, see audit R3/TST notes).
# Brake ON at t>1 while driving at 10 m/s.
gt_integration_test(test_brake_lights test_brake_lights.xosc
    --duration 6 --no-autolight --expect-object Ego --expect-min-sim-time 5.5
    --expect-light-on 6 --expect-move 20)

# Flashing indicators: left at t>1, right at t>4.
gt_integration_test(test_indicators test_indicators.xosc
    --duration 6 --no-autolight --expect-object Ego --expect-min-sim-time 5.5
    --expect-light-on 8 --expect-light-on 9)

# Two lights in one parallel event: lowBeam + fog at t>2.
gt_integration_test(test_multiple_lights test_multiple_lights.xosc
    --duration 6 --no-autolight --expect-object Ego --expect-min-sim-time 5.5
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

