import socket
import struct
import time

IP = "127.0.0.1"
PORT = 53995

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

print(f"Sending UDP packets to {IP}:{PORT}...")

frame = 0
try:
    while True:
        # Format: <iiiiddddIdII (Version 2)
        # 68 bytes
        packed_data = struct.pack('<iiiiddddIdII',
                                    2, 0, 0, frame,
                                    0.5, 0.0, 0.1, 1.0,
                                    1, 0.0,
                                    0, 0)
        sock.sendto(packed_data, (IP, PORT))
        print(f"Sent frame {frame}")
        frame += 1
        time.sleep(0.5)
except KeyboardInterrupt:
    print("Stopping")
    sock.close()
