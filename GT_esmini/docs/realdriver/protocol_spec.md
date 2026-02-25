# RealDriver Protocol Spec (v2)

## Input packet (Python -> C++)
- Format: `[int32 lightMask][HostVehicleData protobuf bytes]`
- Default listen port: `53995` (`BasePort`)

## Output packet: Waypoint (C++ -> Python)
- Type: `2`
- Format: `[u8 type][u32 currentIndex][u32 count][waypoint array]`
- Current waypoint binary layout follows `WaypointData` alignment.
- Python parser currently uses `56 bytes/waypoint` (`<dddI4xdi4xd`).
- Default output port: `54996` (`WaypointPort`)

## Output packet: Longitudinal profile (C++ -> Python)
- Type: `3`
- Format: `[u8 type][u32 count][points...]`
- Point struct: `(double t_offset, double v_target, double a_max, double j_max)`
- Planner defaults: 3.0s horizon, 20 points, 0.15s step
- Sending cadence: every frame
- Default output port: `54995` (`ClientPort`)

## Removed
- Type `1` target speed packet is removed in v2.
- Legacy `port + object_id` port offset is not used in current implementation.
