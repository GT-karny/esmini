# scripts/driver_script migration

Legacy driver_script modules were removed.

Use the maintained package under `DriverScript/realdriver`.

## Mapping
- `RealDriverClient.py` -> `DriverScript/realdriver/client.py`
- `OSIReceiver.py` -> `DriverScript/realdriver/osi_receiver.py`
- `PIDController.py` -> `DriverScript/realdriver/pid_controller.py`
- `esminiRMLib.py` -> `DriverScript/realdriver/rm_lib.py`
- target speed receiver -> `DriverScript/realdriver/udp_receivers.py` (`LongitudinalProfileReceiver`, UDP type=3)

See `DriverScript/docs/realdriver_migration_guide_v2.md` for migration details.
