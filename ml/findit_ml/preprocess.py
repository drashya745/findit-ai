"""Deterministic text normalization for matching.

Every function here is pure: no randomness, no I/O, no state. Given the same input it
always returns the same output, which keeps fitting and ranking reproducible.
"""

from __future__ import annotations

import re
import unicodedata
from collections.abc import Iterable, Mapping
from typing import Any

# Spellings treated as "no value" (case-insensitive), matching config/dataset_mapping.conf.
# "<na>" is how pandas.NA prints.
MISSING_TOKENS = frozenset({"", "none", "null", "nan", "n/a", "na", "<na>", "unknown"})

# Words that say nothing about *which* item a report describes. Generic English plus
# lost-and-found boilerplate and chat filler. Kept explicit (not scikit-learn's built-in
# list, which contains item-like words such as "computer" and "fire").
STOPWORDS = frozenset(
    {
        # General English.
        "a", "an", "the", "and", "or", "but", "of", "on", "in", "at", "to", "for", "from",
        "with", "by", "near", "is", "are", "was", "were", "be", "been", "it", "its", "this",
        "that", "these", "those", "i", "me", "my", "mine", "we", "our", "you", "your", "he",
        "she", "they", "them", "his", "her", "has", "have", "had", "can", "cant", "could",
        "not", "no", "any", "some", "sum", "so", "very", "just", "what", "who", "whose",
        "where", "when", "there", "here", "up", "out", "if", "do", "does", "did", "am", "s",
        "t", "inside", "into", "about", "also", "too",
        # Lost-and-found boilerplate.
        "lost", "found", "lose", "missing", "find", "finding", "picked", "pick", "left",
        "dropped", "discovered", "gone", "someone", "somebody", "somewhere", "belongs",
        "belong", "item", "thing", "last", "seen", "campus", "anyone", "owner",
        # Chat filler.
        "please", "pls", "plz", "help", "idk", "lol", "bro", "btw", "sure",
    }
)

DEFAULT_EXTRA_FIELDS: tuple[str, ...] = ("category", "color", "brand")

_APOSTROPHES = re.compile(r"['’`]")
_NON_ALNUM = re.compile(r"[^0-9a-z]+")


def is_missing(value: Any) -> bool:
    """True for None, NaN/NA, empty or whitespace strings, and spellings like "None"."""
    if value is None:
        return True
    try:
        if value != value:  # NaN is the only value not equal to itself.
            return True
    except (TypeError, ValueError):  # pandas.NA refuses boolean conversion.
        pass
    return str(value).strip().casefold() in MISSING_TOKENS


def _fold_to_ascii(text: str) -> str:
    decomposed = unicodedata.normalize("NFKD", text)
    return "".join(ch for ch in decomposed if not unicodedata.combining(ch))


def _fix_digit_zero(token: str) -> str:
    """'l0st' -> 'lost', but 'a102' and '2026' are left alone."""
    has_letter = any(ch.isalpha() for ch in token)
    other_digits = any(ch.isdigit() and ch != "0" for ch in token)
    return token.replace("0", "o") if has_letter and not other_digits else token


def normalize_text(value: Any) -> str:
    """Lowercase ASCII words separated by single spaces; "" for missing values.

    Steps: Unicode NFKD with accents removed, casefold, drop apostrophes ("can't" -> "cant"),
    replace every other non-alphanumeric run with a space, apply the digit-zero fix.
    """
    if is_missing(value):
        return ""
    text = _fold_to_ascii(str(value)).casefold()
    text = _APOSTROPHES.sub("", text)
    text = _NON_ALNUM.sub(" ", text)
    return " ".join(_fix_digit_zero(token) for token in text.split())


def informative_tokens(value: Any) -> list[str]:
    """Normalized tokens of length >= 2 that are not stopwords, in original order."""
    return [t for t in normalize_text(value).split() if len(t) >= 2 and t not in STOPWORDS]


def informative_terms(value: Any) -> set[str]:
    return set(informative_tokens(value))


def build_document(
    report: Mapping[str, Any],
    *,
    description_weight: int = 2,
    extra_fields: Iterable[str] = DEFAULT_EXTRA_FIELDS,
) -> str:
    """Text representation of one report for TF-IDF.

    The description comes first and is repeated `description_weight` times so it outweighs
    the short structured fields. Each extra field (category, colour, brand) is appended once,
    only when present. Identifiers (report_id, source_ref, reporter_id) and labels are never
    read, so they cannot leak into features.

    Returns "" when the description has no informative words, even if other fields are set:
    structured fields only enrich a description; on their own they would just repeat the
    category/colour comparison done elsewhere.
    """
    if description_weight < 1:
        raise ValueError("description_weight must be at least 1")
    description = informative_tokens(report.get("description"))
    if not description:
        return ""
    parts = description * description_weight
    for field in extra_fields:
        parts.extend(informative_tokens(report.get(field)))
    return " ".join(parts)
