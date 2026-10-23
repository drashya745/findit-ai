import pandas as pd
import pytest

from findit_ml.evaluation.metrics import (
    BinaryCounts,
    Ratio,
    binary_counts,
    choose_threshold,
    mean_defined,
    precision_at_k,
    recall_at_k,
    reciprocal_rank,
)
from findit_ml.evaluation.run_eval import evaluate


# ---------------------------------------------------------------- ranked metrics

def test_ranked_metrics_basic():
    ranked = ["F3", "F1", "F2"]
    assert precision_at_k(ranked, {"F1"}, 1) == 0.0
    assert precision_at_k(ranked, {"F1"}, 3) == pytest.approx(1 / 3)
    assert recall_at_k(ranked, {"F1"}, 1) == 0.0
    assert recall_at_k(ranked, {"F1"}, 2) == 1.0
    assert reciprocal_rank(ranked, {"F1"}) == 0.5


def test_empty_candidate_set():
    # A true match exists but nothing was ranked: defined and zero.
    assert precision_at_k([], {"F1"}, 3) == 0.0
    assert recall_at_k([], {"F1"}, 3) == 0.0
    assert reciprocal_rank([], {"F1"}) == 0.0


def test_short_list_is_not_rewarded():
    assert precision_at_k(["F1"], {"F1"}, 5) == pytest.approx(0.2)


def test_no_positive_matches_is_undefined():
    assert precision_at_k(["F1", "F2"], set(), 1) is None
    assert recall_at_k(["F1", "F2"], set(), 1) is None
    assert reciprocal_rank(["F1"], set()) is None
    assert recall_at_k([], set(), 1) is None


def test_invalid_rankings():
    with pytest.raises(ValueError):
        precision_at_k(["F1"], {"F1"}, 0)
    with pytest.raises(ValueError):
        recall_at_k(["F1", "F1"], {"F1"}, 1)


def test_mean_defined_counts_undefined_values():
    assert mean_defined([1.0, None, 0.0]) == (0.5, 2, 1)
    assert mean_defined([None, None]) == (None, 0, 2)
    assert mean_defined([]) == (None, 0, 0)


# ---------------------------------------------------------------- binary metrics

def test_ratio_formatting():
    assert Ratio(0, 0).value is None and str(Ratio(0, 0)) == "undefined (0/0)"
    assert str(Ratio(1, 4)) == "0.250 (1/4)"


def test_binary_counts_and_f1():
    universe = {("L1", "F1"), ("L1", "F2"), ("L2", "F1"), ("L2", "F2")}
    positives = {("L1", "F1"), ("L2", "F2")}
    scores = {("L1", "F1"): 0.9, ("L1", "F2"): 0.6, ("L2", "F1"): 0.2}  # (L2,F2) never scored.
    counts = binary_counts(scores, positives, universe, 0.5)
    assert counts == BinaryCounts(tp=1, fp=1, fn=1, tn=1)
    assert counts.precision.value == 0.5 and counts.recall.value == 0.5
    assert counts.f1 == pytest.approx(0.5)


def test_threshold_with_no_predictions_makes_precision_undefined():
    universe = {("L1", "F1")}
    counts = binary_counts({("L1", "F1"): 0.3}, {("L1", "F1")}, universe, 0.9)
    assert counts.precision.value is None
    assert counts.recall.value == 0.0
    assert counts.f1 is None


def test_no_positives_makes_recall_and_f1_undefined():
    universe = {("L1", "F1")}
    counts = binary_counts({("L1", "F1"): 0.8}, set(), universe, 0.5)
    assert counts.recall.value is None and counts.precision.value == 0.0
    assert counts.f1 is None


def test_zero_precision_and_recall_make_f1_undefined():
    universe = {("L1", "F1"), ("L1", "F2")}
    counts = binary_counts({("L1", "F1"): 0.1, ("L1", "F2"): 0.9}, {("L1", "F1")}, universe, 0.5)
    assert counts.precision.value == 0.0 and counts.recall.value == 0.0
    assert counts.f1 is None


def test_positive_outside_universe_is_rejected():
    with pytest.raises(ValueError):
        binary_counts({}, {("L9", "F9")}, {("L1", "F1")}, 0.5)


