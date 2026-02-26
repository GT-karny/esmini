# DriverScript

`DriverScript` contains Python-side controllers for GT_esmini.

## Status

- `pythondriver`: recommended for `PythonDriverController` (embedded, synchronous per frame)
- `realdriver`: deprecated (maintenance-only, UDP-based compatibility path)

`realdriver` can still be imported, but it emits `DeprecationWarning`.

## Install

```bash
cd DriverScript
pip install -e .
```

## Packages

### `pythondriver` (recommended)

Embedded API contract:

```python
class EmbeddedController:
    def init(self, config: dict) -> None: ...
    def step(self, frame_data: dict) -> dict: ...
    def close(self) -> None: ...
```

Default script/class:

- `DriverScript/pythondriver/examples/scenario_drive_embedded.py`
- `EmbeddedController`

Returned dict keys from `step()`:

- `throttle` (float)
- `brake` (float)
- `steering` (float)
- `gear` (int)
- `light_mask` (int)
- `engine_brake` (float)
- `adas_states` (list[int])

### `realdriver` (deprecated)

Embedded bridge path only.

- `realdriver.scenario_drive.ScenarioDriveController` is now `mode="embedded"` only
- Legacy UDP mode has been removed

## OpenSCENARIO Controller Properties

Use these properties for embedded controller execution:

```xml
<Property name="esminiController" value="PythonDriverController"/>
<Property name="PythonScript" value="DriverScript/pythondriver/examples/scenario_drive_embedded.py"/>
<Property name="PythonClass" value="EmbeddedController"/>
<Property name="PythonHome" value=""/>
```

UDP properties (`BasePort`, `ClientPort`, `SendWaypoints`, `WaypointPort`) are not used by `PythonDriverController`.

## Migration Tool (`RealDriverController` -> `PythonDriverController`)

```bash
python scripts/migrate_realdriver_to_pythondriver.py GT_esmini/test/scenarios/realdriver_f*.xosc --output-dir GT_esmini/test/scenarios --patch-out artifacts/pythondriver_xosc_migration.patch
```

Behavior:

- rewrites `RealDriverController` to `PythonDriverController`
- removes UDP properties
- injects default `PythonScript` / `PythonClass` / `PythonHome` if missing

## Troubleshooting

- `PythonScript` path invalid: controller init fails at scenario start
- `import osi3` / `import google.protobuf` fails: ensure embedded/runtime Python has matching packages
- missing keys in `step()` return dict: bridge should report explicit error
