# RealDriver Migration Guide v2

## Breaking changes
- Longitudinal UDP packet `type=1` (single target speed) is removed.
- New longitudinal UDP packet is `type=3` (speed profile points).
- Legacy scripts under `scripts/driver_script` were removed.

## New package layout
- `realdriver/control`
- `realdriver/planning`
- `realdriver/io`
- `realdriver/model`
- `realdriver/protocol`

## Longitudinal profile
- Packet type: `3`
- Header: `u8 type` + `u32 count`
- Point: `(t_offset, v_target, a_max, j_max)` as little-endian doubles
- Horizon: 3s, 20 points, 0.15s step
- Consumer interpolation: linear

## Receiver migration
- Old: `TargetSpeedReceiver.receive_all() -> float | None`
- New: `LongitudinalProfileReceiver.receive_all() -> list[LonProfilePoint] | None`
- Typical use:

```python
profile = receiver.receive_all()
if profile:
    target_speed = profile[0].v_target
```
