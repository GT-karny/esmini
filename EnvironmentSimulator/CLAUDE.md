# Component Knowledge Graph: EnvironmentSimulator (Core)

This document defines the structural relationships and contents of the `EnvironmentSimulator` (vanilla esmini) directory.
**NOTE**: This directory contains the **UPSTREAM CORE**. Modifications here should be avoided to maintain clean separation from `GT_esmini`.

## 1. Modules (Source Logic)

The core logic is divided into functional modules.

### `Modules/ScenarioEngine`
- **Role**: Core Engine
- **Responsibility**: Manages the OpenSCENARIO state machine, entity lifecycle, and event processing.
- **Key Components**: `ScenarioEngine`, `Entities`, `Storyboard`.

### `Modules/RoadManager`
- **Role**: Environment / Map Provider
- **Responsibility**: Parses OpenDRIVE (`.xodr`) files and provides road geometry queries (position, heading, curvature).
- **Key Components**: `RoadManager`, `OpenDrive`.

### `Modules/Controllers`
- **Role**: Vehicle Control
- **Responsibility**: Implements lateral/longitudinal controllers for scenario objects.
- **Key Components**: `Controller`, `InteractiveController`, `ExternalController`.

### `Modules/PlayerBase`
- **Role**: Simulation Runner
- **Responsibility**: Bridge between the engine and the application layer. Manages time stepping and loop execution.
- **Key Components**: `ScenarioPlayer`, `PlayerServer`.

### `Modules/ViewerBase`
- **Role**: Visualization
- **Responsibility**: OpenSceneGraph (OSG) integration for 3D rendering.
- **Key Components**: `Viewer`, `Camera`.

### `Modules/CommonMini`
- **Role**: Utilities
- **Responsibility**: Shared helpers, logging (`Logger`), configuration (`Options`), networking (`UDP`).

## 2. Libraries (API Export)

Interfaces exposed to external applications.

- **`Libraries/esminiLib`**: The primary C-API DLL (`esminiLib.dll`) used by most wrappers (including `GT_esmini`).
- **`Libraries/esminiRMLib`**: RoadManager standalone library.
- **`Libraries/esminiROS`**: ROS1/ROS2 integration nodes (experimental).
- **`Libraries/esminiJS`**: Emscripten/WASM bindings for web.

## 3. Applications (Executables)

Entry points and standalone tools.

- **`Applications/esmini`**: The standard esmini executable (CLI tool).
- **`Applications/esmini-dyn`**: Version of esmini linked dynamically against `esminiLib`.
- **`Applications/odrviewer`**: Tool to visualize OpenDRIVE maps without scenarios.
- **`Applications/replayer`**: Tool to replay recording files (`.dat`, `.csv`).
- **`Applications/odrplot`**: Plotting utility for road features.

## 4. Quality Assurance

- **`Unittest/`**: C++ Unit tests (GoogleTest based). output: `run_tests`.
  - **`xodr/`**: Test assets (maps).
  - **`xosc/`**: Test assets (scenarios).
- **`code-examples/`**: Minimal examples demonstrating API usage (`hello_world`, `osi-groundtruth`, etc.).

## 5. Dependency Flow

```mermaid
graph TD
    A[Applications] --> B[Libraries]
    B --> C[Modules]
    C --> D[External Deps (OSG, OSI, etc.)]
    
    subgraph Modules
        ScenarioEngine --> RoadManager
        ScenarioEngine --> Controllers
        PlayerBase --> ScenarioEngine
        PlayerBase --> ViewerBase
    end
```
