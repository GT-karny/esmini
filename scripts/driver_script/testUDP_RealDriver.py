import tkinter as tk
from tkinter import ttk
import argparse
import time
from RealDriverClient import RealDriverClient, LightMode, IndicatorMode

# OSI compliant ADAS function labels for GUI (commonly used subset)
ADAS_GUI_FUNCTIONS = {
    # key: (display_label, osi_function_name)
    'acc': ('ACC (Adaptive Cruise)', 'adaptive_cruise_control'),
    'cc':  ('Cruise Control', 'cruise_control'),
    'lka': ('LKA (Lane Keep)', 'lane_keeping_assist'),
    'ldw': ('LDW (Lane Departure)', 'lane_departure_warning'),
    'aeb': ('AEB (Emergency Brake)', 'automatic_emergency_braking'),
    'bsw': ('BSW (Blind Spot)', 'blind_spot_warning'),
    'fcw': ('FCW (Forward Collision)', 'forward_collision_warning'),
    'ada': ('Active Driving Assist', 'active_driving_assistance'),
    'hap': ('Highway Autopilot', 'highway_autopilot'),
    'apa': ('Active Parking', 'active_parking_assistance'),
    'ahb': ('Auto High Beams', 'automatic_high_beams'),
    'dm':  ('Driver Monitoring', 'driver_monitoring'),
}

# State mapping for GUI
ADAS_STATES = {
    "UNKNOWN": 0,
    "OTHER": 1,
    "ERRORED": 2,
    "UNAVAILABLE": 3,
    "AVAILABLE": 4,
    "STANDBY": 5,
    "ACTIVE": 6
}
ADAS_STATE_NAMES = list(ADAS_STATES.keys())

