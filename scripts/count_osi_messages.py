#!/usr/bin/env python3
"""OSI バイナリファイルのメッセージ数をカウント

Protobufのバージョン不一致を回避するため、パースせずにメッセージ数だけを数える。
"""
import struct
import sys
from pathlib import Path


def count_osi_messages(filename: Path) -> int:
    """OSI length-prefixed binary形式のメッセージ数をカウント

    Args:
        filename: OSIバイナリファイルのパス

    Returns:
        メッセージ数
    """
    count = 0
    try:
        with open(filename, 'rb') as f:
            while True:
                # 4バイトのメッセージサイズを読む
                size_bin = f.read(4)
                if len(size_bin) < 4:
                    break

                # 符号なし整数に変換
                msg_size = struct.unpack('I', size_bin)[0]

                # メッセージ本体をスキップ
                f.seek(msg_size, 1)  # SEEK_CUR = 1

                count += 1
    except OSError as e:
        print(f'ERROR: Could not read file {filename}: {e}', file=sys.stderr)
        return -1

    return count


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: count_osi_messages.py <osi_file>")
        sys.exit(1)

    filename = Path(sys.argv[1])
    if not filename.exists():
        print(f"ERROR: File not found: {filename}")
        sys.exit(1)

    count = count_osi_messages(filename)
    if count < 0:
        sys.exit(1)

    print(f"{filename}: {count} messages")
