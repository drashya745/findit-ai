"""Ranking and binary-decision metrics with explicit handling of undefined cases.

A metric whose denominator is zero is *undefined* and returned as None, never as 0 or 1.
Callers must report undefined values as such ("undefined"), not silently drop them.
"""

from __future__ import annotations

from collections.abc import Iterable, Mapping, Sequence
from dataclasses import dataclass


@dataclass(frozen=True)
class Ratio:
    numerator: int
    denominator: int

    @property
    def value(self) -> float | None:
        return None if self.denominator == 0 else self.numerator / self.denominator

    def __str__(self) -> str:
        value = self.value
        return "undefined (0/0)" if value is None else f"{value:.3f} ({self.numerator}/{self.denominator})"


def _check_ranking(ranked_ids: Sequence[str], k: int) -> None:
    if k < 1:
        raise ValueError("k must be at least 1")
    if len(set(ranked_ids)) != len(ranked_ids):
        raise ValueError("ranked list contains duplicate IDs")


def hits_at_k(ranked_ids: Sequence[str], relevant: set[str], k: int) -> int:
    _check_ranking(ranked_ids, k)
    return sum(1 for candidate in ranked_ids[:k] if candidate in relevant)


def precision_at_k(ranked_ids: Sequence[str], relevant: set[str], k: int) -> float | None:
    """Relevant results in the top K divided by K (strict: short lists are not rewarded).

    Undefined (None) for a query with no relevant items: there is nothing to retrieve, so the
    query says nothing about retrieval precision. Such queries are reported separately.
    """
    hits = hits_at_k(ranked_ids, relevant, k)
    return None if not relevant else hits / k


def recall_at_k(ranked_ids: Sequence[str], relevant: set[str], k: int) -> float | None:
    """Share of relevant items found in the top K; undefined when there are none."""
    hits = hits_at_k(ranked_ids, relevant, k)
    return None if not relevant else hits / len(relevant)


def reciprocal_rank(ranked_ids: Sequence[str], relevant: set[str]) -> float | None:
    """1 / rank of the first relevant item, 0 if none is ranked; undefined with no relevant items."""
    if not relevant:
        return None
    _check_ranking(ranked_ids, 1)
    for position, candidate in enumerate(ranked_ids, start=1):
        if candidate in relevant:
            return 1.0 / position
    return 0.0


def mean_defined(values: Iterable[float | None]) -> tuple[float | None, int, int]:
    """(mean of defined values or None, number defined, number undefined)."""
    values = list(values)
    defined = [v for v in values if v is not None]
    mean = sum(defined) / len(defined) if defined else None
    return mean, len(defined), len(values) - len(defined)


@dataclass(frozen=True)
class BinaryCounts:
    tp: int
    fp: int
    fn: int
    tn: int

    @property
    def precision(self) -> Ratio:
        return Ratio(self.tp, self.tp + self.fp)

    @property
    def recall(self) -> Ratio:
        return Ratio(self.tp, self.tp + self.fn)

    @property
    def f1(self) -> float | None:
        """Undefined when precision or recall is undefined, or both are 0."""
        p, r = self.precision.value, self.recall.value
        if p is None or r is None or p + r == 0:
            return None
        return 2 * p * r / (p + r)


def binary_counts(
    scores: Mapping[tuple[str, str], float],
    positives: set[tuple[str, str]],
    all_pairs: set[tuple[str, str]],
    threshold: float,
) -> BinaryCounts:
    """Pair-level decision "match iff score >= threshold".

    `scores` holds the pairs the pipeline actually scored. Pairs it never scored (e.g. removed
    by category blocking) count as predicted non-matches. `all_pairs` is the universe of
    (lost, found) pairs being judged and must contain every positive.
    """
    if not positives <= all_pairs:
        raise ValueError("every positive pair must be in all_pairs")
    predicted = {pair for pair in all_pairs if pair in scores and scores[pair] >= threshold}
    tp = len(predicted & positives)
    fp = len(predicted - positives)
    fn = len(positives - predicted)
    return BinaryCounts(tp=tp, fp=fp, fn=fn, tn=len(all_pairs) - tp - fp - fn)


def choose_threshold(
    scores: Mapping[tuple[str, str], float],
    positives: set[tuple[str, str]],
    all_pairs: set[tuple[str, str]],
) -> float | None:
    """Threshold maximizing F1 on the given (dev) data; ties go to the higher threshold.

    Returns None when F1 is undefined for every candidate threshold (e.g. no positives).
    """
    best: tuple[float, float] | None = None
    for threshold in sorted({scores[p] for p in all_pairs if p in scores}):
        f1 = binary_counts(scores, positives, all_pairs, threshold).f1
        if f1 is not None and (best is None or f1 >= best[0]):
            best = (f1, threshold)
    return None if best is None else best[1]
