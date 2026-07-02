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

# Register Integration Tests
file(GLOB SCENARIO_FILES "${CMAKE_CURRENT_SOURCE_DIR}/scenarios/*.xosc")
set(SCENARIO_INDEX 0)
foreach(SCENARIO ${SCENARIO_FILES})
    get_filename_component(TEST_NAME ${SCENARIO} NAME_WE)
    add_test(
        NAME GT_esmini_Integration_${TEST_NAME}_${SCENARIO_INDEX}
        COMMAND ${LOADER_TARGET}
                ${SCENARIO}
                --path ${CMAKE_SOURCE_DIR}/resources/xodr
                --path ${CMAKE_SOURCE_DIR}/resources/xosc
                --path ${CMAKE_SOURCE_DIR}/resources
    )
    math(EXPR SCENARIO_INDEX "${SCENARIO_INDEX} + 1")
endforeach()

message(STATUS "GT_esmini: Added ${UNIT_TEST_TARGET} and ${LOADER_TARGET}")

