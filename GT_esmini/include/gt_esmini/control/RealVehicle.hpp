#pragma once

#include <cmath>
#include <fstream>
#include <string>
#include <algorithm>
#include <iostream>
#include <vector>
#include "vehicle.hpp"
#include "gt_esmini/control/manualdrive/AutoTransmission.hpp"
#include "gt_esmini/control/manualdrive/EngineModel.hpp"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace gt_esmini
{
    class RealVehicle : public vehicle::Vehicle
    {
    public:
        RealVehicle();
        // Standard Vehicle Interface
        // SetPos, SetSpeed etc are inherited from Vehicle base
        virtual ~RealVehicle() {}

        // Legacy single-gear physics path (kept for non-ManualDrive callers).
        void UpdatePhysics(double dt, double throttle, double brake, double steering, int gear = 1);

        // Forward-AT physics path: paddle inputs drive the embedded
        // AutoTransmission + EngineModel and produce a real geared driveline.
        void UpdatePhysicsAT(double dt, double throttle, double brake, double steering,
                             bool paddle_up_pressed, bool paddle_down_pressed);

        // Dynamics accessors
        double GetPitch() const { return pitch_; } // pitch_ is in base class
        double GetRoll() const { return roll_; }
        double GetDynamicPitch() const { return dynamic_pitch_; }
        double GetDynamicRoll()  const { return dynamic_roll_; }
        double GetRPM() const { return rpm_; }
        double GetTorqueOutput() const;
        int    GetCurrentGear() const { return gear_; }   // -1 R, 0 N, 1..N forward
        bool   IsATManualMode() const { return at_manual_mode_; }

        void SetEngineBrakeFactor(double val) { params_.engine_brake = val; }
        double GetEngineBrakeFactor() const { return params_.engine_brake; }

        // Acceleration state (populated by UpdatePhysics, used by FFB)
        double latAcc_  = 0.0;   // lateral acceleration [m/s^2] (vehicle frame)
        double longAcc_ = 0.0;   // longitudinal acceleration [m/s^2] (vehicle frame)

        // Terrain attitude integration.
        // FROZEN STUB (audit CTL-4): terrain following is NOT implemented. This is only the
        // attitude-blend scaffold; every caller currently feeds pitch=roll=0, so the terrain
        // component is always zero. No road-normal sampling / enable path exists and none is
        // planned. Kept (not deleted) pending a product decision. See GT_esmini/README.md.
        void SetTerrainAttitude(double pitch, double roll);
        void GetCombinedAttitude(double& pitch, double& roll) const;

        // Parameter Management
        struct VehicleParams
        {
            double pitch_stiffness = 10.0;
            double pitch_damping = 2.0;
            double roll_stiffness = 12.0;
            double roll_damping = 3.0;
            double mass_height = 0.05;
            double center_of_rotation_z_offset = 0.5; // Distance from CG/Pivot to Model Origin (usually ~half height)
            double max_pitch_deg = 5.0;
            double max_roll_deg = 5.0;
            double steer_gain = 0.61; // ~35 deg max wheel angle (Corolla steering ratio)
            double max_speed = 55.0;  // ~200 km/h (economy sedan electronic limiter)
            double max_acc = 4.0;     // Peak longitudinal acceleration [m/s²] (Corolla/Civic class)
            double max_dec = 10.0;    // Peak braking deceleration [m/s²] (100-0 km/h ~38-41m)
            double reverse_gear_ratio = 1.5; // Multiplier for reverse torque

            // Aerodynamic drag: a_drag = drag_coeff * v² [m/s²]
            // Tuned so terminal velocity ≈ max_speed at full throttle
            double drag_coeff = 0.0013;  // ~Corolla: 0.5*1.225*0.29*2.15/1350 ≈ 0.000283 (real), scaled for model

            // Engine braking when throttle released [m/s²]
            double engine_brake = 0.4;

            // Torque curve shape (legacy normalized parabola; only used by
            // UpdatePhysics() legacy path)
            double torque_peak_pos = 0.65;
            double torque_min = 0.3;

            // Understeer parameters
            double understeer_factor = 0.0;          // 0.0 = disabled, typical: 0.0005-0.003
            double critical_speed = 30.0;            // Speed where understeer becomes noticeable [m/s]
            double max_understeer_reduction = 0.0;   // Maximum steering reduction [0-1 range]

            // -- Forward AT driveline parameters --
            double mass_kg                = 1450.0;   // Civic FL1 class curb mass
            double wheel_radius_m         = 0.32;
            double drivetrain_efficiency  = 0.92;
            double v_lockup_mps           = 8.0;      // torque-converter lockup speed
            // Max engine slip RPM above turbine at full throttle / full slip.
            // Tuned so WOT-from-rest target ≈ stall RPM (~2200 for a Civic-class TC).
            double tc_slip_rpm_max        = 1500.0;
            std::vector<double> gear_ratios = {3.642, 2.080, 1.361, 1.024, 0.830, 0.686};
            double final_drive_ratio      = 4.105;
            double reverse_ratio          = 3.583;    // physical reverse gear ratio

            // -- Engine drag (compression braking) for AT path --
            // Computed as drag_torque * total_ratio * eta / r / mass.
            double engine_drag_base_nm    = 30.0;
            double engine_drag_per_krpm   = 10.0;

            // -- Shift event (torque cut + RPM dip/blip) --
            double shift_event_duration_s = 0.18;
            double upshift_dip_rpm        = 200.0;
            double downshift_blip_rpm     = 0.0;     // comfort default
            double shift_torque_factor    = 0.3;     // engine torque scaling during event

            // -- Resistance / aero (used by AT path; legacy uses drag_coeff above) --
            // Defaults model an 11th-gen Civic Sport Touring (Cd≈0.27, A≈2.2 m²).
            double aero_drag_cd           = 0.27;
            double frontal_area_m2        = 2.2;
            double air_density            = 1.225;     // kg/m³ at sea level
            double rolling_resistance_coeff = 0.011;   // typical passenger tire
        };

        void LoadParameters(const std::string& filename);

        // Calculate offset to fix rotation pivot (Pivot Adjustment)
        // returns {dx, dy, dz} in world aligned frame (approximated)
        void GetBodyPositionOffset(double& dx, double& dy, double& dz);

    private:
        VehicleParams params_;

        // Extended physics state
        //
        // rpm_ is the DISPLAY value: base RPM plus the cosmetic idle-jitter
        // overlay (EngineModel::GetRPM). It is what GetRPM() reports to gauges
        // and OSI, and it must NEVER enter a force calculation — the jitter is
        // seeded from std::random_device by default (idle_jitter_seed: 0), so
        // anything downstream of it is nondeterministic across processes.
        // base_rpm_ is the jitter-free engine speed and is the ONLY one physics
        // may read. See UpdatePhysicsAT's engine-compression-braking term for
        // the defect this split fixes.
        double rpm_;
        double base_rpm_ = 0.0;
        double roll_; // New roll state

        // Rates for spring-damper model
        double pitch_rate_;
        double roll_rate_;

        // Terrain/external attitude vs dynamic body attitude separation.
        double terrain_pitch_ = 0.0;
        double terrain_roll_ = 0.0;
        double dynamic_pitch_ = 0.0;  // From spring-damper acceleration
        double dynamic_roll_ = 0.0;   // From spring-damper lateral force

        double idle_rpm_;
        double max_rpm_;
        double gear_ratio_; // Legacy fixed gear (used by legacy UpdatePhysics)
        int    gear_ = 1;   // -1 R, 0 N, 1..N forward (drivetrain-engaged gear)

        // Forward-AT components (used by UpdatePhysicsAT)
        AutoTransmission auto_trans_;
        EngineModel      engine_;
        bool             at_manual_mode_ = false;
        bool             at_seeded_      = false;
        double           engine_torque_nm_ = 0.0;  // last computed engine torque

        // Shift event: torque cut + RPM dip/blip during gear changes.
        double           shift_event_timer_s_ = 0.0;
        int              shift_event_dir_     = 0;  // +1=upshift, -1=downshift

        // Helper to calculate normalized torque from RPM (legacy curve)
        double GetTorque(double current_rpm) const;

        // Apply steering / kinematic update / pitch & roll dynamics for a
        // given longitudinal acceleration. Shared between legacy and AT paths.
        void StepLateralAndAttitude(double dt, double steering, double long_acc);

        // Configure AT and engine parameters from current VehicleParams.
        void ConfigureATAndEngine();
    };
} // namespace gt_esmini
