"""Automatic consistency checks for the synthetic evaluation labels (never edits them).

    python -m findit_ml.evaluation.review_labels --set-dir ../evaluation/synthetic_v1 \
        --rankings ../evaluation/results/synthetic_v1_rankings.csv \
        --judgements ../evaluation/synthetic_v1/review/assistant_judgements.csv \
        --out-dir ../evaluation/synthetic_v1/review

Writes:
  automatic_checks.csv  one row per finding of a deterministic check (kind = "automatic")
  pairs_reviewed.csv    original labels + automatic flags + assistant notes, side by side;
                        `original_label` is copied verbatim and `reviewed_label` differs only
                        where a recorded judgement proposes a change (none are applied to pairs.csv)
These are machine checks and AI-assistant notes; `human_verified` is always "no".
"""

from __future__ import annotations

import argparse
from collections import Counter
from pathlib import Path

import pandas as pd

from findit_ml.preprocess import informative_tokens

CHECKS = {
    "A01": ("report_id_exists", "error"),
    "A02": ("report_type_matches_column", "error"),
    "A03": ("duplicate_pair", "error"),
    "A04": ("contradictory_labels", "error"),
    "A05": ("one_positive_per_lost_and_found", "error"),
    "A06": ("has_true_match_consistent", "error"),
    "A07": ("group_consistent", "error"),
    "A08": ("every_lost_has_a_query_row", "error"),
    "A09": ("positive_with_structured_field_conflict", "info"),
    "A10": ("implicit_negative_same_category_and_colour", "warning"),
    "A11": ("implicit_negative_scores_like_a_positive", "warning"),
    "A12": ("low_information_lost_description", "info"),
}


def _group(source_ref: str) -> str:
    return source_ref.split(":", 1)[1] if source_ref.startswith("SYNTHETIC:") else ""


def run_checks(reports: pd.DataFrame, queries: pd.DataFrame, pairs: pd.DataFrame,
               rankings: pd.DataFrame | None) -> pd.DataFrame:
    rows: list[dict] = []

    def flag(check: str, lost: str, found: str, label: str, detail: str) -> None:
        name, severity = CHECKS[check]
        rows.append({"check_id": check, "check": name, "kind": "automatic", "severity": severity,
                     "lost_id": lost, "found_id": found, "current_label": label, "detail": detail})

    by_id = reports.set_index("report_id")
    lost_ids = set(reports.loc[reports.report_type == "LOST", "report_id"])
    found_ids = set(reports.loc[reports.report_type == "FOUND", "report_id"])
    query_by_lost = queries.set_index("lost_id")

    for p in pairs.itertuples():
        for rid, expected in ((p.lost_id, "LOST"), (p.found_id, "FOUND")):
            if rid not in by_id.index:
                flag("A01", p.lost_id, p.found_id, p.label, f"{rid} is not in reports.csv")
            elif by_id.loc[rid, "report_type"] != expected:
                flag("A02", p.lost_id, p.found_id, p.label, f"{rid} is {by_id.loc[rid, 'report_type']}, expected {expected}")

    pair_counts = Counter(zip(pairs.lost_id, pairs.found_id))
    for (lost, found), n in pair_counts.items():
        if n > 1:
            labels = set(pairs[(pairs.lost_id == lost) & (pairs.found_id == found)].label)
            flag("A04" if len(labels) > 1 else "A03", lost, found, "/".join(sorted(labels)),
                 f"pair listed {n} times with labels {sorted(labels)}")

    positives = pairs[pairs.label == "1"]
    for column, other in (("lost_id", "found_id"), ("found_id", "lost_id")):
        for rid, n in positives[column].value_counts().items():
            if n > 1:
                flag("A05", rid if column == "lost_id" else "", rid if column == "found_id" else "", "1",
                     f"{rid} is in {n} positive pairs")

    positive_lost = set(positives.lost_id)
    for q in queries.itertuples():
        expected = "1" if q.lost_id in positive_lost else "0"
        if q.has_true_match != expected:
            flag("A06", q.lost_id, "", "", f"queries.has_true_match={q.has_true_match} but positives say {expected}")
    for lost in lost_ids - set(queries.lost_id):
        flag("A08", lost, "", "", "lost report has no row in queries.csv")

    for p in pairs.itertuples():
        groups = {"pair": p.group_id,
                  "query": query_by_lost.loc[p.lost_id, "group_id"] if p.lost_id in query_by_lost.index else "?",
                  "lost source_ref": _group(by_id.loc[p.lost_id, "source_ref"]) if p.lost_id in by_id.index else "?",
                  "found source_ref": _group(by_id.loc[p.found_id, "source_ref"]) if p.found_id in by_id.index else "?"}
        if len(set(groups.values())) > 1:
            flag("A07", p.lost_id, p.found_id, p.label, f"group mismatch {groups}")

    for p in positives.itertuples():
        if p.lost_id not in by_id.index or p.found_id not in by_id.index:
            continue
        lost, found = by_id.loc[p.lost_id], by_id.loc[p.found_id]
        conflicts = [f"{c}: {lost[c]} vs {found[c]}" for c in ("category", "color", "location")
                     if lost[c].casefold() != found[c].casefold()]
        if conflicts:
            flag("A09", p.lost_id, p.found_id, "1", "; ".join(conflicts))

    labelled = set(pair_counts)
    for lost in sorted(lost_ids):
        for found in sorted(found_ids):
            if (lost, found) in labelled:
                continue
            a, b = by_id.loc[lost], by_id.loc[found]
            if a.category.casefold() == b.category.casefold() and a.color.casefold() == b.color.casefold():
                flag("A10", lost, found, "unlabelled (closed-world 0)",
                     f"same category ({a.category}) and colour ({a.color}); no explicit label or rationale")

    if rankings is not None and not positives.empty:
        scored = rankings.copy()
        scored["score"] = scored["score"].astype(float)
        key = list(zip(scored.lost_id, scored.found_id))
        scored["is_positive"] = [k in set(zip(positives.lost_id, positives.found_id)) for k in key]
        scored["is_labelled"] = [k in labelled for k in key]
        for (strategy, blocking), group in scored.groupby(["strategy", "category_blocking"]):
            floor = group.loc[group.is_positive, "score"].min()
            risky = group[~group.is_labelled & (group.score >= floor)]
            for r in risky.itertuples():
                flag("A11", r.lost_id, r.found_id, "unlabelled (closed-world 0)",
                     f"{strategy}/blocking {blocking}: score {r.score:.3f} >= lowest positive score {floor:.3f}")

    for q in queries.itertuples():
        tokens = informative_tokens(by_id.loc[q.lost_id, "description"]) if q.lost_id in by_id.index else []
        if len(tokens) <= 2:
            flag("A12", q.lost_id, "", "", f"only {len(tokens)} informative word(s): {tokens}")

    return pd.DataFrame(rows, columns=["check_id", "check", "kind", "severity", "lost_id", "found_id",
                                       "current_label", "detail"])


