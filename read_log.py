import sys

def safe_print(path):
    try:
        # Try cp932 (Shift-JIS) commonly used in Japanese Windows cmd
        with open(path, 'r', encoding='cp932', errors='replace') as f:
            print(f.read())
    except Exception as e:
        print(f"Error reading with cp932: {e}")
        try:
             with open(path, 'r', encoding='utf-8', errors='replace') as f:
                print(f.read())
        except Exception as e2:
            print(f"Error reading with utf-8: {e2}")

if __name__ == "__main__":
    if len(sys.argv) > 1:
        safe_print(sys.argv[1])
