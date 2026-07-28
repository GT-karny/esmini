"""api/vd_input.py の `_pack()` ワイヤフォーマットを、GT_Sim側で実際にこれを読む
`gt_esmini::NetworkInputBridge::Poll()` の仕様と突合するテスト
(feature:F7 監査「web backend APIの18ファイル中14ファイルがテスト0件」の最優先4件の
ひとつ: `_pack`のワイヤフォーマットが無検証で、バイト列がC++側と静かに食い違うと
手動介入の入力経路が壊れるリスクが指摘されていた)。

対応するC++側（`GT_esmini/src/control/manualdrive/` 配下、他ワーカー担当のため本
ターンでは一切変更していない、読解のみ）:

  include/gt_esmini/control/manualdrive/NetworkInputBridge.hpp
    static constexpr uint32_t MAGIC_PEDAL_STEER = 0x50535443;  // "PSTC"

  src/control/manualdrive/NetworkInputBridge.cpp (Poll())
    // [4B magic][8B steering][8B throttle][8B brake][8B clutch][4B gear][4B buttons] = 44 bytes
    static constexpr size_t PEDAL_STEER_WIRE_SIZE = 44;
    std::memcpy(&magic, p, 4);            p += 4;
    std::memcpy(&last_cmd_.steering, p, 8); p += 8;
    std::memcpy(&last_cmd_.throttle, p, 8); p += 8;
    std::memcpy(&last_cmd_.brake,    p, 8); p += 8;
    std::memcpy(&last_cmd_.clutch,   p, 8); p += 8;
    std::memcpy(&last_cmd_.gear,     p, 4); p += 4;
    std::memcpy(&last_cmd_.buttons,  p, 4);

Below, `_CPP_*` constants and byte offsets are copied from that source directly
(NOT derived from vd_input._WIRE), so a silent change to _pack's field order,
count, or magic value shows up as a mismatch against an independent reference
rather than the test re-deriving its expectation from the code under test.
"""

from __future__ import annotations

import struct

from GT_esmini.web.backend.api import vd_input

# NetworkInputBridge.hpp
_CPP_MAGIC_PEDAL_STEER = 0x50535443  # "PSTC"

# NetworkInputBridge.cpp
_CPP_PEDAL_STEER_WIRE_SIZE = 44
_CPP_OFFSET_MAGIC = 0
_CPP_OFFSET_STEERING = 4
_CPP_OFFSET_THROTTLE = 12
_CPP_OFFSET_BRAKE = 20
_CPP_OFFSET_CLUTCH = 28
_CPP_OFFSET_GEAR = 36
_CPP_OFFSET_BUTTONS = 40


def test_pack_total_size_matches_cpp_pedal_steer_wire_size():
    packet = vd_input._pack(steering=0.0, throttle=0.0, brake=0.0, buttons=0)
    assert len(packet) == _CPP_PEDAL_STEER_WIRE_SIZE


def test_pack_magic_matches_cpp_magic_pedal_steer_at_offset_0():
    packet = vd_input._pack(steering=0.0, throttle=0.0, brake=0.0, buttons=0)
    (magic,) = struct.unpack_from("<I", packet, _CPP_OFFSET_MAGIC)
    assert magic == _CPP_MAGIC_PEDAL_STEER
    assert vd_input._MAGIC == _CPP_MAGIC_PEDAL_STEER


def test_pack_field_values_land_at_the_offsets_cpp_poll_reads_them_from():
    """Independently unpacks every field at the fixed byte offset Poll()'s
    sequential memcpy calls consume it from. A reordering inside _pack (e.g.
    swapping throttle/brake, or moving buttons before gear) would leave the
    total size and magic unchanged but would be caught here."""
    steering, throttle, brake, clutch = 0.37, 0.81, -0.12, 0.55
    gear, buttons = -1, 0xDEADBEEF

    packet = vd_input._pack(
        steering=steering,
        throttle=throttle,
        brake=brake,
        buttons=buttons,
        clutch=clutch,
        gear=gear,
    )

    assert struct.unpack_from("<d", packet, _CPP_OFFSET_STEERING)[0] == steering
    assert struct.unpack_from("<d", packet, _CPP_OFFSET_THROTTLE)[0] == throttle
    assert struct.unpack_from("<d", packet, _CPP_OFFSET_BRAKE)[0] == brake
    assert struct.unpack_from("<d", packet, _CPP_OFFSET_CLUTCH)[0] == clutch
    assert struct.unpack_from("<i", packet, _CPP_OFFSET_GEAR)[0] == gear
    assert struct.unpack_from("<I", packet, _CPP_OFFSET_BUTTONS)[0] == buttons


def test_pack_buttons_is_read_by_cpp_as_a_raw_uint32_memcpy():
    # Poll() memcpy's 4 raw bytes into a uint32_t with no sign handling -- a
    # negative Python int must be masked into the same bit pattern C++ would
    # read as a large unsigned value, not raise or wrap differently.
    packet = vd_input._pack(steering=0.0, throttle=0.0, brake=0.0, buttons=-1)
    (buttons,) = struct.unpack_from("<I", packet, _CPP_OFFSET_BUTTONS)
    assert buttons == 0xFFFFFFFF


def test_pack_gear_is_read_by_cpp_as_a_signed_int32():
    # PedalSteerCommand.gear is `int` (VehicleCommand.hpp); reverse must
    # round-trip as negative, not wrap to a large unsigned value.
    packet = vd_input._pack(steering=0.0, throttle=0.0, brake=0.0, buttons=0, gear=-1)
    (gear,) = struct.unpack_from("<i", packet, _CPP_OFFSET_GEAR)
    assert gear == -1


def test_pack_defaults_clutch_and_gear_to_zero():
    packet = vd_input._pack(steering=0.1, throttle=0.2, brake=0.3, buttons=7)
    assert struct.unpack_from("<d", packet, _CPP_OFFSET_CLUTCH)[0] == 0.0
    assert struct.unpack_from("<i", packet, _CPP_OFFSET_GEAR)[0] == 0
