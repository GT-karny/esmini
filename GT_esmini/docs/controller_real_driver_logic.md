# ControllerRealDriver Logic (Mermaid)

Target files:
- `GT_esmini/include/gt_esmini/control/ControllerRealDriver.hpp`
- `GT_esmini/src/control/ControllerRealDriver.cpp`

## 1. Component Structure

```mermaid
classDiagram
    class ControllerRealDriver {
      +Step(timeStep)
      +Activate(mode)
      +GetInputsForOSI(...)
      +GetPowertrainForOSI(...)
      +GetADASStates(...)
      +GetCachedHostVehicleData()
      -GetRunningActionState()
      -HandlePathActions(...)
      -ReceiveLatestUdpInput()
      -ParseDriverInputPacket(packetSize)
      -UpdateVehiclePhysics(timeStep)
      -SendTargetSpeedPacket()
      -MaybeSendWaypoints()
      -UpdateCachedPowertrain()
      -UpdateHostVehicleReporter()
      -SyncGatewayObjectState(...)
      -SyncObjectPoseFromRealVehicle()
      -UpdateVehicleLights()
      -ExtractWaypoints()
      -SendWaypointsUDP()
      -RegenerateWaypointsForLaneChange(...)
      -RegenerateWaypointsForLaneOffset(...)
      -RegenerateWaypointsForTrajectory(...)
    }

    class DriverInputReceiver
    class VehicleStateUpdater
    class EsminiStateApplier
    class ControlDecisionEngine
    class RealVehicle
    class UDPServer
    class GT_UDP_Sender
    class HostVehicleData

    ControllerRealDriver --> DriverInputReceiver : Receive()
    ControllerRealDriver --> VehicleStateUpdater : UpdatePhysics()
    ControllerRealDriver --> EsminiStateApplier : Apply()
    ControllerRealDriver --> ControlDecisionEngine : UpdateSetSpeed()
    ControllerRealDriver --> RealVehicle : owns
    ControllerRealDriver --> UDPServer : receive driver UDP
    ControllerRealDriver --> GT_UDP_Sender : send target speed / waypoints
    ControllerRealDriver --> HostVehicleData : cached_hvd_
```

## 2. Activation Flow

```mermaid
flowchart TD
    A[Activate] --> B{object exists}
    B -- No --> Z[Return base activate]
    B -- Yes --> C[Configure UDP server with fixed base port]
    C --> D[Ensure vehicle light extension]
    D --> E[Create UDP client for target speed]
    E --> F{send waypoints enabled}
    F -- Yes --> G[Create waypointClient_]
    F -- No --> H[Skip]
    G --> I[Initialize real vehicle from object state]
    H --> I
    I --> J[Initialize set speed and current speed]
    J --> K[Remember last observed route]
    K --> L[Load real vehicle params json]
    L --> Z
```

## 3. Main Step Loop

```mermaid
sequenceDiagram
    participant C as ControllerRealDriver
    participant DE as ControlDecisionEngine
    participant DR as DriverInputReceiver
    participant VS as VehicleStateUpdater
    participant EA as EsminiStateApplier
    participant GW as ScenarioGateway/Object

    C->>C: preStepState = GetRunningActionState()
    C->>DE: UpdateSetSpeed(*this)
    C->>DR: Receive(*this)
    C->>VS: UpdatePhysics(*this, timeStep)
    C->>C: SendTargetSpeedPacket()
    C->>C: MaybeSendWaypoints()
    C->>C: UpdateCachedPowertrain()
    C->>C: UpdateHostVehicleReporter()
    C->>C: real_vehicle_.GetCombinedAttitude()
    alt object_ && gateway_
        C->>C: HandlePathActions(preStepState, previousFlags, "")
        C->>EA: Apply(*this, pitch, roll, hasRunningScenarioLongAction)
        C->>C: UpdateVehicleLights()
    end
    C->>C: Update was* flags from preStepState
    C->>C: Controller::Step(timeStep)
    C->>C: RefreshWaypointsOnRoutePointerChange()
    C->>C: postStepState = GetRunningActionState()
    C->>C: postStarted = HandlePathActions(postStepState, preControllerStepFlags, "Post-step ")
    alt postStarted && object_
        C->>GW: SyncObjectPoseFromRealVehicle()
    end
    C->>C: Update was* flags from postStepState
```

## 4. Path Action Priority (`HandlePathActions`)

```mermaid
flowchart TD
    S[Start] --> A{new follow trajectory}
    A -- Yes --> A1[RegenerateWaypointsForTrajectory]
    A1 --> A2[End action]
    A2 --> R[return true]
    A -- No --> B{new lane change with target}
    B -- Yes --> B1[RegenerateWaypointsForLaneChange]
    B1 --> B2[End action]
    B2 --> R
    B -- No --> C{new lane offset}
    C -- Yes --> C1[ResolveLaneOffsetTarget]
    C1 --> C2[Compute transition distance]
    C2 --> C3[RegenerateWaypointsForLaneOffset]
    C3 --> C4[End action]
    C4 --> R
    C -- No --> D{new assign route}
    D -- Yes --> D1[ExtractWaypoints]
    D1 --> D2[mark waypoints extracted true]
    D2 --> D3[End action]
    D3 --> R
    D -- No --> E[return false]
```

## 5. UDP Receive/Parse Pipeline

