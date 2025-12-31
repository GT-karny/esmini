set(TARGET GT_Sim)

set(SOURCES main.cpp)

set(INCLUDES "")

# ############################### Creating executable ###############################################################################################

add_executable(${TARGET} ${SOURCES} ${INCLUDES})

# ############################### Linking ################################################################################

# Link to GT_esminiLib (which links to everything else)
target_link_libraries(${TARGET} PRIVATE GT_esminiLib)

# Add includes for esminiLib headers (exposed by GT_esminiLib interface or needed directly)
# Since GT_Sim uses SE_ functions, it needs esminiLib.hpp which is included by GT_esminiLib.hpp
# But GT_esminiLib target should populate INTERFACE_INCLUDE_DIRECTORIES if set up correctly?
# In parent CMakeLists, GT_esminiLib has PUBLIC include dirs for externals, but PRIVATE for internal esmini paths.
# We likely need to add esmini paths here or make them PUBLIC in parent.
# Let's try relying on what GT_esminiLib provides first, but we know we include GT_esminiLib.hpp which includes esminiLib.hpp.
# esminiLib.hpp will need its path visible.

target_include_directories(${TARGET} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${SCENARIO_ENGINE_PATH}/SourceFiles
    ${SCENARIO_ENGINE_PATH}/OSCTypeDefs
    ${VIEWER_BASE_PATH}
    ${PLAYER_BASE_PATH}
    ${COMMON_MINI_PATH}
    ${CONTROLLERS_PATH}
)

# ############################### Install ############################################################################################################

install(
    TARGETS ${TARGET}
    DESTINATION "${INSTALL_PATH}")

if(BUILD_EXAMPLES)
    install(
        TARGETS ${TARGET}
        DESTINATION "${CODE_EXAMPLES_BIN_PATH}")
endif(BUILD_EXAMPLES)
