# Component Knowledge Graph: DriverScript/

> **Development Freeze**: Python-related features (PythonDriverController, Embedded Python, DriverScript) are frozen as of v0.8. Existing functionality remains available but no new features are planned.

Python-side driver control system. Communicates with GT_Sim (C++) via UDP.

## 1. Directory Structure

| Directory/File | Role |
| :--- | :--- |
| `realdriver/` | Lateral/longitudinal controllers for real-time vehicle control |
| `pythondriver/` | Integrated Python driver (scenario-driven) |
| `tests/` | pytest test suite |
| `bin/` | DLL copy target (GT_esminiLib.dll, etc.) |
| `runtime_api.py` | GT_Sim runtime interaction API |
| `argspec_utils.py` | CLI argument specification utilities |
| `requirements.txt` | Python dependencies |
| `setup.py` | Package setup |

## 2. Python Environment

- **venv**: `.venv/` (Python 3.12)
- **Activate**: `DriverScript/.venv/Scripts/activate`
- **Dependencies**: `pip install -r requirements.txt`
- **Rule**: All scripts MUST be executed via `DriverScript/.venv/Scripts/python.exe`

## 3. Communication Protocol

GT_Sim (C++) ↔ PythonDriver (Python) communicate via UDP:
- C++ → Python: OSI GroundTruth, waypoints, target speed
- Python → C++: Throttle, steering, brake commands
- Transport layer: `GT_esmini/src/io/` (C++ side), `realdriver/` (Python side)

## 4. Testing

```bash
DriverScript/.venv/Scripts/python.exe -m pytest tests/ -v
```

## 5. Key Entry Points

- `pythondriver/scenario_drive.py` — Main scenario execution driver
- `pythondriver/scenario_drive_embedded.py` — Embedded Python variant
- `realdriver/lateral_controller.py` — Pure pursuit lateral control
- `realdriver/longitudinal_controller.py` — PID + feedforward speed control
