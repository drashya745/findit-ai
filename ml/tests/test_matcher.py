from pathlib import Path

import pandas as pd
import pytest

from findit_ml.matcher import TextMatch, TextMatcher

FIXTURE = Path(__file__).parent / "fixtures" / "SYNTHETIC_matching_fixture.csv"


def report(report_id, description, category="", color="", brand=""):
    return {"report_id": report_id, "description": description, "category": category,
            "color": color, "brand": brand}


CORPUS = [
    report("L1", "Black leather wallet with student card", "Wallet", "black"),
    report("F1", "Black leather wallet found with a student card", "Wallet", "black"),
    report("F2", "Green folding umbrella left on bench", "Umbrella", "green"),
    report("F3", "Brown wallet without cards", "Wallet", "brown"),
    report("F4", "Scratched sunglass case", "Glasses", "grey"),
]


@pytest.fixture
def matcher():
    return TextMatcher().fit(CORPUS)


# ---------------------------------------------------------------- fitting and arguments

def test_fit_rejects_corpus_without_text():
    with pytest.raises(ValueError):
        TextMatcher().fit([report("X", "  "), report("Y", "lost found pls")])


def test_rank_requires_fit_and_valid_k():
    with pytest.raises(RuntimeError):
        TextMatcher().rank(CORPUS[0], CORPUS[1:])
    fitted = TextMatcher().fit(CORPUS)
    for bad in (0, -1, 2.5, True):
        with pytest.raises(ValueError):
            fitted.rank(CORPUS[0], CORPUS[1:], top_k=bad)


def test_constructor_validation():
    with pytest.raises(ValueError):
        TextMatcher(word_weight=1.5)
    with pytest.raises(ValueError):
        TextMatcher(extra_fields=("report_id",))
    with pytest.raises(ValueError):
        TextMatcher(char_ngram_range=(4, 2))


def test_candidate_ids_are_validated(matcher):
    with pytest.raises(ValueError):
        matcher.rank(CORPUS[0], [report("F1", "a wallet"), report("F1", "another wallet")])
    with pytest.raises(ValueError):
        matcher.rank(CORPUS[0], [report("", "a wallet")])


# ---------------------------------------------------------------- empty text

@pytest.mark.parametrize("description", ["", "   ", None, "None", "lost pls help"])
def test_empty_lost_description_returns_no_ranking(matcher, description):
    lost = report("L9", description, "Wallet", "black")
    assert matcher.rank(lost, CORPUS[1:]) == []


def test_empty_candidate_scores_zero(matcher):
    results = matcher.rank(CORPUS[0], [report("F9", ""), CORPUS[1]], top_k=2)
    assert [r.candidate_id for r in results] == ["F1", "F9"]
    assert results[1].similarity == 0.0
    assert results[1].explanation == "Candidate has no usable text"


def test_no_candidates(matcher):
    assert matcher.rank(CORPUS[0], []) == []
    assert matcher.rank(CORPUS[0], [CORPUS[0]]) == []  # Only itself: excluded.


# ---------------------------------------------------------------- similarity behaviour

def test_identical_and_duplicate_descriptions(matcher):
    twin_a = report("F7", "Black leather wallet with student card", "Wallet", "black")
    twin_b = report("F6", "Black leather wallet with student card", "Wallet", "black")
    results = matcher.rank(CORPUS[0], [twin_a, CORPUS[2], twin_b], top_k=3)
    assert results[0].similarity == pytest.approx(1.0)
    assert results[0].similarity == results[1].similarity  # Exact tie...
    assert [r.candidate_id for r in results[:2]] == ["F6", "F7"]  # ...broken by ID.


def test_unrelated_item_scores_low_with_no_shared_terms(matcher):
    results = matcher.rank(CORPUS[0], [CORPUS[2], CORPUS[1]], top_k=2)
    related, unrelated = results
    assert related.candidate_id == "F1"
    assert unrelated.candidate_id == "F2"
    assert unrelated.shared_terms == ()
    assert unrelated.word_similarity == 0.0
    assert unrelated.similarity < 0.1
    assert unrelated.explanation.startswith("No exact shared terms") or (
        unrelated.explanation == "No text similarity"
    )