def test_choose_threshold():
    universe = {("L1", "F1"), ("L1", "F2"), ("L2", "F1")}
    scores = {("L1", "F1"): 0.9, ("L1", "F2"): 0.4, ("L2", "F1"): 0.7}
    assert choose_threshold(scores, {("L1", "F1")}, universe) == 0.9
    assert choose_threshold(scores, set(), universe) is None  # F1 undefined everywhere.
    assert choose_threshold({}, {("L1", "F1")}, universe) is None  # Nothing scored.


# ---------------------------------------------------------------- end-to-end on a tiny set

def test_evaluate_handles_empty_rankings_and_no_match_queries():
    queries = pd.DataFrame([
        {"lost_id": "L1", "group_id": "G1", "split": "dev", "has_true_match": "1", "difficulty": "easy"},
        {"lost_id": "L2", "group_id": "G2", "split": "test", "has_true_match": "1", "difficulty": "x"},
        {"lost_id": "L3", "group_id": "N1", "split": "test", "has_true_match": "0", "difficulty": "x"},
    ])
    pairs = pd.DataFrame([
        {"lost_id": "L1", "found_id": "F1", "label": "1", "group_id": "G1", "rationale": ""},
        {"lost_id": "L2", "found_id": "F2", "label": "1", "group_id": "G2", "rationale": ""},
    ])
    reports = pd.DataFrame({"report_id": ["F1", "F2", "F3"], "report_type": ["FOUND"] * 3})
    rankings = pd.DataFrame([
        {"strategy": "s", "category_blocking": "on", "lost_id": "L1", "rank": 1, "found_id": "F1", "score": 0.9},
        {"strategy": "s", "category_blocking": "on", "lost_id": "L1", "rank": 2, "found_id": "F3", "score": 0.2},
        {"strategy": "s", "category_blocking": "on", "lost_id": "L3", "rank": 1, "found_id": "F3", "score": 0.4},
        # L2 has no candidates at all (empty candidate set).
    ])
    ranked, binary, per_query = evaluate(queries, pairs, reports, rankings)
    test_k1 = ranked[(ranked["split"] == "test") & (ranked["k"] == 1)].iloc[0]
    assert test_k1["queries"] == 2 and test_k1["queries_with_true_match"] == 1
    assert test_k1["queries_undefined_no_true_match"] == 1
    assert test_k1["precision_at_k"] == "0.000" and test_k1["recall_at_k"] == "0.000"
    dev = binary[binary["split"] == "dev"].iloc[0]
    assert dev["threshold"] == "0.9000" and dev["f1"] == "1.000"
    test = binary[binary["split"] == "test"].iloc[0]
    assert test["tp"] == 0 and test["fn"] == 1 and test["precision"] == "undefined (0/0)"
    assert test["f1"] == "undefined"
    l2 = per_query[per_query["lost_id"] == "L2"].iloc[0]
    assert l2["true_rank"] == "not_scored" and l2["candidates_scored"] == 0


def test_sensitivity_excludes_only_uncertain_pairs():
    from findit_ml.evaluation.sensitivity import sensitivity

    queries = pd.DataFrame([{"lost_id": "L1", "split": "dev"}, {"lost_id": "L2", "split": "test"}])
    pairs = pd.DataFrame([{"lost_id": "L1", "found_id": "F1", "label": "1"},
                          {"lost_id": "L2", "found_id": "F2", "label": "1"}])
    reports = pd.DataFrame({"report_id": ["F1", "F2", "F3"], "report_type": ["FOUND"] * 3})
    rankings = pd.DataFrame([
        {"strategy": "s", "category_blocking": "on", "lost_id": "L1", "found_id": "F1", "score": "0.9"},
        {"strategy": "s", "category_blocking": "on", "lost_id": "L1", "found_id": "F3", "score": "0.5"},
        {"strategy": "s", "category_blocking": "on", "lost_id": "L2", "found_id": "F2", "score": "0.8"},
        {"strategy": "s", "category_blocking": "on", "lost_id": "L2", "found_id": "F3", "score": "0.95"},
    ])
    result = sensitivity(queries, pairs, reports, rankings, {("L2", "F3")}).set_index("variant")
    assert result.loc["original_labels", "test_fp"] == 1   # (L2, F3) scored 0.95 >= 0.9.
    assert result.loc["uncertain_excluded", "test_fp"] == 0
    assert result.loc["uncertain_excluded", "pairs_excluded"] == 1
    assert result.loc["original_labels", "dev_threshold"] == result.loc["uncertain_excluded", "dev_threshold"]
