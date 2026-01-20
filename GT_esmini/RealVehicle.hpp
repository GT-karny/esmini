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

        void UpdatePhysics(double dt, double throttle, double brake, double steering);

        // Dynamics accessors
        double GetPitch() const { return pitch_; } // pitch_ is in base class
        double GetRoll() const { return roll_; }

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

        double idle_rpm_;
        double max_rpm_;
        double gear_ratio_; // Simple fixed gear for now

        
        // Helper to calculate torque from RPM (simple curve)
        double GetTorque(double current_rpm) const;
    };
}
