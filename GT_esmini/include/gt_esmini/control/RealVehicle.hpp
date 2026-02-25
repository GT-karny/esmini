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
        double GetRPM() const { return rpm_; }
        double GetTorqueOutput() const { return GetTorque(rpm_); }

        void SetEngineBrakeFactor(double val) { engine_brake_factor_ = val; }
        double engine_brake_factor_ = 0.49;

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
            double steer_gain = 0.7; // ~40 deg max
            double max_speed = 60.0;
            double max_acc = 10.0;
            double reverse_gear_ratio = 1.5; // Multiplier for reverse torque

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
