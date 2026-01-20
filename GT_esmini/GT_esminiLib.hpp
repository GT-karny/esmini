/*
 * GT_esmini - Extended esmini with Light Functionality
 * https://github.com/esmini/esmini
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2024 GT_esmini contributors
 */

#pragma once

#if defined(_WIN32)
#if defined(GT_ESMINI_STATIC)
#define GT_ESMINI_API
#elif defined(GT_ESMINI_EXPORTS) || defined(GT_esminiLib_EXPORTS)
#define GT_ESMINI_API __declspec(dllexport)
#else
#define GT_ESMINI_API __declspec(dllimport)
#endif
#else
#define GT_ESMINI_API
#endif

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief GT_esmini initialization function (replaces esmini's SE_Init)
     * 
     * Phase 1: Stub implementation
     * Phase 3: Implement actual initialization logic
     * 
     * @param oscFilename OpenSCENARIO file path
     * @param disable_ctrls Controller disable flag
     * @return 0: success, -1: failure
     */
    GT_ESMINI_API int GT_Init(const char* oscFilename, int disable_ctrls);
    
    /**
     * @brief GT_esmini initialization function with arguments (replaces esmini's SE_InitWithArgs)
     * 
     * Parses arguments, sanitizes scenario if needed, and initializes esmini.
     * 
     * @param argc Argument count
     * @param argv Argument vector
     * @return 0: success, -1: failure
     */
    GT_ESMINI_API int GT_InitWithArgs(int argc, const char* argv[]);

    /**
     * @brief GT_esmini update function (called after esmini's SE_Step)
     * 
     * Phase 1: Stub implementation
     * Phase 3: Implement AutoLight update logic
     * 
     * @param dt Delta time (seconds)
     */
    GT_ESMINI_API void GT_Step(double dt);

    /**
     * @brief Enable AutoLight for GT_esmini
     * 
     * Phase 1: Stub implementation
     * Phase 3: Implement AutoLight enable logic
     */
    GT_ESMINI_API void GT_EnableAutoLight();

    /**
     * @brief GT_esmini cleanup
     * 
     * Phase 1: Stub implementation
     * Phase 3: Implement resource cleanup logic
     */
    GT_ESMINI_API void GT_Close();

    /**
     * @brief Get light state for a vehicle (Debug/Inspection)
     * 
     * @param vehicleId Vehicle ID (SE_GetObjectId)
     * @param lightType Integer casting of VehicleLightType
     *        0: DaytimeRunning, 1: LowBeam, 2: HighBeam, 3: Fog, 4: FogFront, 5: FogRear,
     *        6: Brake, 7: Warning, 8: IndicatorLeft, 9: IndicatorRight, 10: Reversing, ...
     * @return 0: Off, 1: On, 2: Flashing, -1: Error/Vehicle Not Found/No Extension
     */
    // Set light state for a vehicle (for external controllers like FMU)
GT_ESMINI_API void GT_SetExternalLightState(int vehicleId, int lightType, int mode);

    GT_ESMINI_API int GT_GetLightState(int vehicleId, int lightType);

    /**
     * Report object velocity (GT extension with speed sync)
     * This function wraps SE_ReportObjectVel and additionally updates the scalar speed.
     * 
     * @param object_id ID of the object
     * @param timestamp Timestamp (currently unused)
     * @param x_vel Velocity X component (global coordinates)
     * @param y_vel Velocity Y component (global coordinates)
     * @param z_vel Velocity Z component (global coordinates)
     * @return 0 on success, -1 on failure
     */
    GT_ESMINI_API int GT_ReportObjectVel(int object_id, float timestamp, float x_vel, float y_vel, float z_vel);

    /**
     * Get Local ID from Global ID using OSI GroundTruth
     * @param global_id The global ID to search for
     * @return The local ID if found, -1 if not found
     */
    GT_ESMINI_API int GT_GetLocalIdFromGlobalId(int global_id);

#ifdef __cplusplus
}
#endif
