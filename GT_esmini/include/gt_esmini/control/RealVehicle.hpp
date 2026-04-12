#pragma once

#include <cmath>
#include <fstream>
#include <string>
#include <algorithm>
#include <iostream>
#include "vehicle.hpp"

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

        void UpdatePhysics(double dt, double throttle, double brake, double steering, int gear = 1);

        // Dynamics accessors
        double GetPitch() const { return pitch_; } // pitch_ is in base class
        double GetRoll() const { return roll_; }
        double GetDynamicPitch() const { return dynamic_pitch_; }
        double GetDynamicRoll()  const { return dynamic_roll_; }
        double GetRPM() const { return rpm_; }
        double GetTorqueOutput() const { return GetTorque(rpm_); }

        void SetEngineBrakeFactor(double val) { params_.engine_brake = val; }
        double GetEngineBrakeFactor() const { return params_.engine_brake; }

        // Acceleration state (populated by UpdatePhysics, used by FFB)
        double latAcc_  = 0.0;   // lateral acceleration [m/s^2] (vehicle frame)
        double longAcc_ = 0.0;   // longitudinal acceleration [m/s^2] (vehicle frame)

        // Terrain attitude integration (NEW)
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

            // Torque curve shape (normalized parabola)
            double torque_peak_pos = 0.65; // Normalized RPM where peak torque occurs [0-1] (~4500 RPM for NA)
            double torque_min = 0.3;       // Minimum normalized torque at idle/redline [0-1]

            // Understeer parameters
            double understeer_factor = 0.0;          // 0.0 = disabled, typical: 0.0005-0.003
            double critical_speed = 30.0;            // Speed where understeer becomes noticeable [m/s]
            double max_understeer_reduction = 0.0;   // Maximum steering reduction [0-1 range]
        };

        void LoadParameters(const std::string& filename);
        
        // Calculate offset to fix rotation pivot (Pivot Adjustment)
        // returns {dx, dy, dz} in world aligned frame (approximated)
        void GetBodyPositionOffset(double& dx, double& dy, double& dz);

    private:
        VehicleParams params_;
        
        // Extended physics state
        double rpm_;
        double roll_; // New roll state

        // Rates for spring-damper model
        double pitch_rate_;
        double roll_rate_;

        // Terrain vs Dynamic separation (NEW)
        double terrain_pitch_ = 0.0;  // From TerrainTracker
        double terrain_roll_ = 0.0;   // From TerrainTracker
        double dynamic_pitch_ = 0.0;  // From spring-damper acceleration
        double dynamic_roll_ = 0.0;   // From spring-damper lateral force

        double idle_rpm_;
        double max_rpm_;
        double gear_ratio_; // Simple fixed gear for now
        int    gear_ = 1;   // 1=Fwd, 0=N, -1=Rev
        
        // Helper to calculate torque from RPM (simple curve)
        double GetTorque(double current_rpm) const;
    };
} // namespace gt_esmini
