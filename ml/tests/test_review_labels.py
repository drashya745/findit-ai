import pandas as pd

from findit_ml.evaluation.review_labels import build_reviewed, run_checks


def frame(rows):
    return pd.DataFrame(rows).astype(str)


REPORTS = frame([
    {"report_id": "L1", "report_type": "LOST", "category": "Wallet", "color": "black", "location": "Library",
     "description": "Black leather wallet with student card", "source_ref": "SYNTHETIC:G1"},
    {"report_id": "L2", "report_type": "LOST", "category": "Keys", "color": "silver", "location": "Gym",
     "description": "keys", "source_ref": "SYNTHETIC:G2"},
    {"report_id": "F1", "report_type": "FOUND", "category": "Wallet", "color": "black", "location": "Canteen",
     "description": "Black wallet found", "source_ref": "SYNTHETIC:G1"},
    {"report_id": "F2", "report_type": "FOUND", "category": "Wallet", "color": "black", "location": "Gym",
     "description": "Another black wallet", "source_ref": "SYNTHETIC:G2"},
])
QUERIES = frame([
    {"lost_id": "L1", "group_id": "G1", "split": "dev", "has_true_match": "1"},
    {"lost_id": "L2", "group_id": "G2", "split": "test", "has_true_match": "1"},  # Wrong: no positive.
])


def checks_for(pairs):
    return set(run_checks(REPORTS, QUERIES, frame(pairs), None).check_id)


def test_clean_labels_have_no_structural_errors():
    found = checks_for([{"lost_id": "L1", "found_id": "F1", "label": "1", "group_id": "G1"},
                        {"lost_id": "L2", "found_id": "F2", "label": "0", "group_id": "G2"}])
    assert not found & {"A01", "A02", "A03", "A04", "A05", "A07", "A08"}


def test_detects_structural_problems():
    found = checks_for([
        {"lost_id": "L1", "found_id": "F1", "label": "1", "group_id": "G1"},
        {"lost_id": "L1", "found_id": "F1", "label": "0", "group_id": "G1"},   # Contradiction.
        {"lost_id": "L1", "found_id": "X9", "label": "0", "group_id": "G1"},   # Unknown ID.
        {"lost_id": "F2", "found_id": "L2", "label": "0", "group_id": "G2"},   # Swapped types.
        {"lost_id": "L2", "found_id": "F1", "label": "1", "group_id": "G1"},   # Group mismatch, F1 used twice.
    ])
    assert {"A01", "A02", "A04", "A05", "A07"} <= found


def test_flags_judgement_questions():
    found = checks_for([{"lost_id": "L1", "found_id": "F1", "label": "1", "group_id": "G1"}])
    assert "A06" in found  # L2 claims a true match that has no positive pair.
    assert "A09" in found  # Positive with a location conflict.
    assert "A10" in found  # L1/F2: same category and colour, unlabelled.
    assert "A12" in found  # L2 description has one informative word.


def test_reviewed_view_preserves_original_labels():
    pairs = frame([{"lost_id": "L1", "found_id": "F1", "label": "1", "group_id": "G1"}])
    checks = run_checks(REPORTS, QUERIES, pairs, None)
    notes = frame([{"lost_id": "L1", "found_id": "F2", "judgement": "x", "confidence": "low",
                    "proposed_label": "uncertain"}])
    reviewed = build_reviewed(pairs, checks, notes).set_index(["lost_id", "found_id"])
    assert reviewed.loc[("L1", "F1"), "original_label"] == "1"
    assert reviewed.loc[("L1", "F1"), "reviewed_label"] == "1"
    assert reviewed.loc[("L1", "F2"), "original_label"] == "unlabelled (closed-world 0)"
    assert reviewed.loc[("L1", "F2"), "reviewed_label"] == "uncertain"
    assert set(reviewed["human_verified"]) == {"no"}
