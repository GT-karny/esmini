import tkinter as tk
from tkinter import ttk
import argparse
import time
from RealDriverClient import RealDriverClient

class RealDriverGUI:
    def __init__(self, root, client):
        self.root = root
        self.client = client
        self.root.title(f"RealDriver Controller (ID: {client.object_id})")

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

        # Steering (Note: esmini usually expects negative value for Left turn if using standard coord, 
        # but let's keep -1.0 to 1.0 and let the user adjust visually)
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

        # Quit Button
        ttk.Button(self.root, text="Quit", command=self.root.destroy).pack(pady=10)

    def update_loop(self):
        # Update client controls
        # Note: Original script passed NEGATIVE steering angle. 
        # If the simulator expects positive=LEFT, and slider is -1(Left)..1(Right), we might need logic.
        # Usually: standard X-Y plane, Angle increases CounterClockwise (Left).
        # Slider Left (-1) -> Angle (+1).
        # Let's apply negate to match typical steering wheel logic (Left turn = positive angle).
        steer_input = -self.steer_var.get() 

        self.client.set_controls(
            throttle=self.throttle_var.get(),
            brake=self.brake_var.get(),
            steering=steer_input 
        )
        
        self.client.set_gear(self.gear_var.get())
        self.client.set_engine_brake(self.engine_brake_var.get())

        # Update lights
        for key, var in self.light_vars.items():
            self.client.set_light_state(key, var.get())

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
    client = RealDriverClient(ip=args.ip, port=args.port, object_id=args.id)

    # Init GUI
    root = tk.Tk()
    app = RealDriverGUI(root, client)
    
    try:
        root.mainloop()
    finally:
        client.close()

if __name__ == "__main__":
    main()
