include_guard()

# ############################### Setting internal paths ##############################################################

macro(set_project_internal_paths)

    set(CODE_EXAMPLES_BIN_PATH
        ${CMAKE_SOURCE_DIR}/code-examples-bin)
    set(ENVIRONMENT_SIMULATOR_PATH
        ${CMAKE_SOURCE_DIR}/EnvironmentSimulator)
    set(RESOURCES_PATH
        ${CMAKE_SOURCE_DIR}/resources)
    set(RUN_PATH
        ${CMAKE_SOURCE_DIR}/run)
    set(SUPPORT_PATH
        ${CMAKE_SOURCE_DIR}/support)
    set(INSTALL_PATH
        ${CMAKE_SOURCE_DIR}/bin)

    set(APPLICATIONS_PATH
        ${ENVIRONMENT_SIMULATOR_PATH}/Applications)
    set(CODE_EXAMPLES_PATH
        ${ENVIRONMENT_SIMULATOR_PATH}/code-examples)
    set(LIBRARIES_PATH
        ${ENVIRONMENT_SIMULATOR_PATH}/Libraries)
    set(MODULES_PATH
        ${ENVIRONMENT_SIMULATOR_PATH}/Modules)

    set(COMMON_MINI_PATH
        ${MODULES_PATH}/CommonMini)
    set(CONTROLLERS_PATH
        ${MODULES_PATH}/Controllers)
    set(PLAYER_BASE_PATH
        ${MODULES_PATH}/PlayerBase)
    set(ROAD_MANAGER_PATH
        ${MODULES_PATH}/RoadManager)
    set(SCENARIO_ENGINE_PATH
        ${MODULES_PATH}/ScenarioEngine)
    set(VIEWER_BASE_PATH
        ${MODULES_PATH}/ViewerBase)

    set(ESMINI_RM_LIB_PATH
        ${LIBRARIES_PATH}/esminiRMLib)
    set(ESMINI_LIB_PATH
        ${LIBRARIES_PATH}/esminiLib)

    set(REPLAYER_PATH
        ${APPLICATIONS_PATH}/replayer)

endmacro()

# ############################### Setting project external paths ###################################################

macro(set_project_external_paths)

    set(EXTERNALS_PATH
        ${CMAKE_SOURCE_DIR}/externals)
    set(EXTERNALS_DIRENT_PATH
        ${EXTERNALS_PATH}/dirent)
    set(EXTERNALS_EXPR_PATH
        ${EXTERNALS_PATH}/expr)
    set(EXTERNALS_GOOGLETEST_PATH
        ${EXTERNALS_PATH}/googletest)
    set(EXTERNALS_OSG_PATH
        ${EXTERNALS_PATH}/osg)
    # [GT_ODR:osi-path] GT_esmini: keep the flat osi base path -- the GT OSI 3.7.0 package lives in
    # externals/osi/v11 (tracked in-repo, incl. zlib 1.2.13 artifacts for the v3.4.0 gzip feature).
    # Upstream v3.4.0 appends ${OSI_RELEASE_TAG}, which composes externals/osi/<tag>/v11 = nonexistent
    # and re-downloads the upstream OSI 3.5.0 archive, silently downgrading the GT OSI 3.7.0 upgrade
    # (ego Identifier wire emission). Recorded in gt_roadmanager_patches.md section 0.
    #
    # On MSVC the GT OSI package is VENDORED, never downloaded, so OSI_VERSION does not select it: see
    # set_osi_resolved_version() below for why the version is derived from the package instead.
    set(EXTERNALS_OSI_PATH
        ${EXTERNALS_PATH}/osi)
    set(EXTERNALS_PUGIXML_PATH
        ${EXTERNALS_PATH}/pugixml)
    set(EXTERNALS_SUMO_PATH
        ${EXTERNALS_PATH}/sumo)
    set(EXTERNALS_IMPLOT_PATH
        ${EXTERNALS_PATH}/implot/${IMPLOT_RELEASE_TAG})
    set(EXTERNALS_YAML_PATH
        ${EXTERNALS_PATH}/yaml)
    set(EXTERNALS_FMT_PATH
        ${EXTERNALS_PATH}/fmt)
    set(MODELS_PATH
        ${RESOURCES_PATH}/models)

endmacro()

# ############################### Setting OS specific paths ########################################################

