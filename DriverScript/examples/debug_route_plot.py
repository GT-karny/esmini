import os
import sys
import csv
import math
import argparse
import matplotlib.pyplot as plt
import numpy as np

# Add parent directory to path to import modules
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from realdriver.rm_lib import EsminiRMLib
from realdriver.gt_rm_lib import GTEsminiRMLib
from realdriver.simplified_router import SimplifiedRouter
from realdriver.waypoint import Waypoint

def main():
    parser = argparse.ArgumentParser(description="Debug Route Plotter")
    parser.add_argument("--xodr_path", required=True, help="Path to OpenDRIVE map")
    parser.add_argument("--lib_path", default=r"E:\Repository\GT_esmini\esmini\DriverScript\bin\esminiRMLib.dll", help="Path to esminiRMLib.dll")
    parser.add_argument("--gt_lib_path", default=r"E:\Repository\GT_esmini\esmini\DriverScript\bin\GT_esminiLib.dll", help="Path to GT_esminiRMLib.dll")
    args = parser.parse_args()

    print(f"Loading map: {args.xodr_path}")
    
    # Initialize Libs
    rm = EsminiRMLib(args.lib_path)
    if rm.Init(args.xodr_path) < 0:
        print("Failed to init EsminiRMLib")
        return

    gt_rm = GTEsminiRMLib(args.gt_lib_path)
    if gt_rm.init(args.xodr_path) < 0:
        print("Failed to init GT_esminiRMLib")
        return

    # Initialize Router
    router = SimplifiedRouter(rm, gt_rm)

    # --- 1. Define Sparse Waypoints (From User Logs) ---
    # WP[0]: x=-83.97, y=-20.57, road=3, lane=-1
    # WP[1]: x=22.64, y=-3.64, road=13, lane=-1
    # WP[2]: x=6.94, y=107.71, road=2, lane=1  <-- Note: Target was Lane 1 in logs
    
    sparse_wps = [
        Waypoint(x=-83.97, y=-20.57, road_id=3, lane_id=-1, s=0.0), # s is approx
        Waypoint(x=22.64, y=-3.64, road_id=13, lane_id=-1, s=0.0),
        Waypoint(x=6.94, y=107.71, road_id=2, lane_id=1, s=0.0) 
    ]
    
    # Update S based on projection (to be precise)
    pos_handle = rm.CreatePosition()
    for wp in sparse_wps:
        rm.SetWorldXYHPosition(pos_handle, wp.x, wp.y, 0.0)
        res, data = rm.GetPositionData(pos_handle)
        if res == 0:
            wp.s = data.s
            if wp.road_id != 13: # Don't overwrite Road 13 (Projected to 12 otherwise)
                wp.road_id = data.roadId
            # KEEP original intention of lane for routing test
            print(f"  Fixed WP: Road={wp.road_id}, Lane={wp.lane_id}, S={wp.s:.2f}, X={wp.x:.2f}, Y={wp.y:.2f}")

    print("\nGenerating Dense Route...")
    # Use the first waypoint as current position
    dense_wps = router.calculate_route_from_waypoints(current_pos=sparse_wps[0], waypoints=sparse_wps, step_size=1.0)
    print(f"Generated {len(dense_wps)} dense waypoints.")

    # --- 3. Export CSV ---
    csv_filename = "dense_waypoints_debug.csv"
    with open(csv_filename, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(["Index", "X", "Y", "H", "RoadID", "LaneID", "S"])
        for i, wp in enumerate(dense_wps):
            writer.writerow([i, wp.x, wp.y, wp.h, wp.road_id, wp.lane_id, wp.s])
    print(f"Exported to {csv_filename}")

    # --- 4. Sample Road Geometry for Plotting ---
    # Roads to plot: R3(Start), R13(Expected Turn), R12(Mistaken Straight), R1(Mistaken), R6(Mistaken), R2(End)
    plot_road_ids = [3, 13, 2, 12, 1, 6, 7, 10, 11, 16] # Surrounding roads
    
    road_geoms = {} # {road_id: {type: 'ref'/'lane', x:[], y:[]}}

    print("\nSampling Road Geometry...")
    for rid in plot_road_ids:
        length = gt_rm.get_road_length(rid)
        if length <= 0: continue
        
        # Sample Reference Line (Lane 0)
        xs_ref, ys_ref = [], []
        # Sample Lane -1
        xs_rn, ys_rn = [], []
        # Sample Lane 1
        xs_lp, ys_lp = [], []

        step = 1.0
        for s in np.arange(0, length, step):
            # Ref
            rm.SetLanePosition(pos_handle, rid, 0, 0.0, float(s), True)
            res, p = rm.GetPositionData(pos_handle)
            if res==0:
                xs_ref.append(p.x)
                ys_ref.append(p.y)
            
            # Lane -1 (Right)
            rm.SetLanePosition(pos_handle, rid, -1, 0.0, float(s), True) 
            res, p = rm.GetPositionData(pos_handle)
            if res==0:
                xs_rn.append(p.x)
                ys_rn.append(p.y)

            # Lane 1 (Left)
            rm.SetLanePosition(pos_handle, rid, 1, 0.0, float(s), True)
            res, p = rm.GetPositionData(pos_handle)
            if res==0:
                xs_lp.append(p.x)
                ys_lp.append(p.y)
        
        road_geoms[rid] = {
            "ref": (xs_ref, ys_ref),
            "right": (xs_rn, ys_rn),
            "left": (xs_lp, ys_lp)
        }

    # --- 5. Plotting ---
    plt.figure(figsize=(12, 12))
    plt.title("Dense Route vs Road Network")
    plt.xlabel("X (m)")
    plt.ylabel("Y (m)")
    plt.grid(True)
    plt.axis('equal')

    # Plot Roads
    for rid, geom in road_geoms.items():
        # Plot Ref
        plt.plot(geom["ref"][0], geom["ref"][1], 'k--', linewidth=0.5, alpha=0.5, label='Ref Line' if rid==3 else "")
        # Plot Lane -1 (Blue)
        plt.plot(geom["right"][0], geom["right"][1], 'b-', linewidth=0.8, alpha=0.3, label='Lane -1' if rid==3 else "")
        # Plot Lane 1 (Red)
        plt.plot(geom["left"][0], geom["left"][1], 'r-', linewidth=0.8, alpha=0.3, label='Lane 1' if rid==3 else "")
        
        # Annotate Road ID at start
        if len(geom["ref"][0]) > 0:
            plt.text(geom["ref"][0][0], geom["ref"][1][0], f"R{rid}", fontsize=8, color='black')

    # Plot Dense Route
    dx = [wp.x for wp in dense_wps]
    dy = [wp.y for wp in dense_wps]
    plt.plot(dx, dy, 'g.-', linewidth=1.5, markersize=3, label="Dense Route")
    
    # Highlight specific points
    plt.plot(dx[0], dy[0], 'go', markersize=8, label="Start")
    plt.plot(dx[-1], dy[-1], 'rx', markersize=8, label="End")

    # Plot Sparse WPs
    sx = [wp.x for wp in sparse_wps]
    sy = [wp.y for wp in sparse_wps]
    plt.plot(sx, sy, 'mo', markersize=8, fillstyle='none', markeredgewidth=2, label="Sparse WPs")

    # Annotate Sparse WPs with Road and Lane ID
    for i, wp in enumerate(sparse_wps):
        plt.text(wp.x + 1, wp.y + 1, f"WP{i}\nR{wp.road_id}\nL{wp.lane_id}", fontsize=9, color='magenta', ha='left')

    # Annotate Dense WPs with Lane ID (every 10th point or when lane changes)
    last_lane = None
    for i, wp in enumerate(dense_wps):
        # Annotate if: Start/End, Lane Change, or every 10 points
        if i == 0 or i == len(dense_wps)-1 or wp.lane_id != last_lane or i % 10 == 0:
             plt.text(wp.x, wp.y, f"L{wp.lane_id}", fontsize=6, color='darkgreen', alpha=0.8)
        last_lane = wp.lane_id

    # Zoom in on Intersection
    #plt.xlim(-60, 60)
    #plt.ylim(-60, 60)

    plt.legend()
    plt.savefig("route_debug_plot_zoom.png", dpi=150)
    print("Saved plot to route_debug_plot_zoom.png")
    # plt.show()

if __name__ == "__main__":
    main()
