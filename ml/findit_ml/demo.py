"""Command-line demo for TextMatcher.

    python -m findit_ml.demo                       # fictional examples (default)
    python -m findit_ml.demo --reports data/processed/reports.csv --lost-id R000001
    add --json for machine-readable output
"""

from __future__ import annotations

import argparse
import json
import sys

from findit_ml.data import detect_label_columns, load_reports, split_lost_found
from findit_ml.matcher import TextMatch, TextMatcher

# Invented for the demo; not taken from the dataset.
FICTIONAL_FOUND = [
    {"report_id": "F1", "category": "Wallet", "color": "black", "brand": "",
     "description": "Black leather wallet found under a library table, has a student card"},
    {"report_id": "F2", "category": "Water Bottle", "color": "blue", "brand": "Milton",
     "description": "Blue steel bottle with a dent near the cap"},
    {"report_id": "F3", "category": "Umbrella", "color": "green", "brand": "",
     "description": "Green folding umbrella left on the bus stop bench"},
    {"report_id": "F4", "category": "Earphones", "color": "white", "brand": "Boat",
     "description": "White wireless earbuds in a scratched charging case"},
    {"report_id": "F5", "category": "Wallet", "color": "brown", "brand": "",
     "description": "Brown wallet, no cards inside"},
]
FICTIONAL_LOST = [
    {"report_id": "L1", "category": "Wallet", "color": "black", "brand": "",
     "description": "Lost my black leather wallet in the library, student ID card inside"},
    {"report_id": "L2", "category": "Earphones", "color": "white", "brand": "",
     "description": "Earbuds case has scratches, wireless"},
    {"report_id": "L3", "category": "Calculator", "color": "grey", "brand": "Casio",
     "description": "Scientific calculator with my initials"},
    {"report_id": "L4", "category": "Keys", "color": "silver", "brand": "",
     "description": "   "},
]


def _print_results(lost: dict, results: list[TextMatch]) -> None:
    print(f"\nLost {lost['report_id']} [{lost['category']}, {lost['color']}]: "
          f"\"{lost['description']}\"")
    if not results:
        print("  (no ranking: the lost report has no usable text, or there are no candidates)")
    for match in results:
        print(f"  #{match.rank} {match.candidate_id}  similarity {match.similarity:.3f}")
        print(f"     {match.explanation}")


def run_fictional(top_k: int, as_json: bool) -> None:
    matcher = TextMatcher().fit(FICTIONAL_LOST + FICTIONAL_FOUND)
    output = {}
    for lost in FICTIONAL_LOST:
        results = matcher.rank(lost, FICTIONAL_FOUND, top_k=top_k)
        output[lost["report_id"]] = [m.to_dict() for m in results]
        if not as_json:
            _print_results(lost, results)
    if as_json:
        print(json.dumps(output, indent=2))
    else:
        print("\nSimilarity ranks candidates relative to each other; it is not a probability.")


def run_dataset(path: str, lost_id: str, top_k: int, any_category: bool, as_json: bool) -> int:
    frame = load_reports(path)
    labels = detect_label_columns(frame.columns)
    lost_frame, found_frame = split_lost_found(frame)
    matches = lost_frame[lost_frame["report_id"] == lost_id]
    if matches.empty:
        print(f"{lost_id} is not an open lost report in {path}", file=sys.stderr)
        return 1
    lost = matches.iloc[0].to_dict()
    candidates = found_frame if any_category else found_frame[found_frame["category"] == lost["category"]]

    # Fit once on all report texts (lost and found). Unlabelled text only; see docs/PYTHON_MATCHER.md.
    matcher = TextMatcher().fit(frame)
    results = matcher.rank(lost, candidates, top_k=top_k)
    if as_json:
        print(json.dumps({"lost_id": lost_id, "candidates_considered": len(candidates),
                          "matches": [m.to_dict() for m in results]}, indent=2))
        return 0
    print(f"Fitted on {matcher.n_documents_fitted_} documents, vocabulary {matcher.vocabulary_size} "
          f"words. Label columns in dataset: {labels or 'none'}.")
    print(f"Candidates: {len(candidates)} open found reports"
          f"{'' if any_category else ' in category ' + repr(lost['category'])}.")
    _print_results(lost, results)
    print("\nSimilarity ranks candidates relative to each other; it is not a probability.")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="FindIt AI TF-IDF matcher demo")
    parser.add_argument("--reports", help="processed reports.csv (omit for fictional examples)")
    parser.add_argument("--lost-id", help="lost report to match (with --reports)")
    parser.add_argument("--top-k", type=int, default=3)
    parser.add_argument("--any-category", action="store_true",
                        help="compare against found reports of every category")
    parser.add_argument("--json", action="store_true", help="print JSON")
    args = parser.parse_args(argv)
    if args.top_k < 1:
        parser.error("--top-k must be at least 1")
    if args.reports:
        if not args.lost_id:
            parser.error("--lost-id is required with --reports")
        return run_dataset(args.reports, args.lost_id, args.top_k, args.any_category, args.json)
    run_fictional(args.top_k, args.json)
    return 0


if __name__ == "__main__":
    sys.exit(main())
