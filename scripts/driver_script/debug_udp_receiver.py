import socket
import struct

PORT = 53995
IP = "0.0.0.0"

def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.bind((IP, PORT))
        print(f"DEBUG RECEIVER: Listening on {IP}:{PORT}")
    except Exception as e:
        print(f"ERROR: Could not bind to port {PORT}: {e}")
        return

    print("Waiting for packets...")
    count = 0
    try:
        while True:
            data, addr = sock.recvfrom(65536)
            count += 1
            print(f"[{count}] Received {len(data)} bytes from {addr}")
            
            if len(data) >= 4:
                # Try to look at first int
                mask = struct.unpack('<i', data[:4])[0]
                print(f"    LightMask: {mask}")
                
            # Hex dump first 16 bytes
            hex_data = data[:16].hex()
            print(f"    Hex: {hex_data}...")
            
    except KeyboardInterrupt:
        print("\nStopping...")
    finally:
        sock.close()

if __name__ == "__main__":
    main()