macro(set_project_os_specific_paths)

    if(APPLE)
        set(EXTERNALS_OSG_OS_SPECIFIC_PATH
            ${EXTERNALS_OSG_PATH}/mac)
        set(EXTERNALS_OSI_OS_SPECIFIC_PATH
            ${EXTERNALS_OSI_PATH}/mac)
        set(EXTERNALS_SUMO_OS_SPECIFIC_PATH
            ${EXTERNALS_SUMO_PATH}/mac)
        set(EXTERNALS_GOOGLETEST_OS_SPECIFIC_PATH
            ${EXTERNALS_GOOGLETEST_PATH}/mac)
        set(EXTERNALS_IMPLOT_OS_SPECIFIC_PATH
            ${EXTERNALS_IMPLOT_PATH}/mac)
        set(TIME_LIB
            "")
    elseif(LINUX)
        set(EXTERNALS_OSG_OS_SPECIFIC_PATH
            ${EXTERNALS_OSG_PATH}/linux)
        set(EXTERNALS_OSI_OS_SPECIFIC_PATH
            ${EXTERNALS_OSI_PATH}/linux)
        set(EXTERNALS_SUMO_OS_SPECIFIC_PATH
            ${EXTERNALS_SUMO_PATH}/linux)
        set(EXTERNALS_GOOGLETEST_OS_SPECIFIC_PATH
            ${EXTERNALS_GOOGLETEST_PATH}/linux)
        set(EXTERNALS_IMPLOT_OS_SPECIFIC_PATH
            ${EXTERNALS_IMPLOT_PATH}/linux)
        set(TIME_LIB
            "")
    elseif(MINGW)
        set(SOCK_LIB
            Ws2_32.lib)
        set(TIME_LIB
            winmm)
    elseif(MSVC)
        if("${CMAKE_VS_PLATFORM_NAME}"
           STREQUAL
           "Win32")
            message("Win32 configurations not supported")
        else()
            set(EXTERNALS_OSG_OS_SPECIFIC_PATH
                ${EXTERNALS_OSG_PATH}/v10)
            # [GT_ODR:osi-path] v10 (OSI 3.5.0) -> v11 (OSI 3.7.0), commit 9fffa06e. This line IS the
            # OSI selection for the GT build -- the single place that decides which package is compiled
            # and linked against. MSVC only; Linux/macOS have no vendored GT package (see ci.yml `test`).
            set(EXTERNALS_OSI_OS_SPECIFIC_PATH
                ${EXTERNALS_OSI_PATH}/v11)
            set(EXTERNALS_SUMO_OS_SPECIFIC_PATH
                ${EXTERNALS_SUMO_PATH}/v10)
            set(EXTERNALS_GOOGLETEST_OS_SPECIFIC_PATH
                ${EXTERNALS_GOOGLETEST_PATH}/v10)
            set(EXTERNALS_IMPLOT_OS_SPECIFIC_PATH
                ${EXTERNALS_IMPLOT_PATH}/v10)
            set(SOCK_LIB
                Ws2_32.lib)
            set(TIME_LIB
                "")
        endif()
    endif()

    if(MSVC)
        set(EXTERNALS_DIRENT_INCLUDES
            "${EXTERNALS_DIRENT_PATH}/win")
    else()
        set(EXTERNALS_DIRENT_INCLUDES
            "")
    endif()

endmacro()

# ############################### Resolving the OSI package version ################################################

