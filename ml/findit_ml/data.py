"""Loading the normalized reports written by the C++ `findit_ingest` tool.

Schema of data/processed/reports.csv (inspected, Phase 4):
    report_id, report_type, status, category, description, color, brand, location,
    reporter_id, date, held_at, source_ref
Absent values are empty strings (e.g. brand on 1,652 rows, held_at on all rows).
There is no label / match column.
"""

from __future__ import annotations

from pathlib import Path

import pandas as pd

REPORT_COLUMNS = (
    "report_id", "report_type", "status", "category", "description", "color", "brand",
    "location", "reporter_id", "date", "held_at", "source_ref",
)
REPORT_TYPES = frozenset({"LOST", "FOUND"})
# Column-name fragments that would suggest ground-truth match labels.
LABEL_COLUMN_HINTS = ("match", "pair", "label", "target", "truth", "gold")


def load_reports(path: str | Path) -> pd.DataFrame:
    """Read reports.csv as strings, keeping "", "None" and "NA" as written (no NaN conversion)."""
    frame = pd.read_csv(path, dtype=str, keep_default_na=False, na_filter=False, encoding="utf-8")
    missing = [c for c in REPORT_COLUMNS if c not in frame.columns]
    if missing:
        raise ValueError(f"{path}: missing columns {missing}")
    bad_types = sorted(set(frame["report_type"]) - REPORT_TYPES)
    if bad_types:
        raise ValueError(f"{path}: unexpected report_type values {bad_types}")
    duplicated = frame["report_id"][frame["report_id"].duplicated()].tolist()
    if duplicated:
        raise ValueError(f"{path}: duplicate report_id values, e.g. {duplicated[:3]}")
    return frame


def detect_label_columns(columns: list[str] | pd.Index) -> list[str]:
    """Columns whose names suggest match labels. An empty list means no labels are available."""
    return [c for c in columns if any(hint in str(c).lower() for hint in LABEL_COLUMN_HINTS)]


def split_lost_found(frame: pd.DataFrame, *, open_only: bool = True) -> tuple[pd.DataFrame, pd.DataFrame]:
    if open_only:
        frame = frame[frame["status"] == "OPEN"]
    return frame[frame["report_type"] == "LOST"], frame[frame["report_type"] == "FOUND"]
