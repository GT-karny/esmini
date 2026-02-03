import ctypes
import os
import sys

# Load DLL
dll_path = os.path.abspath(os.path.join(os.path.dirname(__file__), "../build/GT_esmini/Release/GT_esminiLib.dll"))
if not os.path.exists(dll_path):
    dll_path = os.path.abspath(os.path.join(os.path.dirname(__file__), "../build/GT_esmini/GT_esminiLib.dll"))
    if not os.path.exists(dll_path):
        print(f"Error: DLL not found at {dll_path}")
        sys.exit(1)

print(f"Loading DLL: {dll_path}")
lib = ctypes.CDLL(dll_path)

# Define structs
class GT_RM_RoadSignalInfo(ctypes.Structure):
    _fields_ = [
        ("id", ctypes.c_int),
        ("s", ctypes.c_double),
        ("t", ctypes.c_double),
        ("x", ctypes.c_double),
        ("y", ctypes.c_double),
        ("z", ctypes.c_double),
        ("h", ctypes.c_double),
        ("p", ctypes.c_double),
        ("r", ctypes.c_double),
        ("type", ctypes.c_char * 64),
        ("subtype", ctypes.c_char * 64),
        ("country", ctypes.c_char * 64),
        ("value", ctypes.c_double),
        ("unit", ctypes.c_char * 64),
        ("text", ctypes.c_char * 128),
        ("isDynamic", ctypes.c_bool),
        ("height", ctypes.c_double),
        ("width", ctypes.c_double)
    ]

# Define function prototypes
lib.GT_RM_Init.argtypes = [ctypes.c_char_p]
lib.GT_RM_Init.restype = ctypes.c_int

lib.GT_RM_GetNumRoads.argtypes = []
lib.GT_RM_GetNumRoads.restype = ctypes.c_int

lib.GT_RM_GetRoadIdByIndex.argtypes = [ctypes.c_int]
lib.GT_RM_GetRoadIdByIndex.restype = ctypes.c_uint32

lib.GT_RM_GetRoadSignalCount.argtypes = [ctypes.c_uint32]
lib.GT_RM_GetRoadSignalCount.restype = ctypes.c_int

lib.GT_RM_GetRoadSignal.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.POINTER(GT_RM_RoadSignalInfo)]
lib.GT_RM_GetRoadSignal.restype = ctypes.c_int

lib.GT_RM_Close.argtypes = []
lib.GT_RM_Close.restype = None

# Initialize
xodr_path = os.path.abspath(os.path.join(os.path.dirname(__file__), "../resources/xodr/fabriksgatan_traffic_lights.xodr"))
print(f"Loading ODR: {xodr_path}")
ret = lib.GT_RM_Init(xodr_path.encode('utf-8'))
if ret != 0:
    print("Failed to init GT_RM")
    sys.exit(1)

# Iterate roads and signals
num_roads = lib.GT_RM_GetNumRoads()
print(f"Number of roads: {num_roads}")

for i in range(num_roads):
    road_id = lib.GT_RM_GetRoadIdByIndex(i)
    signal_count = lib.GT_RM_GetRoadSignalCount(road_id)
    
    if signal_count > 0:
        print(f"Road ID: {road_id}, Signals: {signal_count}")
        for j in range(signal_count):
            info = GT_RM_RoadSignalInfo()
            ret = lib.GT_RM_GetRoadSignal(road_id, j, ctypes.byref(info))
            if ret == 0:
                print(f"  Signal {j}: ID={info.id}, Type={info.type.decode('utf-8')}, Subtype={info.subtype.decode('utf-8')}")
                print(f"    Pos=(x={info.x:.2f}, y={info.y:.2f}, z={info.z:.2f}) Rot=(h={info.h:.2f}, p={info.p:.2f}, r={info.r:.2f})")
                print(f"    S={info.s:.2f}, T={info.t:.2f}, Value={info.value}, Unit={info.unit.decode('utf-8')}, Text={info.text.decode('utf-8')}")
                print(f"    Dynamic={info.isDynamic}, H={info.height:.2f}, W={info.width:.2f}")

lib.GT_RM_Close()
print("Done")
