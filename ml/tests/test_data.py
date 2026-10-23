from pathlib import Path

import pytest

from findit_ml.data import REPORT_COLUMNS, detect_label_columns, load_reports, split_lost_found

PROCESSED = Path(__file__).resolve().parents[2] / "data" / "processed" / "reports.csv"
HEADER = ",".join(REPORT_COLUMNS)


def write(tmp_path, *rows, header=HEADER):
    path = tmp_path / "reports.csv"
    path.write_text("\n".join([header, *rows]) + "\n", encoding="utf-8")
    return path


def test_load_keeps_strings_as_written(tmp_path):
    path = write(
        tmp_path,
        "R000001,LOST,OPEN,Pen,Blue pen lost,blue,,Gym,U001,2025-09-01,,1",
        'R000002,FOUND,MATCHED,Pen,"Found pen, blue",blue,NA,Gym,U002,2025-09-02,Desk,2',
    )
    frame = load_reports(path)
    assert len(frame) == 2
    assert frame.loc[0, "brand"] == ""
    assert frame.loc[1, "brand"] == "NA"  # Not silently turned into NaN.
    assert frame.loc[1, "description"] == "Found pen, blue"
    lost, found = split_lost_found(frame)
    assert list(lost["report_id"]) == ["R000001"]
    assert found.empty  # R000002 is MATCHED, not OPEN.


def test_load_rejects_bad_files(tmp_path):
    with pytest.raises(ValueError, match="missing columns"):
        load_reports(write(tmp_path, "x", header="report_id,description"))
    with pytest.raises(ValueError, match="report_type"):
        load_reports(write(tmp_path, "R000001,RETURNED,OPEN,Pen,Blue pen,blue,,Gym,U001,2025-09-01,,1"))
    with pytest.raises(ValueError, match="duplicate"):
        load_reports(write(tmp_path,
                           "R000001,LOST,OPEN,Pen,Blue pen,blue,,Gym,U001,2025-09-01,,1",
                           "R000001,LOST,OPEN,Pen,Blue pen,blue,,Gym,U001,2025-09-01,,1"))


def test_detect_label_columns():
    assert detect_label_columns(list(REPORT_COLUMNS)) == []
    assert detect_label_columns(["report_id", "match_id", "is_pair"]) == ["match_id", "is_pair"]


@pytest.mark.skipif(not PROCESSED.exists(), reason="run ./build/findit_ingest first")
def test_processed_dataset_has_no_labels():
    frame = load_reports(PROCESSED)
    assert len(frame) == 30000
    assert detect_label_columns(frame.columns) == []