```mermaid
flowchart TD
    A[Receive latest UDP input] --> B{udp server exists}
    B -- No --> Z[Return]
    B -- Yes --> C[Loop receive from udpServer]
    C --> D{received greater than zero}
    D -- No --> Z
    D -- Yes --> E[Parse driver input packet]
    E --> F{packet size valid and protobuf ok}
    F -- No --> C
    F -- Yes --> G[Read lightMask from first 4 bytes]
    G --> H[Parse cached host vehicle data]
    H --> I[Extract throttle/brake/steering/gear]
    I --> J[engineBrake fixed to 0.49]
    J --> K[Map ADAS names to state array]
    K --> C
```

## 6. Waypoint Send/Generation Logic

```mermaid
flowchart TD
    A[Maybe send waypoints] --> B{send waypoints}
    B -- No --> Z[Return]
    B -- Yes --> C{waypoints already extracted}
    C -- No --> D[Extract waypoints and mark extracted]
    C -- Yes --> E[SendWaypointsUDP]
    D --> E
    E --> F{waypoint client exists and waypoints not empty?}
    F -- No --> Z
    F -- Yes --> G[Update current waypoint index from real vehicle pose]
    G --> H[Build packet type=2 + index + count + waypoint array]
    H --> I[Send via waypointClient_]
```

```mermaid
flowchart TD
    A[Extract/Regenerate waypoints] --> B{Source type}
    B -->|Assigned Route| C[Convert route waypoints to waypoint data]
    B -->|No Route| D[Fallback road following in 5m steps]
    B -->|LaneChange action| E[SmootherStep interpolation between lanes]
    B -->|LaneOffset action| F[SmootherStep interpolation of lane offset]
    B -->|FollowTrajectory action| G[Sample trajectory by s]
    C --> H[waypoints ready]
    D --> H
    E --> H
    F --> H
    G --> H
```

## 7. Longitudinal Action Handling Summary

```mermaid
flowchart TD
    A[Get target speed from actions] --> B{long speed running}
    B -- Yes --> C{absolute or relative}
    C --> C1[Set target speed]
    B -- No --> D[Keep set speed]
    C1 --> E{profile distance or synchronize running}
    D --> E
    E -- Yes --> F[targetSpeed equals current object speed]
    E -- No --> G[Keep previous target speed]
    F --> H[has running action true]
    G --> I[running action true only for long speed]
```

## 8. DriverScript Integration

`ControllerRealDriver` と DriverScript は、UDPで双方向に接続されます。

- C++受信: `DriverScript/realdriver/client.py` (`RealDriverClient.send_update`)
- C++送信(目標速度): `DriverScript/realdriver/udp_receivers.py` (`TargetSpeedReceiver`)
- C++送信(ウェイポイント): `DriverScript/realdriver/udp_receivers.py` (`WaypointReceiver`)
- Python統合制御: `DriverScript/realdriver/scenario_drive.py` (`ScenarioDriveController`)

```mermaid
flowchart LR
    subgraph PY[DriverScript Python]
        RC[RealDriverClient]
        SR[ScenarioDriveController]
        TSR[TargetSpeedReceiver]
        WPR[WaypointReceiver]
    end

    subgraph CPP[GT_esmini C++]
        CRD[ControllerRealDriver]
        SG[Scenario/Gateway]
    end

    RC -- "UDP BasePort (default 53995)\n[lightMask:int32 + HostVehicleData protobuf]" --> CRD
    CRD -- "UDP ClientPort (default 54995)\n[type=1 + targetSpeed:double]" --> TSR
    CRD -- "UDP WaypointPort (default 54996)\n[type=2 + currentIndex + waypoint array]" --> WPR
    TSR --> SR
    WPR --> SR
    SR -- "throttle/brake/steering/gear via RealDriverClient" --> RC
    CRD --> SG
```

## 9. End-to-End Runtime Loop (with DriverScript)

```mermaid
sequenceDiagram
    participant PY as DriverScript Control Loop
    participant RC as RealDriverClient
    participant CRD as ControllerRealDriver
    participant SR as ScenarioDriveController
    participant TS as TargetSpeedReceiver
    participant WP as WaypointReceiver

    loop each frame
        PY->>SR: update(ground_truth, dt)
        SR->>TS: receive_all()
        TS-->>SR: latest target speed (optional)
        SR->>WP: receive()
        WP-->>SR: currentIndex + waypoints (optional)
        SR-->>PY: steering/throttle/brake
        PY->>RC: set_controls(...) / set_gear(...)
        PY->>RC: send_update()
        RC->>CRD: [lightMask + HostVehicleData]
        CRD->>CRD: ParseDriverInputPacket + UpdateVehiclePhysics
        CRD->>TS: SendTargetSpeedPacket(type=1)
        CRD->>WP: SendWaypointsUDP(type=2, if enabled)
    end
```

## 10. Packet Compatibility Notes

- RealDriver input packet (Python -> C++):
  - `int32 lightMask` + `HostVehicleData protobuf bytes`
  - 実装: `DriverScript/realdriver/client.py`, `GT_esmini/src/control/ControllerRealDriver.cpp`
- TargetSpeed packet (C++ -> Python):
  - `uint8 type=1` + `double targetSpeed` (9 bytes)
  - 実装: `GT_esmini/src/control/ControllerRealDriver.cpp`, `DriverScript/realdriver/udp_receivers.py`
- Waypoint packet (C++ -> Python):
  - `uint8 type=2` + `uint32 currentIndex` + `uint32 count` + waypoint struct配列
  - Python側は `DriverScript/realdriver/waypoint.py` で C++構造体アライメントを考慮して `56 bytes/waypoint` として解析

注意:
- 現在の `ControllerRealDriver` は固定ポート運用（`BasePort` をそのまま使用）で、`object_id` 加算はしていません。
