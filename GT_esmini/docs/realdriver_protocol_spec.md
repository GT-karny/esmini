# RealDriver Protocol Spec (v2)

## Input packet (Python -> C++)
- Format: `[int32 lightMask][HostVehicleData protobuf bytes]`

## Output packet: Waypoint (C++ -> Python)
- Type: `2`
- Format: `[u8 type][u32 currentIndex][u32 count][waypoint array]`

## Output packet: Longitudinal profile (C++ -> Python)
- Type: `3`
- Format: `[u8 type][u32 count][points...]`
- Point struct: `(double t_offset, double v_target, double a_max, double j_max)`
- Planner defaults: 3.0s horizon, 20 points, 0.15s step
- Sending cadence: every frame

## Removed
- Type `1` target speed packet is removed in v2.