class RealDriverGUI:
    def __init__(self, root, client):
        self.root = root
        self.client = client
        self.root.title(f"RealDriver Controller (UDP)")

        # Variables
        self.throttle_var = tk.DoubleVar(value=0.0)
        self.brake_var = tk.DoubleVar(value=0.0)
        self.steer_var = tk.DoubleVar(value=0.0)
        self.gear_var = tk.IntVar(value=1) # 1: D, 0: N, -1: R
        self.engine_brake_var = tk.DoubleVar(value=0.49) # Default 0.49
        
        # Light variables
        self.light_vars = {
            'low': tk.BooleanVar(value=False),
            'high': tk.BooleanVar(value=False),
            'left': tk.BooleanVar(value=False),
            'right': tk.BooleanVar(value=False),
            'hazard': tk.BooleanVar(value=False),
            'fog_front': tk.BooleanVar(value=False),
            'fog_rear': tk.BooleanVar(value=False)
        }

        # ADAS variables (OSI compliant function names)
        # Using StringVar to store selected state name from Combobox
        self.adas_vars = {}
        for key, (label, osi_name) in ADAS_GUI_FUNCTIONS.items():
            self.adas_vars[osi_name] = tk.StringVar(value="UNKNOWN")

        self.create_widgets()
        self.update_loop()

    def create_widgets(self):
        # Frame for Controls
        control_frame = ttk.LabelFrame(self.root, text="Driving Controls", padding=10)
        control_frame.pack(fill="x", padx=10, pady=5)

        # Throttle
        ttk.Label(control_frame, text="Throttle").grid(row=0, column=0, sticky="e")
        ttk.Scale(control_frame, from_=0.0, to=1.0, variable=self.throttle_var, orient="horizontal", length=200).grid(row=0, column=1, padx=10)
        
        # Brake
        ttk.Label(control_frame, text="Brake").grid(row=1, column=0, sticky="e")
        ttk.Scale(control_frame, from_=0.0, to=1.0, variable=self.brake_var, orient="horizontal", length=200).grid(row=1, column=1, padx=10)

        # Steering
        ttk.Label(control_frame, text="Steering").grid(row=2, column=0, sticky="e")
        ttk.Scale(control_frame, from_=-1.0, to=1.0, variable=self.steer_var, orient="horizontal", length=200).grid(row=2, column=1, padx=10)
        
        # Engine Brake
        ttk.Label(control_frame, text="Eng Brake (m/s2)").grid(row=3, column=0, sticky="e")
        ttk.Scale(control_frame, from_=0.0, to=5.0, variable=self.engine_brake_var, orient="horizontal", length=200).grid(row=3, column=1, padx=10)


        # Gear
        gear_frame = ttk.LabelFrame(self.root, text="Gear", padding=10)
        gear_frame.pack(fill="x", padx=10, pady=5)
        
        ttk.Radiobutton(gear_frame, text="Reverse (R)", variable=self.gear_var, value=-1).pack(side="left", padx=10)
        ttk.Radiobutton(gear_frame, text="Neutral (N)", variable=self.gear_var, value=0).pack(side="left", padx=10)
        ttk.Radiobutton(gear_frame, text="Drive (D)", variable=self.gear_var, value=1).pack(side="left", padx=10)

        # Lights
        light_frame = ttk.LabelFrame(self.root, text="Lights", padding=10)
        light_frame.pack(fill="x", padx=10, pady=5)

        # Layout mapping
        layout = [
            [("Low Beam", 'low'), ("High Beam", 'high')],
            [("Left Ind", 'left'), ("Hazard", 'hazard'), ("Right Ind", 'right')],
            [("Fog Front", 'fog_front'), ("Fog Rear", 'fog_rear')]
        ]

        for r, row_items in enumerate(layout):
            for c, (label, key) in enumerate(row_items):
                ttk.Checkbutton(light_frame, text=label, variable=self.light_vars[key]).grid(row=r, column=c, padx=5, sticky="w")

        # ADAS Functions Frame (OSI compliant)
        adas_frame = ttk.LabelFrame(self.root, text="ADAS Functions (OSI)", padding=10)
        adas_frame.pack(fill="x", padx=10, pady=5)

        # Layout: Grid of Label + Combobox
        items = list(ADAS_GUI_FUNCTIONS.items())
        cols = 3
        
        for i, (key, (label, osi_name)) in enumerate(items):
            row = i // cols
            col = i % cols
            
            frame = ttk.Frame(adas_frame)
            frame.grid(row=row, column=col, sticky="w", padx=10, pady=5)
            
            ttk.Label(frame, text=label).pack(anchor="w")
            
            cb = ttk.Combobox(frame, textvariable=self.adas_vars[osi_name], values=ADAS_STATE_NAMES, state="readonly", width=12)
            cb.pack(anchor="w")

        # Quit Button
        ttk.Button(self.root, text="Quit", command=self.root.destroy).pack(pady=10)

    def update_loop(self):
        # Update client controls
        # Negate steering to match typical Logic (Left = Positive)
        steer_input = -self.steer_var.get() 

        self.client.set_controls(
            throttle=self.throttle_var.get(),
            brake=self.brake_var.get(),
            steering=steer_input 
        )
        
        self.client.set_gear(self.gear_var.get())
        self.client.set_engine_brake(self.engine_brake_var.get())

        # Update lights using High-Level API
        # Headlights
        if self.light_vars['high'].get():
            self.client.set_headlights(LightMode.HIGH)
        elif self.light_vars['low'].get():
            self.client.set_headlights(LightMode.LOW)
        else:
            self.client.set_headlights(LightMode.OFF)

        # Indicators
        if self.light_vars['hazard'].get():
            self.client.set_indicators(IndicatorMode.HAZARD)
        elif self.light_vars['left'].get():
            self.client.set_indicators(IndicatorMode.LEFT)
        elif self.light_vars['right'].get():
            self.client.set_indicators(IndicatorMode.RIGHT)
        else:
            self.client.set_indicators(IndicatorMode.OFF)

        # Fog Lights
        self.client.set_fog_lights(
            front=self.light_vars['fog_front'].get(),
            rear=self.light_vars['fog_rear'].get()
        )

        # Update ADAS functions (OSI compliant)
        for osi_name, var in self.adas_vars.items():
            state_str = var.get()
            state_int = ADAS_STATES.get(state_str, 0)
            self.client.set_adas_function(osi_name, state_int)

        # Send packet
        self.client.send_update()

        # Schedule next update (20ms = 50Hz)
        self.root.after(20, self.update_loop)

def main():
    parser = argparse.ArgumentParser(description="RealDriver GUI Controller")
    parser.add_argument("--ip", type=str, default="127.0.0.1", help="esmini Host IP")
    parser.add_argument("--port", type=int, default=53995, help="Base Port")
    parser.add_argument("--id", type=int, default=0, help="Object ID")
    
    args = parser.parse_args()

    # Init Client
    client = RealDriverClient(args.ip, args.port)

    # Init GUI
    root = tk.Tk()
    app = RealDriverGUI(root, client)
    
    try:
        root.mainloop()
    finally:
        client.close()

if __name__ == "__main__":
    main()
