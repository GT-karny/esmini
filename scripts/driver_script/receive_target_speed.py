"""
RealDriver Target Speed UDP Receiver

This script receives target speed packets from ControllerRealDriver.

Packet Structure:
    [Type: 1 byte = 1] + [targetSpeed: 8 bytes double]

Usage:
    python receive_target_speed.py [port]
    
Default port: 54995
"""

import socket
import struct
import sys

def main():
    # Default port
    port = 54995
    
    # Parse command line arguments
    if len(sys.argv) > 1:
        port = int(sys.argv[1])
    
    # Create UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", port))
    
    print(f"Listening for target speed on 127.0.0.1:{port}")
    print("Waiting for packets...\n")
    
    try:
        while True:
            data, addr = sock.recvfrom(1024)
            
            # Check packet structure
            if len(data) == 9:  # 1 byte type + 8 bytes double
                packet_type = data[0]
                
                if packet_type == 1:  # Target speed packet
                    target_speed = struct.unpack('d', data[1:9])[0]
                    print(f"Target Speed: {target_speed:.2f} m/s ({target_speed * 3.6:.2f} km/h)")
                else:
                    print(f"Unknown packet type: {packet_type}")
            else:
                print(f"Invalid packet size: {len(data)} bytes (expected 9)")
                
    except KeyboardInterrupt:
        print("\nShutting down...")
    finally:
        sock.close()

if __name__ == "__main__":
    main()
