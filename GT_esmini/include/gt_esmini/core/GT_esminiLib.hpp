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
     * @brief Enable observation-based vehicle physics for non-GT-controller vehicles
     *
     * Applies spring-damper pitch/roll dynamics to all vehicles that do not have
     * a RealDriverController or PythonDriverController assigned.
     * Must be called after GT_Init / GT_InitWithArgs.
     */
    GT_ESMINI_API void GT_EnableVehiclePhysics();

    /**
     * @brief Enable bicycle-model heading correction for non-GT-controller vehicles
     *
     * Applies nose-leading heading offset so that the front of the vehicle
     * leads turns and lane changes, matching real vehicle behavior.
     * Must be called after GT_Init / GT_InitWithArgs.
     */
    GT_ESMINI_API void GT_EnableHeadingCorrection();

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

    // =====================================
    // HostVehicleData APIs
    // =====================================

    /**
     * Set host vehicle inputs for OSI HostVehicleData output
     * @param vehicle_id Vehicle ID (use -1 for first/ego vehicle)
     * @param throttle Throttle position [0.0, 1.0]
     * @param brake Brake position [0.0, 1.0]
     * @param steering Steering input (will be converted to wheel angle using config ratio)
     * @param gear Gear: -1=Reverse, 0=Neutral, 1+=Forward
     */
    GT_ESMINI_API void GT_SetHostVehicleInputs(int vehicle_id, double throttle, double brake, double steering, int gear);

    /**
     * Set host vehicle light mask for OSI HostVehicleData output
     * @param vehicle_id Vehicle ID (use -1 for first/ego vehicle)
     * @param light_mask Bitmask: bit0=LowBeam, bit1=HighBeam, bit2=IndicatorLeft, bit3=IndicatorRight,
     *                   bit4=Warning, bit5=FogFront, bit6=FogRear
     */
    GT_ESMINI_API void GT_SetHostVehicleLights(int vehicle_id, int light_mask);

    /**
     * Set host vehicle powertrain data for OSI HostVehicleData output
     * @param vehicle_id Vehicle ID (use -1 for first/ego vehicle)
     * @param rpm Engine RPM
     * @param torque Engine torque [Nm]
     */
    GT_ESMINI_API void GT_SetHostVehiclePowertrain(int vehicle_id, double rpm, double torque);

    /**
     * Set the active drive mode for the HVDEstimator (affects HVD reporting
     * for vehicles that don't have a GT custom controller assigned).
     *
     * Built-in modes (loaded from real_vehicle_params.json shift_schedule.modes):
     *   "comfort" - relaxed shift points, low RPM cruise (default)
     *   "sport"   - high RPM shift points, kickdown bias, rev-match blip on downshift
     *
     * @param mode Mode name (case-sensitive, null-terminated)
     * @return 0 on success, -1 if mode is unknown
     */
    GT_ESMINI_API int GT_SetDriveMode(const char* mode);

    /**
     * Retrieve this frame's serialized OSI HostVehicleData for a vehicle, in-process.
     *
     * Mirrors SE_GetOSIGroundTruth's in-process access pattern (a raw pointer into
     * a buffer owned by the DLL, valid until the next GT_Step) but for
     * HostVehicleData rather than GroundTruth. Exists so verification harnesses
     * that run GT_esminiLib.dll in-process (gt_lib.py) can read HVD without the
     * UDP 48199 transport, which does not fit an in-process harness
     * (req-vd-ad:REQ-AD-028 段c; design doc manualdrive_adas_design.md §8-5).
     *
     * Unlike SE_GetOSIGroundTruth, this does NOT force a re-serialization:
     * GT_Step already calls UpdateFromObjectState() + Send() for the ego
     * unconditionally every frame (HVD is not frequency-gated the way
     * GroundTruth is), so the buffer is already current by the time any caller
     * can reach this function.
     *
     * IMPORTANT limitation: GT_HostVehicleReporter holds exactly ONE
     * serialization buffer — whichever vehicle GT_Step most recently resolved
     * as ego/target, not one buffer per vehicle_id. A vehicle_id that does not
     * match that resolved vehicle is REFUSED (returns nullptr, *size = 0)
     * rather than silently handed a different vehicle's bytes mislabeled as
     * its own — a verification harness that reports the wrong vehicle's HVD as
     * "measured" is a worse failure than one that visibly gets nothing.
     *
     * @param vehicle_id Vehicle (object) id, or -1 for the first/ego vehicle
     *                    (same resolution rule as GT_SetHostVehicleInputs).
     * @param size       Output: number of bytes in the returned buffer. Written
     *                   as 0 on any failure path (null player, uninitialized
     *                   HVD reporter, unresolved/non-ego vehicle_id, nothing
     *                   serialized yet, or _USE_OSI undefined); left untouched
     *                   if size itself is null.
     * @return Pointer to the serialized HostVehicleData bytes (valid only
     *         until the next GT_Step — copy before stepping again), or
     *         nullptr if unavailable for any of the reasons above.
     */
    GT_ESMINI_API const void* GT_GetOSIHostVehicleData(int vehicle_id, int* size);

    // =====================================
    // Traffic Signal State API
    // =====================================

    /**
     * Get the current state string of a traffic signal.
     * State is set by TrafficSignalStateAction or TrafficSignalController during simulation.
     *
     * @param road_id    Road ID
     * @param index      Signal index (0 ~ SE_GetNumberOfRoadSigns(road_id)-1)
     * @param state      Output buffer for state string (e.g. "red;yellow;green")
     * @param bufferSize Buffer size
     * @return 0: success, -1: not found or not a traffic light
     */
    GT_ESMINI_API int GT_GetTrafficSignalState(int road_id, int index, char* state, int bufferSize);

    /**
     * Get VirtualDriver telemetry for a vehicle as a JSON string.
     *
     * Returns the aggregate VirtualDriverTelemetry (ego state, override flags,
     * short-plan preview, driver-model diagnostics, indicator) serialized to
     * JSON, so Web / Python / Electron can all read it without ABI marshaling.
     *
     * @param vehicle_id Vehicle (object) id assigned a VirtualDriverController
     * @param out_json   Output buffer for the JSON string
     * @param buf_size   Size of out_json
     * @return Number of bytes written (excluding NUL), or -1 if the vehicle has
     *         no VirtualDriverController / on error.
     */
    GT_ESMINI_API int GT_GetVirtualDriverTelemetry(int vehicle_id, char* out_json, int buf_size);

    /**
     * GT-flavored variant of SE_OpenOSISocket (auto-enables per-frame OSI frequency);
     * core SE_OpenOSISocket is vanilla upstream (audit BND-2 / R5-U1).
     *
     * Opens the OSI groundtruth UDP socket. Unlike vanilla SE_OpenOSISocket, this
     * forces the OSI frequency to 1 (send every frame) when it was left at 0, so the
     * in-process verification harness emits OSI each GT_Step even without --osi <hz>.
     *
     * @param ipaddr Destination IP for the OSI groundtruth UDP stream
     * @return 0 on success, -1 on failure (null player/osiReporter, OpenSocket error,
     *         or _USE_OSI undefined)
     */
    GT_ESMINI_API int GT_OpenOSISocket(const char* ipaddr);

    // =====================================
    // Log relay API (audit CORE-4 / GT-5)
    // =====================================

    /**
     * Log callback signature for GT_SetLogCallback.
     * @param level     0=unknown, 1=debug, 2=info, 3=warn, 4=error
     * @param message   Full formatted log line (trailing newline stripped)
     * @param user_data Opaque pointer passed through from GT_SetLogCallback
     */
    typedef void (*GT_LogCallbackFn)(int level, const char* message, void* user_data);

    /**
     * Register a callback that receives every esmini/GT log message with a level.
     * Bridges the core txtLogger (level-less, console-only) so callers can route
     * diagnostics to their own logging without scraping stdout. Pass callback=nullptr
     * to detach. Thread-safe.
     *
     * @param callback  Callback function, or nullptr to detach
     * @param user_data Opaque pointer forwarded to each callback invocation
     */
    GT_ESMINI_API void GT_SetLogCallback(GT_LogCallbackFn callback, void* user_data);

    /**
     * Copy the last error-level log message (NUL-terminated, truncated to fit) into
     * buffer. Populated during GT_Init / GT_InitWithArgs, so the cause is available
     * even when init returns a bare rc < 0.
     *
     * @param buffer      Destination buffer
     * @param buffer_size Size of buffer in bytes
     * @return Number of characters copied (excluding NUL), 0 if no error recorded,
     *         -1 if arguments are invalid.
     */
    GT_ESMINI_API int GT_GetLastError(char* buffer, int buffer_size);

#ifdef __cplusplus
}
#endif
