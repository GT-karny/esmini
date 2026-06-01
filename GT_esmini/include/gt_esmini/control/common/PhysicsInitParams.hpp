#pragma once

#include <string>

namespace gt_esmini
{

// Backend-agnostic initialization parameters for IPhysicsBackend.
//
// Decouples the physics backend from any single controller's config schema
// (e.g. ManualDriveConfig), so RealVehicleBackend / NetworkPhysicsBridge can
// be shared by ControllerManualDrive and ControllerVirtualDriver alike. Each
// controller builds this from its own JSON config.
struct PhysicsInitParams
{
    // RealVehicleBackend: vehicle physics parameter file (resolved via ConfigLoader).
    std::string vehicle_params_file;

    // NetworkPhysicsBridge: external physics simulator transport.
    std::string network_transport_type = "udp";  // "udp" | "tcp"
    std::string network_host           = "127.0.0.1";
    int         network_cmd_port       = 0;
    int         network_state_port     = 0;
};

} // namespace gt_esmini
