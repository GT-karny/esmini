# System Structure

GT_esminiの内部構造とデータフローを可視化します。

## 1. System Context Diagram (システム概要)

GT_esminiは`esmini`を拡張し、外部コントローラ(RealDriver)やOSI出力機能を提供します。

```mermaid
graph TD
    subgraph External[External Systems]
        PythonScript[Python Script / Driver Station]
        OSIConsumer[OSI Consumer / Visualizer]
    end

    subgraph GT_esmini_System[GT_esmini Ecosystem]
        esmini[esmini Core]
        GT_esmini[GT_esmini DLL]
    end

    PythonScript -- UDP (Control Input) --> GT_esmini
    GT_esmini -- OSI (GroundTruth) --> OSIConsumer
    
    esmini -- Load/Link --> GT_esmini
    GT_esmini -- Extensions/Hooks --> esmini
```

## 2. Class Diagram (クラス構成)

主要なクラスとそれらの関係性を示します。

```mermaid
classDiagram
    class GT_esminiLib {
        +GT_Init()
        +GT_Step()
        +GT_Close()
        +GT_ReportObjectVel()
    }

    class ControllerRealDriver {
        -RealVehicle real_vehicle_
        -GT_UDP_Sender* udpClient_
        -GT_UDP_Sender* waypointClient_
        -osi3::HostVehicleData cached_hvd_
        +Step(double timeStep)
        +GetTypeName()
    }

    class RealVehicle {
        -VehicleParams params_
        -double rpm_
        -double roll_
        +UpdatePhysics()
        +GetRPM()
        +GetTorqueOutput()
    }

    class GT_OSIReporter {
        +UpdateOSIGroundTruth()
        +UpdateOSIHostVehicleData()
    }

    class GT_UDP_Sender {
        +Send()
    }

    %% Relationships
    ControllerRealDriver *-- RealVehicle : Has-A
    ControllerRealDriver *-- GT_UDP_Sender : Uses
    ControllerRealDriver ..> GT_OSIReporter : Updates Data
    GT_esminiLib ..> GT_OSIReporter : Global Access

    %% Inheritance (Conceptual)
    namespace esmini_interfaces {
        class Controller
        class Vehicle
    }
    
    Controller <|-- ControllerRealDriver
    Vehicle <|-- RealVehicle
```

## 3. Sequence Diagram (処理フロー)

1フレームあたりの処理フロー（`GT_Step` および内部の `Controller` 処理）を示します。

```mermaid
sequenceDiagram
    participant esmini as esmini Core
    participant CTRL as ControllerRealDriver
    participant VEH as RealVehicle
    participant REP as GT_OSIReporter
    participant UDP as UDP Socket

    Note over esmini: Simulation Loop Start

    esmini->>CTRL: Step(timeStep)
    activate CTRL
    
    CTRL->>UDP: Receive Input (Throttle, Brake, Steer)
    UDP-->>CTRL: DriverInput
    
    CTRL->>VEH: UpdatePhysics(dt, input...)
    activate VEH
    VEH->>VEH: Calculate RPM, Torque, Position
    VEH-->>CTRL: Updated State
    deactivate VEH
    
    CTRL->>CTRL: Update OSI HostVehicleData
    
    CTRL->>REP: GT_ReportObjectVel (Sync Speed)
    
    deactivate CTRL

    esmini->>esmini: Update World/Traffic

    esmini->>REP: UpdateOSIGroundTruth()
    activate REP
    REP->>REP: Collect Object States
    REP->>REP: Serialize & Send OSI
    deactivate REP

    Note over esmini: Simulation Loop End
```
