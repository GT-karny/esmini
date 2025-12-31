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
    GT_ESMINI_API int GT_GetLightState(int vehicleId, int lightType);

#ifdef __cplusplus
}
#endif
