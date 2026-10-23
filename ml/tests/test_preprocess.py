import math

import pandas as pd
import pytest

from findit_ml.preprocess import (
    build_document,
    informative_terms,
    informative_tokens,
    is_missing,
    normalize_text,
)


@pytest.mark.parametrize(
    "value", [None, "", "   ", "None", "none", "NULL", "nan", "N/A", "na", math.nan, pd.NA]
)
def test_missing_values(value):
    assert is_missing(value)
    assert normalize_text(value) == ""


@pytest.mark.parametrize("value", ["Nike", "0", "a", 0, "Natural"])
def test_present_values(value):
    assert not is_missing(value)


def test_normalize_text():
    assert normalize_text("L0st notebook (brown) bro pls help find.") == (
        "lost notebook brown bro pls help find"
    )
    assert normalize_text("Can't find my KEYCHAIN!!") == "cant find my keychain"
    assert normalize_text("Café  résumé\tpouch") == "cafe resume pouch"
    assert normalize_text("Classroom A102, 2026") == "classroom a102 2026"
    assert normalize_text("scraztches") == "scraztches"  # Typos are kept, not "corrected".


def test_normalize_is_deterministic():
    text = "Found sum USB cable (pink) lol not sure whose."
    assert {normalize_text(text) for _ in range(5)} == {"found sum usb cable pink lol not sure whose"}


def test_informative_tokens_drop_boilerplate():
    assert informative_tokens("I lost my grey bag, has scratches near the edge.") == [
        "grey", "bag", "scratches", "edge",
    ]
    assert informative_terms("Found it near the campus, pls help") == set()


def test_build_document_prioritizes_description_and_skips_missing_fields():
    report = {"description": "Blue steel bottle", "category": "Water Bottle", "color": "blue",
              "brand": "None"}
    assert build_document(report) == "blue steel bottle blue steel bottle water bottle blue"
    assert build_document(report, description_weight=1, extra_fields=()) == "blue steel bottle"


def test_build_document_needs_a_description():
    assert build_document({"description": "   ", "category": "Keys", "color": "silver"}) == ""
    assert build_document({"category": "Keys"}) == ""


def test_build_document_never_reads_identifiers():
    report = {"report_id": "R000123", "source_ref": "123", "reporter_id": "U007",
              "description": "Red cap with logo", "category": "Cap"}
    document = build_document(report)
    assert "r000123" not in document and "123" not in document and "u007" not in document


def test_build_document_rejects_bad_weight():
    with pytest.raises(ValueError):
        build_document({"description": "x y z"}, description_weight=0)
