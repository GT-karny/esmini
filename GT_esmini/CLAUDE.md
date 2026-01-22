# Component Knowledge Graph: GT_esmini

This document defines the structural relationships and responsibilities of the `GT_esmini` module components for AI context analysis.

## 1. System Interfaces

### `GT_esminiLib`
- **File**: `GT_esminiLib.{hpp,cpp}`
- **Type**: API Facade / Entry Point
- **Responsibility**: 
  - Standardizes the initialization and simulation loop for the extended simulator.
  - Manages the lifecycle of global singletons (`AutoLightController`, `TerrainTracker`).
- **Key Symbols**:
  - `GT_Init()`: Initializes the `GT_ScenarioReader` factory.
  - `GT_Step()`: Drives the update loops for `AutoLightController` and `TerrainTracker`.
  - `GT_ReportObjectVel()`: Backdoor to inject velocity data into `esmini` objects for OSI reporting.

## 2. Simulation Logic & Physics

### `ControllerRealDriver`
- **File**: `ControllerRealDriver.{hpp,cpp}`
- **Type**: `scenarioengine::Controller` Implementation
- **Registry Name**: `"RealDriverController"`
- **Input Source**: UDP Socket (Port: `53995`)
- **Data Protocol**: Binary Struct `UDPPacket` (Size-dependent versioning).
- **Internal Model**: Owns an instance of `RealVehicle`.
- **Logic Flow**: 
  1. Recv UDP packet.
  2. Map Packet inputs (Throttle, Brake, Steer) -> `RealVehicle::UpdatePhysics`.
  3. Apply `RealVehicle` state (Pos, Rot) -> `esmini::Object`.

### `RealVehicle`
- **File**: `RealVehicle.{hpp,cpp}`
- **Type**: Physics Engine / Dynamics Model
- **Inheritance**: `vehicle::Vehicle` (esmini base)
- **Configuration**: `real_vehicle_params.json`
- **Simulation Features**:
  - **Suspension**: Mass-spring-damper model for Pitch/Roll dynamics.
  - **Powertrain**: Lookup table-based RPM/Torque generation.
  - **Terrain Integration**: Composes dynamic attitude with terrain normal vectors.

### `TerrainTracker`
- **File**: `TerrainTracker.{hpp,cpp}`
- **Type**: Utility / Physics Subsystem
- **Responsibility**: Raycast/Sampling of OpenDRIVE road geometry to determine surface orientation.
- **Output**: Writes terrain pitch/roll offsets directly to `RealVehicle` instances.

## 3. Automation & Logic

### `AutoLightController`
- **File**: `AutoLightController.{hpp,cpp}`
- **Type**: Logic Controller
- **Target**: `VehicleLightExtension` (Composition)
- **Heuristics**:
  - **Brake**: Dynamic thresholding (`< -1.2 m/s²`) with time-latch (`0.7s`).
  - **Turn Signals**: 
    - Lateral velocity threshold (`> 0.25 m/s`).
    - OpenDRIVE Junction anticipation (`35m` lookahead).
  - **Reverse**: Gear state monitoring.

### `GT_ScenarioReader`
- **File**: `GT_ScenarioReader.{hpp,cpp}`
- **Type**: `scenarioengine::ScenarioReader` Extension
- **Responsibility**: XML Parsing Hook.
- **Extensions**:
  - **PrivateAction**: Intercepts `PrivateAction` to find `AppearanceAction`.
  - **LightStateAction**: Implements parsing for ASAM OpenSCENARIO 1.2 `LightStateAction`.

### `ExtraAction` / `ExtraEntities`
- **File**: `ExtraAction.{hpp,cpp}`, `ExtraEntities.{hpp,cpp}`
- **Type**: Data Structures & State Containers
- **Core Pattern**: Composition over Inheritance for Vehicle state.
- **Data**:
  - `VehicleLightExtension`: Stores `std::map<VehicleLightType, LightState>`.
  - `OSCLightStateAction`: Runtime executable action for light transitions.

## 4. Output Generation

### `GT_OSIReporter`
- **File**: `GT_OSIReporter.cpp`
- **Type**: Data Serializer / UDP Transmitter
- **Protocol**: ASAM OSI (Open Simulation Interface) over UDP.
- **Extended Fields**:
  - **HostVehicleData**: Injects `Throttle`, `Brake`, `Steering`, `Gear` into `osi3::SensorView`.
  - **Velocity**: Manual override via `GT_ReportObjectVel`.
- **Architecture**:
  - Modifies `OSIReporter` behavior via hooks/extensions rather than direct inheritance (due to static linking limitations).

## 5. Execution Environment

### `GT_Sim`
- **File**: `GT_Sim/main.cpp`
- **Type**: Standalone Executable (Launcher)
- **Responsibility**: 
  - Provides a lightweight runtime environment for verifying `GT_esmini` functionality without full FMU integration.
  - Implements a precise real-time pacing loop (`std::chrono` based).
- **Features**:
  - **AutoLight**: Enable via `--autolight` flag.
  - **OSI Output**: Enable via `--osi <ip>` flag.
  - **Frequency Control**: Set update rate via `--hz <freq>`.
  - **Standard esmini Flags**: Supports all native esmini arguments. These are stripped of GT extensions and forwarded to `SE_InitWithArgs`.
    - **Common Flags**:
      - `--osc <file>`: Specify OpenSCENARIO file.
      - `--window <x> <y> <w> <h>`: Set window position and size.
      - `--headless`: Run without visualization (off-screen).
      - `--fixed_timestep <dt>`: Force fixed execution step size.
      - `--threads`: Run viewer in separate thread.
      - `--record <file>`: Record simulation to file.
      - `--seed <int>`: Set random seed.
      - `--csv_logger`: Enable CSV logging.
      - `--disable_log`: Disable log file generation.
      - `--path <dir>`: Add search path for resources.
    - **Visualization Flags**:
      - `--camera_mode <int>`: Set initial camera mode.
      - `--custom_camera ...`: Add custom camera.
      - `--follow_object <id>`: Camera follows object ID.
      - `--wireframe`: Render in wireframe mode.

## 6. Documentation Policy

### **Human-Only Documentation**
The following resources are intended strictly for human consumption and should **NOT** be used as a source of truth for coding or architecture analysis by AI agents. Code structure and `CLAUDE.md` components take precedence.

- **`GT_esmini/README.md`**: Simplified overview for users.
- **`GT_esmini/docs/`**: User guides and manuals.
