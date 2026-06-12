# Component Knowledge Graph: GT_esmini

This document defines the architectural boundaries for `GT_esmini` after the feature-oriented refactor.

## 1. Module Topology

- `core`: Public C API facade (`GT_Init`, `GT_Step`, `GT_Close`, `GT_ReportObjectVel`), config path resolution, shared abstractions.
- `scenario`: OpenSCENARIO extension parsing and runtime entities (`GT_ScenarioReader`, `ExtraAction`, `ExtraEntities`, `GT_TrafficSignalController`).
- `io`: UDP transport and packet-level I/O (`GT_UDP`).
- `control`: Real-time control pipeline (`ControllerManualDrive`, `RealVehicle`, `TerrainTracker`, `AutoLightController`) and split responsibilities (`ManualDriveCoordinator`, `IndicatorFSM`, `IInputSource`, `IPhysicsBackend`).
- `osi`: OSI/HostVehicleData reporting (`GT_OSIReporter*`, `GT_HostVehicleReporter`) and provider interfaces.

## 2. Dependency Rules

- `io -> core`
- `scenario -> core`
- `control -> core, io, scenario`
- `osi -> core, scenario`
- Public output remains `GT_esminiLib` and `GT_esminiLib_static`.

## 3. Include Rules

- Public includes must use: `#include <gt_esmini/...>`
- Internal includes must use: `#include "gt_esmini/..."`

## 4. Configuration

- Runtime config files are under `GT_esmini/config/`.
- `ConfigLoader` resolves to `exe_dir/config/`:
  - `config/real_vehicle_params.json` — Vehicle physics parameters
  - `config/host_vehicle_config.json` — HVD reporting config
  - `config/manual_drive.json` — ManualDrive controller settings (input type, button mapping, FFB, domain control)

## 5. Directory Anchors

- Headers: `GT_esmini/include/gt_esmini/{core,scenario,io,control,osi}`
- Sources: `GT_esmini/src/{core,scenario,io,control,osi}`
- Legacy experiments: `GT_esmini/archive/`

## 6. ManualDrive Controller

Renamed from `ControllerRacingWheel` → `ControllerManualDrive` (v0.8).

Key capabilities:
- **Input**: Pluggable sources (`SDL2WheelInput`, `NetworkInputBridge`, `StubInputSource`)
- **Physics**: Pluggable backends (`RealVehicleBackend`, `NetworkPhysicsBridge`)
- **FFB**: Spring, damper, Coulomb friction model (G29-compatible)
- **Button mapping**: Fully configurable via `manual_drive.json`
- **Domain control**: Independent lateral/longitudinal domain assignment (manual/scenario)
- **Indicator FSM**: Auto-cancel with steering return detection
- **Light control**: Headlight, high beam, fog, hazard toggle via buttons
- **HVD output**: `GetInputsForOSI()`, `GetPowertrainForOSI()`, `GetADASStates()`

## 7. Web Stack

- **Backend**: FastAPI (`GT_esmini/web/backend/main.py`)
  - API layer: `api/*.py` (REST endpoints: projects, scenarios, simulations, roads, scripts, results, config, OSI stream)
  - Service layer: `services/*.py` (business logic, simulation runner)
  - DB: SQLite (`db/database.py`)
  - gRPC: OSI GroundTruth / HostVehicleData streaming (`services/grpc_server.py`, `services/osi_bridge.py`)
- **Frontend**: React + Vite + TypeScript (`GT_esmini/web/frontend/`)
  - Pages: Projects, ProjectDetail, Scenarios, NewSimulation, Simulations, SimulationDetail
  - Key components: ManualDrivePanel, HvdGaugePanel, OsiLivePanel, LiveSceneView, WindowControls
- **Electron**: Desktop shell (`GT_esmini/web/electron/`)
  - Custom titlebar (frameless window)
  - Auto-detects packaged vs. dev mode for server startup
  - Health check polling on startup
- **Package**: PyInstaller (`GT_esmini/web/pyinstaller/`)

## 8. TrafficSignalController

- Auto-cycling traffic signal phases
- Action-based and condition-based phase transitions
- OpenDRIVE controller reference integration

Key files: `src/scenario/GT_TrafficSignalController.*`

## 9. Development Freeze

Python-related features (PythonDriverController, Embedded Python, DriverScript) are **development-frozen** as of v0.8. Existing functionality remains available but no new features are planned.

Since the R2 decoupling (audit SUB-1), the embedded-Python stack is **opt-in**: `GT_ENABLE_EMBEDDED_PYTHON` defaults OFF (no Python3 dev headers needed; PythonDriverController excluded from the build). Distribution packages configure with `-DGT_ENABLE_EMBEDDED_PYTHON=ON` via `scripts/build_package.ps1`.

## 10. Documentation Policy

Human docs under `GT_esmini/docs/` remain supplementary. Architectural truth is this file + code.
