"""Tests for services/result_service.py (feature:F7 audit #3: `api/results.py` +
`services/result_service.py` had zero coverage; the audit specifically named
"パストラバーサル防止(relative_to -> 403)、DatFormatUnsupported -> 409変換が
無検証" as the priority gap).

DAT binary parsing itself (dat.py, vendored upstream tooling) is out of scope
here -- we synthesize only the 4-byte version header _dat_major_version()
reads, and pre-write sim.csv directly (bypassing DAT->CSV conversion entirely)
to test compute_metrics/get_timeseries/_parse_csv in isolation from the DAT
wire format.
"""

from __future__ import annotations

import struct

import pytest

from GT_esmini.web.backend.services import result_service

# ---------------------------------------------------------------------------
# _dat_major_version
# ---------------------------------------------------------------------------


def test_dat_major_version_reads_first_uint32_le(tmp_path):
    dat = tmp_path / "sim.dat"
    dat.write_bytes(struct.pack("<I", 4) + b"\x00" * 20)
    assert result_service._dat_major_version(dat) == 4


def test_dat_major_version_none_when_file_too_short(tmp_path):
    dat = tmp_path / "sim.dat"
    dat.write_bytes(b"\x01\x02")  # < 4 bytes
    assert result_service._dat_major_version(dat) is None


def test_dat_major_version_none_when_file_missing(tmp_path):
    assert result_service._dat_major_version(tmp_path / "missing.dat") is None


# ---------------------------------------------------------------------------
# _ensure_csv
# ---------------------------------------------------------------------------


def test_ensure_csv_returns_existing_csv_without_touching_dat(tmp_path):
    (tmp_path / "sim.csv").write_text("existing", encoding="utf-8")
    # No sim.dat at all -- if _ensure_csv tried to convert, it would return None
    # instead of the pre-existing csv.
    result = result_service._ensure_csv(tmp_path)
    assert result == tmp_path / "sim.csv"


def test_ensure_csv_none_when_neither_dat_nor_csv_present(tmp_path):
    assert result_service._ensure_csv(tmp_path) is None


def test_ensure_csv_raises_unsupported_on_wrong_major_version(tmp_path):
    dat = tmp_path / "sim.dat"
    dat.write_bytes(struct.pack("<I", 5) + b"\x00" * 20)  # esmini >= 3.4.0 -> v5

    with pytest.raises(result_service.DatFormatUnsupported) as exc_info:
        result_service._ensure_csv(tmp_path)

    assert "v5" in str(exc_info.value)
    assert "v4" in str(exc_info.value)


def test_ensure_csv_converts_systemexit_from_dat_py_into_unsupported(
    tmp_path, monkeypatch
):
    """dat.py calls exit(-1) (SystemExit) on header/version paths not caught by
    our pre-check; that must never escape as a raw SystemExit inside the
    server process."""
    dat = tmp_path / "sim.dat"
    dat.write_bytes(struct.pack("<I", 4) + b"\x00" * 20)  # passes the pre-check

    import dat as dat_module

    class _ExplodingDATFile:
        def __init__(self, *a, **kw):
            raise SystemExit(-1)

    monkeypatch.setattr(dat_module, "DATFile", _ExplodingDATFile)

    with pytest.raises(result_service.DatFormatUnsupported):
        result_service._ensure_csv(tmp_path)


def test_ensure_csv_returns_none_on_generic_conversion_failure(tmp_path, monkeypatch):
    dat = tmp_path / "sim.dat"
    dat.write_bytes(struct.pack("<I", 4) + b"\x00" * 20)

    import dat as dat_module

    class _BrokenDATFile:
        def __init__(self, *a, **kw):
            raise RuntimeError("corrupt packet stream")

    monkeypatch.setattr(dat_module, "DATFile", _BrokenDATFile)

    assert result_service._ensure_csv(tmp_path) is None


# ---------------------------------------------------------------------------
# list_result_files / get_result_meta
# ---------------------------------------------------------------------------


def test_list_result_files_classifies_by_extension(tmp_path):
    (tmp_path / "sim.dat").write_bytes(b"x")
    (tmp_path / "sim.csv").write_text("x")
    (tmp_path / "stdout.txt").write_text("x")
    (tmp_path / "telemetry.jsonl").write_text("x")
    (tmp_path / "scene_default.xosc").write_text("x")
    (tmp_path / "plot.png").write_bytes(b"x")
    (tmp_path / "notes.md").write_text("x")

    files = result_service.list_result_files(tmp_path)
    by_name = {f.name: f.type for f in files}

    assert by_name["sim.dat"] == "dat"
    assert by_name["sim.csv"] == "csv"
    assert by_name["stdout.txt"] == "log"
    assert by_name["telemetry.jsonl"] == "jsonl"
    assert by_name["scene_default.xosc"] == "xosc"
    assert by_name["plot.png"] == "image"
    assert by_name["notes.md"] == "other"


def test_list_result_files_empty_for_missing_directory(tmp_path):
    assert result_service.list_result_files(tmp_path / "nope") == []


