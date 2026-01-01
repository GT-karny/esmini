import ctypes
import os
import sys
import time

# Adjust paths
bin_path = os.path.abspath("../bin")
# Add bin to DLL search path for dependencies
os.add_dll_directory(bin_path)

esimini_dll_path = os.path.join(bin_path, "esminiLib.dll")
gt_dll_path = os.path.join(bin_path, "GT_esminiLib.dll")

# Load Libraries
try:
    esmini_lib = ctypes.CDLL(esimini_dll_path)
    gt_lib = ctypes.CDLL(gt_dll_path)
except OSError as e:
    print(f"Error loading DLLs: {e}")
    sys.exit(1)

# Define Argument Types
gt_lib.GT_InitWithArgs.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_char_p)]
gt_lib.GT_InitWithArgs.restype = ctypes.c_int

gt_lib.GT_Step.argtypes = [ctypes.c_double]
gt_lib.GT_Step.restype = None

gt_lib.GT_GetLightState.argtypes = [ctypes.c_int, ctypes.c_int]
gt_lib.GT_GetLightState.restype = ctypes.c_int

gt_lib.GT_EnableAutoLight.argtypes = []
gt_lib.GT_EnableAutoLight.restype = None

# SE_StepDT from esminiLib
esmini_lib.SE_StepDT.argtypes = [ctypes.c_double]
esmini_lib.SE_StepDT.restype = ctypes.c_int

# SE_GetSimulationTime
esmini_lib.SE_GetSimulationTime.argtypes = []
esmini_lib.SE_GetSimulationTime.restype = ctypes.c_double

# Arguments
xosc_path = "../resources/xosc/LaneChangeTest.xosc"
# Make sure paths are correct relative to execution dir (scripts/)
# esmini expects paths relative to its execution or absolute?
# Let's use absolute paths for safety.
res_path = os.path.abspath("../resources")
xosc_abs_path = os.path.abspath(xosc_path)

args = [b"GT_Sim", b"--window", b"0", b"0", b"100", b"100", b"--res_path", res_path.encode(), xosc_abs_path.encode()]
argc = len(args)
argv = (ctypes.c_char_p * argc)(*args)

# Initialize
print("Initializing GT_esmini...")
ret = gt_lib.GT_InitWithArgs(argc, argv)
if ret != 0:
    print("Initialization failed.")
    sys.exit(1)

# Enable AutoLight
gt_lib.GT_EnableAutoLight()

# Loop
dt = 0.05
sim_time = 0.0
max_time = 15.0

print("Starting simulation loop...")
print("Time, Left(8), Right(9)")

# ID 0 is usually Ego if defined first.
ego_id = 0 

# State tracking for clean logs
last_l = -1
last_r = -1

while sim_time < max_time:
    # Step
    esmini_lib.SE_StepDT(dt)
    gt_lib.GT_Step(dt)
    
    sim_time = esmini_lib.SE_GetSimulationTime()
    
    # Get State
    left_state = gt_lib.GT_GetLightState(ego_id, 8) # 8 = IndicatorLeft
    right_state = gt_lib.GT_GetLightState(ego_id, 9) # 9 = IndicatorRight
    
    # Simplified State: 0=Off, 1=On, 2=Flash
    l_str = "OFF" if left_state == 0 else ("ON" if left_state == 1 else "FLASH")
    r_str = "OFF" if right_state == 0 else ("ON" if right_state == 1 else "FLASH")
    
    if left_state != last_l or right_state != last_r:
        print(f"{sim_time:.2f}, {l_str}, {r_str}")
        last_l = left_state
        last_r = right_state
    
    # Also print periodic heartbeats
    if int(sim_time / 1.0) > int((sim_time - dt) / 1.0):
       pass # print(f"{sim_time:.2f} ...")

print("Done.")
