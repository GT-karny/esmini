'''
   Raw UDP receiver for diagnosing OSI connectivity issues.
   This script listens on port 48198 and reports any UDP packets received.
'''

import socket
import time
import struct

def main():
    print("="*80)
    print("Raw UDP Receiver - Low-level Connectivity Test")
    print("="*80)
    print("\nListening on UDP port 48198...")
    print("This will show ANY data received on this port.\n")

    # Create UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(5.0)  # 5 second timeout

    try:
        sock.bind(('127.0.0.1', 48198))
        print("[OK] Successfully bound to 127.0.0.1:48198")
    except Exception as e:
        print(f"[ERROR] Failed to bind to port 48198: {e}")
        print("Another process may be using this port.")
        return

    print("\nWaiting for UDP packets...")
    print("Start GT_Sim with --osi 127.0.0.1 option\n")

    packet_count = 0
    last_check = time.time()

    while True:
        try:
            # Try to receive data
            data, addr = sock.recvfrom(8208)
            packet_count += 1

            print(f"\n[Packet {packet_count}] Received {len(data)} bytes from {addr}")

            # Try to parse header
            if len(data) >= 8:
                try:
                    counter, size = struct.unpack('iI', data[:8])
                    print(f"  Header: counter={counter}, size={size}")
                    print(f"  Payload size: {len(data)-8} bytes")
                except:
                    print("  Could not parse header")

            # Show first few bytes in hex
            hex_preview = ' '.join(f'{b:02x}' for b in data[:32])
            print(f"  First bytes (hex): {hex_preview}...")

        except socket.timeout:
            current = time.time()
            elapsed = current - last_check
            print(f"[{elapsed:.1f}s] No data received (still waiting...)")
            last_check = current

        except KeyboardInterrupt:
            print("\n\n[Quit] Ctrl+C pressed")
            break

        except Exception as e:
            print(f"\n[Error] {e}")
            import traceback
            traceback.print_exc()
            break

    sock.close()
    print(f"\nSocket closed. Received {packet_count} packets total.")
    print("="*80)

if __name__ == "__main__":
    main()