def test_partial_wording_counts_as_general_similarity(matcher):
    lost = report("L2", "Scratches on my sunglasses case")
    results = matcher.rank(lost, [CORPUS[4], CORPUS[2]], top_k=2)
    assert results[0].candidate_id == "F4"
    # "case" is shared exactly; "scratched"/"scratches" and "sunglass"/"sunglasses" only
    # overlap in characters, so char similarity exceeds what the exact terms explain.
    assert results[0].shared_terms == ("case",)
    assert results[0].char_similarity > results[0].word_similarity > 0
    assert "Exact shared terms: case" in results[0].explanation
    assert "general similarity" in results[0].explanation

    only_partial = report("L3", "Scratches sunglasses")
    result = matcher.rank(only_partial, [CORPUS[4]], top_k=1)[0]
    assert result.shared_terms == ()
    assert result.word_similarity == 0.0 and result.char_similarity > 0
    assert result.explanation.startswith("No exact shared terms; general similarity")


def test_shared_terms_include_category_and_colour_when_present(matcher):
    result = matcher.rank(CORPUS[0], [CORPUS[3]], top_k=1)[0]
    assert "wallet" in result.shared_terms


# ---------------------------------------------------------------- ordering and reuse

def test_stable_ordering_regardless_of_candidate_order(matcher):
    candidates = CORPUS[1:]
    expected = [(r.candidate_id, r.similarity) for r in matcher.rank(CORPUS[0], candidates, top_k=4)]
    for permutation in ([3, 2, 1, 0], [1, 3, 0, 2], [2, 0, 3, 1]):
        shuffled = [candidates[i] for i in permutation]
        got = [(r.candidate_id, r.similarity) for r in matcher.rank(CORPUS[0], shuffled, top_k=4)]
        assert got == expected
    assert [r.rank for r in matcher.rank(CORPUS[0], candidates, top_k=4)] == [1, 2, 3, 4]


def test_top_k_and_descending_scores(matcher):
    results = matcher.rank(CORPUS[0], CORPUS[1:], top_k=2)
    assert len(results) == 2
    assert results[0].similarity >= results[1].similarity


def test_fit_once_reuse_does_not_change_model(matcher):
    vocabulary = dict(matcher._word.vocabulary_)
    first = matcher.rank(CORPUS[0], CORPUS[1:])
    matcher.rank(report("L5", "Brand new unseen words zebra xylophone"), CORPUS[1:])
    assert matcher._word.vocabulary_ == vocabulary
    assert matcher.rank(CORPUS[0], CORPUS[1:]) == first
    assert "zebra" not in matcher._word.vocabulary_  # Unseen query words are not learned.


def test_ids_are_not_features(matcher):
    original = matcher.rank(CORPUS[0], [CORPUS[1]], top_k=1)[0]
    relabelled = dict(CORPUS[1], report_id="R999999", source_ref="leak")
    renamed = matcher.rank(CORPUS[0], [relabelled], top_k=1)[0]
    assert renamed.similarity == original.similarity


def test_accepts_dataframe_and_structured_output(matcher):
    results = matcher.rank(CORPUS[0], pd.DataFrame(CORPUS[1:]), top_k=1)
    assert isinstance(results[0], TextMatch)
    as_dict = results[0].to_dict()
    assert set(as_dict) == {"candidate_id", "rank", "similarity", "word_similarity",
                            "char_similarity", "shared_terms", "explanation"}
    assert isinstance(as_dict["shared_terms"], list)
    assert 0.0 <= as_dict["similarity"] <= 1.0


# ---------------------------------------------------------------- synthetic fixture

def test_synthetic_fixture_obvious_pairs_rank_first():
    """SYNTHETIC hand-written fixture (see fixtures/README.md); not an accuracy measure."""
    frame = pd.read_csv(FIXTURE, dtype=str, keep_default_na=False)
    lost = frame[frame["report_type"] == "LOST"]
    found = frame[frame["report_type"] == "FOUND"]
    features = frame.drop(columns=["synthetic_expected_match"])  # Labels never reach fit().
    matcher = TextMatcher().fit(features)
    for row in lost.to_dict("records"):
        expected = row.pop("synthetic_expected_match")
        top = matcher.rank(row, found.drop(columns=["synthetic_expected_match"]), top_k=1)
        assert top[0].candidate_id == expected, row["report_id"]
