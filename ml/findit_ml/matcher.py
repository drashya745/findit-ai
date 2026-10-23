"""TF-IDF + cosine similarity text matcher.

Each report becomes a document (see preprocess.build_document) that is vectorized twice:
  * word TF-IDF (unigrams): rewards exact shared words, weighted by rarity
  * character n-gram TF-IDF (3-5 chars within words): rewards partial wording such as
    "scratched" vs "scratches" or small typos
similarity = word_weight * cosine_word + (1 - word_weight) * cosine_char, in [0, 1].

The similarity is a relative ranking value, not a probability of a match.
"""

from __future__ import annotations

from collections.abc import Iterable, Mapping, Sequence
from dataclasses import asdict, dataclass
from typing import Any

import numpy as np
import pandas as pd
from sklearn.feature_extraction.text import TfidfVectorizer
from sklearn.metrics.pairwise import cosine_similarity

from findit_ml.preprocess import DEFAULT_EXTRA_FIELDS, build_document, is_missing

ID_FIELD = "report_id"
MAX_TERMS_IN_EXPLANATION = 8
# Rounding before sorting so mathematically equal scores tie exactly despite float noise.
SCORE_DECIMALS = 10


@dataclass(frozen=True)
class TextMatch:
    candidate_id: str
    rank: int
    similarity: float       # Combined score in [0, 1].
    word_similarity: float  # Cosine of word TF-IDF vectors.
    char_similarity: float  # Cosine of character n-gram TF-IDF vectors.
    shared_terms: tuple[str, ...]  # Exact informative words in both documents, rarest first.
    explanation: str

    def to_dict(self) -> dict[str, Any]:
        result = asdict(self)
        result["shared_terms"] = list(self.shared_terms)
        return result


def _as_records(reports: pd.DataFrame | Iterable[Mapping[str, Any]]) -> list[Mapping[str, Any]]:
    if isinstance(reports, pd.DataFrame):
        return reports.to_dict("records")
    records = list(reports)
    for record in records:
        if not isinstance(record, Mapping):
            raise TypeError("Each report must be a mapping (dict-like)")
    return records


