#include "RealVehicle.hpp"
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Clamp helper
template <typename T>
T Clamp(T val, T min, T max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

namespace gt_esmini
{

RealVehicle::RealVehicle() : vehicle::Vehicle()
{
    // Default tuning values
    idle_rpm_ = 800.0;
    max_rpm_ = 7000.0;
    rpm_ = idle_rpm_;
    gear_ratio_ = 3.5; // Final drive * generic gear
    
    // Physics State
    roll_ = 0.0;
    pitch_rate_ = 0.0;
    roll_rate_ = 0.0;

    // Params init with defaults is automatic via struct
}

void RealVehicle::LoadParameters(const std::string& filename)
{
    std::ifstream file(filename);
    if (!file.is_open()) 
    {
        // LOG_INFO("RealVehicle params not found, using defaults: {}", filename);
        return;
    }

    std::string line;
    while (std::getline(file, line))
    {
        // Very simple "key": value parser
        auto parse_val = [&](const std::string& key, double& val) 
        {
             if (line.find(key) != std::string::npos) {
                 size_t colon = line.find(":");
                 if (colon != std::string::npos) {
                     try {
                         // Simple cleanup of value string could be needed but stod is robust ignoring leading whitespace
                         val = std::stod(line.substr(colon + 1));
                     } catch (...) {}
                 }
             }
        };

        parse_val("pitch_stiffness", params_.pitch_stiffness);
        parse_val("pitch_damping", params_.pitch_damping);
        parse_val("roll_stiffness", params_.roll_stiffness);
        parse_val("roll_damping", params_.roll_damping);
        parse_val("mass_height", params_.mass_height);
        parse_val("center_of_rotation_z_offset", params_.center_of_rotation_z_offset); 
        parse_val("max_pitch_deg", params_.max_pitch_deg);
        parse_val("max_roll_deg", params_.max_roll_deg);
        parse_val("steer_gain", params_.steer_gain);
        parse_val("max_speed", params_.max_speed);
        parse_val("max_acc", params_.max_acc);
        parse_val("idle_rpm", idle_rpm_);
        parse_val("max_rpm", max_rpm_);
        parse_val("gear_ratio", gear_ratio_);
    }
    
    SetMaxAcc(params_.max_acc);
    SetMaxSpeed(params_.max_speed);
}

void RealVehicle::GetBodyPositionOffset(double& dx, double& dy, double& dz)
{
    // Pivot Correction Logic:
    double z = params_.center_of_rotation_z_offset;
    
    // dx: Nose Up (+Pitch) -> Top moves Back (-X) relative to Pivot?
    // If Pivot is lower, and we rotate +Pitch (Nose Up), the Model Center (above pivot) moves BACK.
    dx = -z * std::sin(pitch_);
    
    // dy: Roll Right (+Roll) -> Top moves Right (+Y) relative to Pivot?
    dy = z * std::sin(roll_);
    
    // dz: Vertical drop due to tilt
    // Height = z * cos(p) * cos(r).
    // Offset relative to upright (z) is: z*cos*cos - z.
    // So we ADD (z*cos*cos - z) to the Z position.
    
    // Wait, the previous logic was:
    // gateway checks PosZ.
    // If we want the PIVOT to be at PosZ, then Model Center is at PosZ + z.
    // When tilted, Model Center is at PosZ + z*cos*cos.
    // So we just return the shift from the "Pivot Point".
    
    // The integration in ControllerRealDriver adds dz to posZ_.
    // If posZ_ is the Ground/Pivot Z (e.g. 0), then we want the Model Center Z.
    // Model Center Z = PivotZ + z * cos(p) * cos(r).
    // So dz = z * cos(p) * cos(r).
    
    dz = z * std::cos(pitch_) * std::cos(roll_) - z;
}


double RealVehicle::GetTorque(double current_rpm) const
{
    // Very simple torque curve: peaking at mid range (e.g., 3000-4000)
    // Normalized 0..1 output, will be multiplied by MaxAcc later
    
    // Parabolic-ish curve
    double normalized_rpm = (current_rpm - idle_rpm_) / (max_rpm_ - idle_rpm_);
    if (normalized_rpm < 0) normalized_rpm = 0;
    if (normalized_rpm > 1) normalized_rpm = 1;
    
    // Peak torque at 50% RPM range
    // 4 * x * (1-x) gives parabola 0->1->0
    // Allow some torque at idle and redline
    double base_torque = 0.4 + 0.6 * (4.0 * normalized_rpm * (1.0 - normalized_rpm)); 
    return base_torque;
}

void RealVehicle::UpdatePhysics(double dt, double throttle, double brake, double steering)
{
    if (dt <= 0.00001) return;

    // 1. Engine & RPM Logic
    // ---------------------
    // Target RPM is linearly proportional to throttle (simple model)
    // Or closer to physics: RPM increases based on Torque - Load
    
    // Simple load-based RPM approach:
    // If neutral (clutch disengaged): Engine responds fast to throttle
    // If in gear: RPM is tied to speed, but we are doing a simplified auto-trans model here.
    
    // Let's use a "Goal RPM" based on throttle, but filtered by inertia
    double target_rpm = idle_rpm_ + (max_rpm_ - idle_rpm_) * throttle;
    
    // Apply "Engine Inertia"
    double rpm_change_rate = 2000.0; // RPM per second change
    if (target_rpm > rpm_) {
        rpm_ += rpm_change_rate * dt;
        if (rpm_ > target_rpm) rpm_ = target_rpm;
    } else {
        rpm_ -= rpm_change_rate * dt; // Decelerate engine
        if (rpm_ < target_rpm) rpm_ = target_rpm; 
    }
    
    // Also, if vehicle speed is high, RPM cannot be too low (engine braking/coasting)
    // Approx mapping: Speed (m/s) -> RPM
    double mechanical_min_rpm = std::abs(speed_) * 60.0 * gear_ratio_ * 2.0; // tuning factor
    if (rpm_ < mechanical_min_rpm) 
    {
         // Engine spun up by wheels
         rpm_ = mechanical_min_rpm;
    }
    rpm_ = Clamp(rpm_, idle_rpm_, max_rpm_);

    // 2. Acceleration / Deceleration
    // ------------------------------
    
    // Acceleration force depends on Engine Torque (from RPM) * Throttle
    // We reuse SetMaxAcc from base, but modulate it.
    double available_torque = GetTorque(rpm_);
    double engine_force = available_torque * throttle * GetMaxAcc();
    
    double deceleration_force = 0.0;
    if (brake > 0)
    {
        deceleration_force = brake * GetMaxDec(); // Brake power
    }
    
    // Engine braking (drag)
    double drag_force = speed_ * speed_ * 0.005; // Air drag
    if (throttle < 0.05) drag_force += 2.0; // Engine braking constant
    
    // Net Acceleration
    // Direction calculation is simplified (assuming forward)
    double acc = engine_force - deceleration_force - drag_force;
    
    // [FIX 1] Prevent reversing on brake
    // Only apply negative acceleration if speed > 0
    if (speed_ <= 0.01 && acc < 0 && brake > 0)
    {
        acc = 0;
        speed_ = 0;
    } 
    else 
    {
        speed_ += acc * dt;
    }

    // Simple stop handling
    if (speed_ < 0 && throttle > 0 && brake > 0) // burnout/standstill logic simplified
    {
        speed_ = 0;
    }
    // Prevent reversing for now in this simple model if brake provided
    if (speed_ < -0.1) speed_ = -0.1; // Cap reverse speed (or 0)
    
    // 3. Steering
    // -----------
    // Use base class steering logic for now, it's decent kinematic model
    // Just inject steering input to wheel angle state
    
    double steer_max = params_.steer_gain; // Configurable
    double target_wheel_angle = -steering * steer_max; // - because left is positive in math usually, steering input might be reversed
    
    // Simple steering lag/rate limit
    double steer_rate = 5.0; // rad/s
    double diff = target_wheel_angle - wheelAngle_;
    if (std::abs(diff) < steer_rate * dt) {
        wheelAngle_ = target_wheel_angle;
    } else {
        wheelAngle_ += (diff > 0 ? 1 : -1) * steer_rate * dt;
    }
    
    // 4. Update Position (Kinematic Bicycle)
    // --------------------------------------
    // Reuse base Update() which expects speed_ and wheelAngle_ to be set
    // vehicle::Vehicle::Update(dt) does x, y, heading updates
    
    vehicle::Vehicle::Update(dt);

    // 5. Pitch and Roll (The "Real" part - Spring Damper Model)
    // -----------------------------------
    
    // Longitudinal Acceleration (current frame approximate)
    double long_acc = acc; 
    
    // Lateral Acceleration = v^2 / r = v * omega = v * (v / L * tan(delta)) approximate
    // Base vehicle calculates velAngleRelVehicleLongAxis_, we can use that for better slip
    // Simple: LatAcc = Speed * YawRate
    double yaw_rate = headingDot_; // Calculated in base Update()
    double lat_acc = speed_ * yaw_rate;
    
    // [FIX 2 & 3] Natural Suspension Dynamics & Direction Correction
    
    // Pitch Logic
    // Accelerate (Forward) -> Inertia pushes mass BACK -> Nose UP (+Pitch).
    // User said "Inverted". So I will flip this to: Acc > 0 -> Pitch < 0 (Nose Down??).
    // Wait, Acc elicits Nose Up is physically correct.
    // Parameter mass_height determines direction/magnitude. 
    // If param is positive -> Nose Up. If we want inverted, make param negative or logic negative.
    // I will use -param logic as requested before.
    
    // NOTE: In esmini/OSI: 
    // Pitch: Rotation around Y-axis. Positive = Nose Up.
    // Roll: Rotation around X-axis. Positive = Right side down.
    
    // Implementation:
    // Force:
    double pitch_forcing = -params_.mass_height * long_acc; 
    
    double pitch_acc = (-params_.pitch_stiffness * pitch_) - (params_.pitch_damping * pitch_rate_) + pitch_forcing;
    pitch_rate_ += pitch_acc * dt;
    pitch_ += pitch_rate_ * dt;
    
    // Roll Logic
    // Left Turn (Yaw Rate > 0) -> Centrifugal Force to Right -> Body Rolls to Right (+Roll).
    // User says inverted. So Left Turn -> Roll Left (-Roll)?
    // I will FLIP the sign of forcing.
    double roll_forcing = params_.mass_height * lat_acc;  
    
    double roll_acc = (-params_.roll_stiffness * roll_) - (params_.roll_damping * roll_rate_) + roll_forcing;
    roll_rate_ += roll_acc * dt;
    roll_ += roll_rate_ * dt;
    
    // Clamp angles
    double lim_p = params_.max_pitch_deg * M_PI / 180.0;
    double lim_r = params_.max_roll_deg * M_PI / 180.0;
    pitch_ = Clamp(pitch_, -lim_p, lim_p);
    roll_ = Clamp(roll_, -lim_r, lim_r);
    
    // Damping/Restitution is handled by the -k*x and -c*v terms automatically.
    // It will oscillate and settle naturally.
}

} // namespace gt_esmini
