# Component Knowledge Graph: GT_esmini

This document defines the architectural boundaries for `GT_esmini` after the feature-oriented refactor.

## 1. Module Topology

- `core`: Public C API facade (`GT_Init`, `GT_Step`, `GT_Close`, `GT_ReportObjectVel`), config path resolution, shared abstractions.
- `scenario`: OpenSCENARIO extension parsing and runtime entities (`GT_ScenarioReader`, `ExtraAction`, `ExtraEntities`).
- `io`: UDP transport and packet-level I/O (`GT_UDP`).
- `control`: Real-time control pipeline (`ControllerRealDriver`, `RealVehicle`, `TerrainTracker`, `AutoLightController`) and split responsibilities (`DriverInputReceiver`, `VehicleStateUpdater`, `EsminiStateApplier`, `ControlDecisionEngine`).
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
- `ConfigLoader` resolves:
  - `config/real_vehicle_params.json`
  - `config/host_vehicle_config.json`

## 5. Directory Anchors

- Headers: `GT_esmini/include/gt_esmini/{core,scenario,io,control,osi}`
- Sources: `GT_esmini/src/{core,scenario,io,control,osi}`
- Legacy experiments: `GT_esmini/archive/`

## 6. Web Stack

- **Backend**: FastAPI (`GT_esmini/web/backend/main.py`)
  - API layer: `api/*.py` (REST endpoints: projects, scenarios, simulations, roads, scripts, results, config, OSI stream)
  - Service layer: `services/*.py` (business logic, simulation runner)
  - DB: SQLite (`db/database.py`)
  - gRPC: OSI GroundTruth / HostVehicleData streaming (`services/grpc_server.py`, `services/osi_bridge.py`)
- **Frontend**: React + Vite + TypeScript (`GT_esmini/web/frontend/`)
- **Package**: PyInstaller (`GT_esmini/web/pyinstaller/`)

## 7. TrafficSignalController

Recently added feature supporting:
- Auto-cycling traffic signal phases
- Action-based and condition-based phase transitions
- OpenDRIVE controller reference integration

Key files: `src/scenario/GT_TrafficSignalController.*`

## 8. Documentation Policy

Human docs under `GT_esmini/docs/` remain supplementary. Architectural truth is this file + code.