# [GT_ODR:osi-path] GT_esmini: the OSI version is DERIVED from the package on disk, never declared.
#
# Upstream's OSI_VERSION is not "the OSI version we build against". It is the esmini-dependencies
# release-tag selector -- version_mapping.cmake maps it to OSI_RELEASE_TAG / OSI_TAG_URL (the download
# URL in cloud/set_cloud_links.cmake), and external/osi.cmake additionally gates its library-name list
# on it. On MSVC that selector is inert: GT vendors its OSI in-repo (externals/osi/v11, .gitignore
# negation + LFS, produced by scripts/generate_osi_libs.sh at OSI 3.7.0) and no esmini-dependencies
# release corresponds to it, so there is no tag OSI_VERSION could truthfully name. On Linux/macOS there
# is no vendored GT package and upstream's download still applies -- which is why a full USE_OSI build
# only succeeds on Windows (see the `test` job matrix comment in .github/workflows/ci.yml).
#
# That leaves one honest source for "which OSI did we actually resolve": the package's own VERSION file.
# It ships with the binaries and therefore cannot drift from them. Recomputed on every configure (FORCE)
# -- a cached copy could go stale, which is the failure mode this replaces. Must be called AFTER any
# download that could materialize the package.
macro(set_osi_resolved_version)

    # Named for the RESOLVED package, not upstream's OSI_PACKAGE_URL -- those can be two different OSIs.
    set(_osi_version_file
        "${EXTERNALS_OSI_OS_SPECIFIC_PATH}/VERSION")

    if(NOT
       EXTERNALS_OSI_OS_SPECIFIC_PATH
       OR NOT
          IS_DIRECTORY
          "${EXTERNALS_OSI_OS_SPECIFIC_PATH}")
        set(_osi_resolved_version
            "not-found")
    elseif(NOT
           EXISTS
           "${_osi_version_file}")
        # Only GT's own generate_osi_libs.sh packages carry a VERSION file; the upstream
        # esmini-dependencies archives do not, so the version is genuinely unknown there.
        set(_osi_resolved_version
            "unknown")
    else()
        # OSI ships its version as a `VERSION_MAJOR = 3` / `_MINOR` / `_PATCH` triplet.
        file(READ
             "${_osi_version_file}"
             _osi_version_text)
        set(_osi_version_fields
            "")
        foreach(
            _osi_field
            IN
            ITEMS VERSION_MAJOR
                  VERSION_MINOR
                  VERSION_PATCH)
            if("${_osi_version_text}" MATCHES "${_osi_field}[ \t]*=[ \t]*([0-9]+)")
                list(APPEND _osi_version_fields ${CMAKE_MATCH_1})
            endif()
        endforeach()

        list(LENGTH _osi_version_fields _osi_version_field_count)
        if(_osi_version_field_count EQUAL 3)
            # A list stringifies with ";" separators -- REPLACE joins it (string(JOIN) needs CMake 3.12,
            # this project's floor is 3.10).
            string(REPLACE ";"
                           "."
                           _osi_resolved_version
                           "${_osi_version_fields}")
        else()
            set(_osi_resolved_version
                "unparsable")
        endif()
    endif()

    set(OSI_RESOLVED_VERSION
        "${_osi_resolved_version}"
        CACHE STRING
              "OSI version of the package in EXTERNALS_OSI_OS_SPECIFIC_PATH (derived from its VERSION file; read-only)"
              FORCE)

    # Re-document upstream's OSI_VERSION in place so CMakeCache.txt cannot be read as a version claim.
    # The VALUE is deliberately left untouched: version_mapping.cmake maps no other value, and
    # external/osi.cmake FATALs on anything but "3.5.0" -- both upstream files stay pristine.
    set(OSI_VERSION
        "${OSI_VERSION}"
        CACHE STRING
              "esmini-dependencies OSI download-tag selector -- NOT the OSI version in use; see OSI_RESOLVED_VERSION"
              FORCE)

endmacro()

# ############################### Setting project includes #########################################################

macro(set_project_includes)

    set(EXTERNALS_OSG_INCLUDES
        ${EXTERNALS_OSG_OS_SPECIFIC_PATH}/build/include
        ${EXTERNALS_OSG_OS_SPECIFIC_PATH}/include)
    set(EXTERNALS_OSI_INCLUDES
        ${EXTERNALS_OSI_OS_SPECIFIC_PATH}/include)
    set(EXTERNALS_SUMO_INCLUDES
        ${EXTERNALS_SUMO_OS_SPECIFIC_PATH}/include)
    set(EXTERNALS_GOOGLETEST_INCLUDES
        ${EXTERNALS_GOOGLETEST_OS_SPECIFIC_PATH}/include)
    set(EXTERNALS_IMPLOT_INCLUDES
        ${EXTERNALS_IMPLOT_OS_SPECIFIC_PATH}/include/implot
        ${EXTERNALS_IMPLOT_OS_SPECIFIC_PATH}/include/imgui
        ${EXTERNALS_IMPLOT_OS_SPECIFIC_PATH}/include/imgui/backends
        ${EXTERNALS_IMPLOT_OS_SPECIFIC_PATH}/include/glfw)
    set(EXTERNALS_YAML_INCLUDES
        ${EXTERNALS_YAML_PATH})
    set(EXTERNALS_FMT_INCLUDES
        ${EXTERNALS_FMT_PATH}/include)


endmacro()

# ############################### Setting project library paths ####################################################

macro(set_project_library_paths)

    set(EXTERNALS_OSG_LIBRARY_PATH
        ${EXTERNALS_OSG_OS_SPECIFIC_PATH}/lib)

    set(EXTERNALS_OSG_PLUGINS_LIBRARY_PATH
        ${EXTERNALS_OSG_LIBRARY_PATH}/osgPlugins-3.6.5)

    if(DYN_PROTOBUF)
        set(EXTERNALS_OSI_LIBRARY_PATH
            ${EXTERNALS_OSI_OS_SPECIFIC_PATH}/lib-dyn)
    else()
        set(EXTERNALS_OSI_LIBRARY_PATH
            ${EXTERNALS_OSI_OS_SPECIFIC_PATH}/lib)
    endif(DYN_PROTOBUF)

    set(EXTERNALS_SUMO_LIBRARY_PATH
        ${EXTERNALS_SUMO_OS_SPECIFIC_PATH}/lib)

    set(EXTERNALS_GTEST_LIBRARY_PATH
        ${EXTERNALS_GOOGLETEST_OS_SPECIFIC_PATH}/lib)

    set(EXTERNALS_IMPLOT_LIBRARY_PATH
        ${EXTERNALS_IMPLOT_OS_SPECIFIC_PATH}/lib)

endmacro()