def build_reviewed(pairs: pd.DataFrame, checks: pd.DataFrame, judgements: pd.DataFrame) -> pd.DataFrame:
    """Side-by-side view. Includes unlabelled pairs that were flagged or judged."""
    keys = list(dict.fromkeys(
        list(zip(pairs.lost_id, pairs.found_id))
        + [(r.lost_id, r.found_id) for r in checks.itertuples() if r.found_id]
        + [(r.lost_id, r.found_id) for r in judgements.itertuples()]))
    original = {(r.lost_id, r.found_id): r.label for r in pairs.itertuples()}
    notes = {(r.lost_id, r.found_id): r for r in judgements.itertuples()}
    rows = []
    for lost, found in keys:
        flags = sorted(set(checks[(checks.lost_id == lost) & (checks.found_id == found)].check_id))
        lost_flags = sorted(set(checks[(checks.lost_id == lost) & (checks.found_id == "")].check_id))
        note = notes.get((lost, found))
        label = original.get((lost, found), "")
        rows.append({
            "lost_id": lost, "found_id": found,
            "original_label": label if label else "unlabelled (closed-world 0)",
            "reviewed_label": (note.proposed_label if note is not None and note.proposed_label
                               else (label if label else "unlabelled (closed-world 0)")),
            "automatic_flags": ";".join(flags + [f"{f}(lost)" for f in lost_flags]),
            "assistant_judgement": note.judgement if note is not None else "",
            "assistant_confidence": note.confidence if note is not None else "",
            "review_status": "needs_human_review" if (flags or lost_flags or note is not None) else "no_issue_found",
            "human_verified": "no",
        })
    return pd.DataFrame(rows)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="Automatic checks for synthetic evaluation labels")
    parser.add_argument("--set-dir", required=True)
    parser.add_argument("--rankings")
    parser.add_argument("--judgements")
    parser.add_argument("--out-dir", required=True)
    args = parser.parse_args(argv)
    read = lambda p: pd.read_csv(p, dtype=str, keep_default_na=False)  # noqa: E731
    set_dir, out = Path(args.set_dir), Path(args.out_dir)
    out.mkdir(parents=True, exist_ok=True)
    reports, queries, pairs = (read(set_dir / f) for f in ("reports.csv", "queries.csv", "pairs.csv"))
    rankings = read(args.rankings) if args.rankings else None
    checks = run_checks(reports, queries, pairs, rankings)
    checks.to_csv(out / "automatic_checks.csv", index=False)
    judgements = (read(args.judgements) if args.judgements else
                  pd.DataFrame(columns=["lost_id", "found_id", "judgement", "confidence", "proposed_label"]))
    reviewed = build_reviewed(pairs, checks, judgements)
    reviewed.to_csv(out / "pairs_reviewed.csv", index=False)
    print(checks.groupby(["check_id", "check", "severity"]).size().to_string())
    print(f"\n{len(reviewed)} pairs in pairs_reviewed.csv; "
          f"{(reviewed.review_status == 'needs_human_review').sum()} need human review")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