def test_get_result_meta_wraps_file_listing(tmp_path):
    (tmp_path / "sim.dat").write_bytes(b"x")
    meta = result_service.get_result_meta("job-1", "scn-1", str(tmp_path))
    assert meta.job_id == "job-1"
    assert meta.scenario_id == "scn-1"
    assert len(meta.files) == 1


# ---------------------------------------------------------------------------
# compute_metrics / get_timeseries / _parse_csv (pre-written sim.csv, bypassing
# DAT conversion entirely)
# ---------------------------------------------------------------------------

_CSV = (
    "esmini,1.0,OpenDRIVE,road.xodr\n"
    "time,id,name,speed,x,y,roadId,laneId,s\n"
    "0.0,0,Ego,10.0,0.0,0.0,1,-1,0.0\n"
    "1.0,0,Ego,12.5,10.0,0.0,1,-1,10.0\n"
    "2.0,0,Ego,15.0,25.0,0.0,1,-1,25.0\n"
)


def _write_csv(tmp_path, content=_CSV):
    (tmp_path / "sim.csv").write_text(content, encoding="utf-8")


def test_compute_metrics_none_when_no_data(tmp_path):
    assert result_service.compute_metrics(str(tmp_path)) is None


def test_compute_metrics_propagates_dat_format_unsupported(tmp_path):
    (tmp_path / "sim.dat").write_bytes(struct.pack("<I", 5) + b"\x00" * 20)
    with pytest.raises(result_service.DatFormatUnsupported):
        result_service.compute_metrics(str(tmp_path))


def test_compute_metrics_summary_and_final_state(tmp_path):
    _write_csv(tmp_path)
    metrics = result_service.compute_metrics(str(tmp_path))

    assert metrics["summary"]["num_frames"] == 3
    assert metrics["summary"]["duration"] == pytest.approx(2.0)
    assert metrics["summary"]["max_speed"] == pytest.approx(15.0)
    assert metrics["summary"]["min_speed"] == pytest.approx(10.0)
    assert metrics["summary"]["avg_speed"] == pytest.approx((10.0 + 12.5 + 15.0) / 3)
    assert metrics["final_state"]["time"] == pytest.approx(2.0)
    assert metrics["final_state"]["road_id"] == 1


def test_get_timeseries_none_data_returns_empty_list(tmp_path):
    assert result_service.get_timeseries(str(tmp_path)) == []


def test_get_timeseries_propagates_dat_format_unsupported(tmp_path):
    (tmp_path / "sim.dat").write_bytes(struct.pack("<I", 5) + b"\x00" * 20)
    with pytest.raises(result_service.DatFormatUnsupported):
        result_service.get_timeseries(str(tmp_path))


def test_get_timeseries_filters_by_entity_name(tmp_path):
    csv = (
        "esmini,1.0,OpenDRIVE,road.xodr\n"
        "time,id,name,speed\n"
        "0.0,0,Ego,10.0\n"
        "0.0,1,Other,20.0\n"
    )
    _write_csv(tmp_path, csv)

    data = result_service.get_timeseries(str(tmp_path), entity="Other")

    assert len(data) == 1
    assert data[0]["name"] == "Other"


def test_get_timeseries_falls_back_to_id_zero_then_all_rows(tmp_path):
    csv = (
        "esmini,1.0,OpenDRIVE,road.xodr\n"
        "time,id,name,speed\n"
        "0.0,0,Vehicle1,10.0\n"
    )
    _write_csv(tmp_path, csv)

    # entity="Ego" matches nothing by name -> falls back to id==0
    data = result_service.get_timeseries(str(tmp_path), entity="Ego")

    assert len(data) == 1
    assert data[0]["name"] == "Vehicle1"


def test_get_timeseries_field_selection(tmp_path):
    _write_csv(tmp_path)
    data = result_service.get_timeseries(str(tmp_path), fields=["time", "speed"])
    assert all(set(row.keys()) <= {"time", "speed"} for row in data)
    assert len(data) == 3


def test_parse_csv_skips_metadata_line_and_coerces_types(tmp_path):
    _write_csv(tmp_path)
    rows = result_service._parse_csv(tmp_path / "sim.csv")

    assert len(rows) == 3
    assert isinstance(rows[0]["time"], float)
    assert isinstance(rows[0]["id"], int)
    assert rows[0]["name"] == "Ego"


def test_parse_csv_skips_malformed_rows_with_wrong_column_count(tmp_path):
    csv = (
        "esmini,1.0\n"
        "time,speed\n"
        "0.0,10.0\n"
        "1.0,10.0,extra_column\n"  # malformed -- must be skipped, not raise
        "2.0,12.0\n"
    )
    _write_csv(tmp_path, csv)

    rows = result_service._parse_csv(tmp_path / "sim.csv")

    assert len(rows) == 2
    assert [r["time"] for r in rows] == [0.0, 2.0]


def test_parse_csv_empty_when_fewer_than_two_lines(tmp_path):
    (tmp_path / "sim.csv").write_text("only one line\n", encoding="utf-8")
    assert result_service._parse_csv(tmp_path / "sim.csv") == []
