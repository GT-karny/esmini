# Build Modifications Traceability: OSIReporter Replacement

## Overview
This document tracks the modification of the `esmini` build system to support the "Build-Time Component Swap" strategy for Phase 4 (OSI Light Extension).
To avoid modifying the core `OSIReporter.cpp` file directly, we replace it with a custom version `GT_OSIReporter.cpp` during the build process of the `ScenarioEngine` module.

## Rationale
- **Constraint:** Direct modification of core `esmini` files is prohibited to maintain upgradability and separation of concerns.
- **Solution:** By instructing CMake to compile `GT_OSIReporter.cpp` *instead of* `OSIReporter.cpp`, we can inject custom logic (light state reporting) while keeping the original file intact. The class name `OSIReporter` is preserved to maintain compatibility with `OSIReporter.hpp` and other dependent modules.

## Modifications

### 1. New File Creation
- **Source:** `esmini/EnvironmentSimulator/Modules/ScenarioEngine/SourceFiles/OSIReporter.cpp`
- **Destination:** `esmini/GT_esmini/GT_OSIReporter.cpp`
- **Action:** A copy of the original file was created. Custom logic for injecting light states was added to `UpdateOSIMovingObject`.

### 2. CMakeLists.txt Modification
- **Target File:** `esmini/EnvironmentSimulator/Modules/ScenarioEngine/CMakeLists.txt`
- **Action:**
    1.  Removed `SourceFiles/OSIReporter.cpp` from the `SRC_SOURCEFILES` list.
    2.  Added `../../GT_esmini/GT_OSIReporter.cpp` (relative path) to the `SRC_SOURCEFILES` list.
    3.  Added `../../GT_esmini` to `target_include_directories` to allow access to `VehicleExtensionManager.hpp`.

## Verification
- **Build Success:** The `GT_esmini` project (specifically `ScenarioEngine` and `esminiLib`) must compile without errors.
- **Functionality:** Running specific scenarios (e.g., `cut_in_simple.xosc` with `auto-light`) and inspecting OSI output should show populated `LightState` fields in `MovingObject` messages.
- **Regression:** Core OSI fields (position, speed) must remain correct.

## Rollback Plan
To revert these changes:
1.  Revert edits to `esmini/EnvironmentSimulator/Modules/ScenarioEngine/CMakeLists.txt`.
2.  Delete `esmini/GT_esmini/GT_OSIReporter.cpp` (optional, as it won't be compiled).
