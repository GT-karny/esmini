# GT_esmini Test Directory
include(${CMAKE_SOURCE_DIR}/support/cmake/common/unittest.cmake)

# Unit Tests
set(UNIT_TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_ScenarioReaderParsing.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_ControllerRealDriverUtils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_LonProfilePlanner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/realdriver/test_LonProfilePlannerModule.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/realdriver/test_ControllerRealDriverUtilsModule.cpp
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

# Register Integration Tests
file(GLOB SCENARIO_FILES "${CMAKE_CURRENT_SOURCE_DIR}/scenarios/*.xosc")
list(FILTER SCENARIO_FILES EXCLUDE REGEX "\\.temp\\.xosc$")
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