class TextMatcher:
    """Fit once on a corpus of report texts, then rank candidates for many queries.

    The vocabulary and IDF weights are frozen at fit time; words first seen in a later
    query are ignored by the word vectorizer (their character n-grams may still count).
    Refit when the corpus changes substantially.
    """

    def __init__(
        self,
        *,
        word_weight: float = 0.6,
        description_weight: int = 2,
        extra_fields: Sequence[str] = DEFAULT_EXTRA_FIELDS,
        char_ngram_range: tuple[int, int] = (3, 5),
        min_df: int = 1,
    ) -> None:
        if not 0.0 <= word_weight <= 1.0:
            raise ValueError("word_weight must be between 0 and 1")
        if description_weight < 1:
            raise ValueError("description_weight must be at least 1")
        low, high = char_ngram_range
        if not 1 <= low <= high:
            raise ValueError("char_ngram_range must satisfy 1 <= low <= high")
        if ID_FIELD in extra_fields:
            raise ValueError("Identifiers must not be used as features")
        self.word_weight = word_weight
        self.description_weight = description_weight
        self.extra_fields = tuple(extra_fields)
        self.char_ngram_range = char_ngram_range
        self.min_df = min_df
        self._word: TfidfVectorizer | None = None
        self._char: TfidfVectorizer | None = None
        self.n_documents_fitted_ = 0

    # ------------------------------------------------------------------ fitting

    def document(self, report: Mapping[str, Any]) -> str:
        return build_document(
            report, description_weight=self.description_weight, extra_fields=self.extra_fields
        )

    def fit(self, reports: pd.DataFrame | Iterable[Mapping[str, Any]]) -> "TextMatcher":
        """Learn vocabulary and IDF from report texts. Uses only text fields, never labels or IDs."""
        documents = [d for d in (self.document(r) for r in _as_records(reports)) if d]
        if not documents:
            raise ValueError("Cannot fit: no report in the corpus has usable text")
        word = TfidfVectorizer(
            analyzer="word",
            token_pattern=r"(?u)\b\w\w+\b",
            lowercase=False,  # Already normalized.
            sublinear_tf=True,
            min_df=self.min_df,
        )
        char = TfidfVectorizer(
            analyzer="char_wb",
            ngram_range=self.char_ngram_range,
            lowercase=False,
            sublinear_tf=True,
            min_df=self.min_df,
        )
        word.fit(documents)
        char.fit(documents)
        self._word, self._char = word, char
        self.n_documents_fitted_ = len(documents)
        return self

    @property
    def is_fitted(self) -> bool:
        return self._word is not None and self._char is not None

    @property
    def vocabulary_size(self) -> int:
        return len(self._word.vocabulary_) if self._word is not None else 0

    # ------------------------------------------------------------------ ranking

    def rank(
        self,
        lost: Mapping[str, Any],
        candidates: pd.DataFrame | Iterable[Mapping[str, Any]],
        top_k: int = 5,
    ) -> list[TextMatch]:
        """Top-K candidates by text similarity, highest first; ties broken by candidate ID.

        Returns [] when the lost report has no usable text: with nothing to compare, any
        ordering would be arbitrary. A candidate without usable text scores 0.
        The lost report itself (same report_id) is excluded.
        """
        if not self.is_fitted:
            raise RuntimeError("TextMatcher is not fitted; call fit() first")
        if not isinstance(top_k, int) or isinstance(top_k, bool) or top_k < 1:
            raise ValueError("top_k must be a positive integer")

        lost_id = lost.get(ID_FIELD)
        records = self._eligible(_as_records(candidates), lost_id)
        lost_document = self.document(lost)
        if not records or not lost_document:
            return []

        ids = [str(r[ID_FIELD]) for r in records]
        documents = [self.document(r) for r in records]
        word_sim, char_sim = self._similarities(lost_document, documents)
        combined = np.clip(self.word_weight * word_sim + (1 - self.word_weight) * char_sim, 0, 1)
        combined = np.round(combined, SCORE_DECIMALS)

        order = sorted(range(len(ids)), key=lambda i: (-combined[i], ids[i]))[:top_k]
        lost_terms = set(lost_document.split())
        results = []
        for rank, i in enumerate(order, start=1):
            shared = self._rank_terms(lost_terms & set(documents[i].split()))
            results.append(
                TextMatch(
                    candidate_id=ids[i],
                    rank=rank,
                    similarity=float(combined[i]),
                    word_similarity=float(round(word_sim[i], SCORE_DECIMALS)),
                    char_similarity=float(round(char_sim[i], SCORE_DECIMALS)),
                    shared_terms=shared,
                    explanation=self._explain(
                        shared, float(word_sim[i]), float(char_sim[i]), float(combined[i]),
                        bool(documents[i]),
                    ),
                )
            )
        return results

    # ------------------------------------------------------------------ internals

    @staticmethod
    def _eligible(records: list[Mapping[str, Any]], lost_id: Any) -> list[Mapping[str, Any]]:
        seen: set[str] = set()
        eligible = []
        for record in records:
            candidate_id = record.get(ID_FIELD)
            if is_missing(candidate_id):
                raise ValueError(f"Every candidate needs a non-empty '{ID_FIELD}'")
            candidate_id = str(candidate_id)
            if candidate_id in seen:
                raise ValueError(f"Duplicate candidate ID: {candidate_id}")
            seen.add(candidate_id)
            if lost_id is not None and candidate_id == str(lost_id):
                continue
            eligible.append(record)
        return eligible

    def _similarities(self, query: str, documents: list[str]) -> tuple[np.ndarray, np.ndarray]:
        assert self._word is not None and self._char is not None
        # cosine_similarity returns 0 for all-zero rows (empty or entirely unseen text).
        word = cosine_similarity(self._word.transform(documents), self._word.transform([query]))
        char = cosine_similarity(self._char.transform(documents), self._char.transform([query]))
        return word.ravel(), char.ravel()

    def _rank_terms(self, terms: set[str]) -> tuple[str, ...]:
        """Shared terms ordered by IDF (rarest, most informative first), then alphabetically."""
        assert self._word is not None
        vocabulary, idf = self._word.vocabulary_, self._word.idf_
        max_idf = float(idf.max())
        weight = {t: float(idf[vocabulary[t]]) if t in vocabulary else max_idf for t in terms}
        ordered = sorted(terms, key=lambda t: (-weight[t], t))
        return tuple(ordered[:MAX_TERMS_IN_EXPLANATION])

    @staticmethod
    def _explain(
        shared: tuple[str, ...], word: float, char: float, combined: float, has_text: bool
    ) -> str:
        if not has_text:
            return "Candidate has no usable text"
        if combined <= 0:
            return "No text similarity"
        exact = (
            "Exact shared terms: " + ", ".join(shared) if shared else "No exact shared terms"
        )
        return (
            f"{exact}; general similarity {combined:.2f} "
            f"(word TF-IDF {word:.2f}, character n-gram {char:.2f})"
        )
