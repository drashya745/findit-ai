"""Compute evaluation metrics from findit_eval rankings and the labelled synthetic set.

    python -m findit_ml.evaluation.run_eval \
        --queries evaluation/synthetic_v1/queries.csv --pairs evaluation/synthetic_v1/pairs.csv \
        --reports evaluation/synthetic_v1/reports.csv \
        --rankings evaluation/results/synthetic_v1_rankings.csv --out-dir evaluation/results

Writes metrics_ranked.csv, metrics_binary.csv, per_query.csv and summary.md. Undefined values
are written as the word "undefined".
"""

from __future__ import annotations

import argparse
from pathlib import Path

import pandas as pd

from findit_ml.evaluation.metrics import (
    binary_counts,
    choose_threshold,
    mean_defined,
    precision_at_k,
    recall_at_k,
    reciprocal_rank,
)

K_VALUES = (1, 3, 5)
UNDEFINED = "undefined"


def _fmt(value: float | None, digits: int = 3) -> str:
    return UNDEFINED if value is None else f"{value:.{digits}f}"


def load_inputs(queries_path, pairs_path, reports_path, rankings_path):
    read = lambda p: pd.read_csv(p, dtype=str, keep_default_na=False)  # noqa: E731
    queries, pairs, reports, rankings = map(read, (queries_path, pairs_path, reports_path, rankings_path))
    rankings["rank"] = rankings["rank"].astype(int)
    rankings["score"] = rankings["score"].astype(float)
    if (pairs["label"] == "1").groupby(pairs["lost_id"]).sum().gt(1).any():
        raise ValueError("this evaluation assumes at most one true match per lost report")
    unknown = set(pairs["lost_id"]) - set(queries["lost_id"])
    if unknown:
        raise ValueError(f"pairs reference unknown lost reports: {sorted(unknown)}")
    return queries, pairs, reports, rankings


def evaluate(queries, pairs, reports, rankings):
    positives = {(r.lost_id, r.found_id) for r in pairs.itertuples() if r.label == "1"}
    relevant_by_lost = {q: {f for (l, f) in positives if l == q} for q in queries["lost_id"]}
    found_ids = sorted(reports.loc[reports["report_type"] == "FOUND", "report_id"])
    split_of = dict(zip(queries["lost_id"], queries["split"]))

    ranked_rows, binary_rows, per_query_rows = [], [], []
    configs = rankings[["strategy", "category_blocking"]].drop_duplicates().itertuples(index=False)
    for strategy, blocking in configs:
        subset = rankings[(rankings["strategy"] == strategy) & (rankings["category_blocking"] == blocking)]
        ranked = {lost: group.sort_values("rank")["found_id"].tolist()
                  for lost, group in subset.groupby("lost_id")}
        scores = {(r.lost_id, r.found_id): r.score for r in subset.itertuples()}

        # Per query.
        for q in queries.itertuples():
            ids = ranked.get(q.lost_id, [])
            relevant = relevant_by_lost[q.lost_id]
            true_id = next(iter(relevant), "")
            per_query_rows.append({
                "strategy": strategy, "category_blocking": blocking, "split": q.split,
                "lost_id": q.lost_id, "group_id": q.group_id, "difficulty": q.difficulty,
                "has_true_match": q.has_true_match, "candidates_scored": len(ids),
                "true_found_id": true_id,
                "true_rank": (ids.index(true_id) + 1) if true_id in ids else (
                    "not_scored" if true_id else ""),
                "true_score": _fmt(scores.get((q.lost_id, true_id)), 4) if true_id in ids else "",
                "top1_found_id": ids[0] if ids else "",
                "top1_score": _fmt(scores[(q.lost_id, ids[0])], 4) if ids else "",
                "top1_correct": (ids[0] == true_id) if ids and true_id else "",
            })

        # Ranked retrieval per split.
        for split in ("dev", "test", "all"):
            lost_ids = [q for q in queries["lost_id"] if split == "all" or split_of[q] == split]
            with_match = [q for q in lost_ids if relevant_by_lost[q]]
            for k in K_VALUES:
                p, n_p, undef_p = mean_defined(
                    precision_at_k(ranked.get(q, []), relevant_by_lost[q], k) for q in lost_ids)
                r, _, _ = mean_defined(
                    recall_at_k(ranked.get(q, []), relevant_by_lost[q], k) for q in lost_ids)
                mrr, _, _ = mean_defined(
                    reciprocal_rank(ranked.get(q, []), relevant_by_lost[q]) for q in lost_ids)
                ranked_rows.append({
                    "strategy": strategy, "category_blocking": blocking, "split": split, "k": k,
                    "queries": len(lost_ids), "queries_with_true_match": len(with_match),
                    "queries_undefined_no_true_match": undef_p,
                    "hits": sum(1 for q in with_match if relevant_by_lost[q] & set(ranked.get(q, [])[:k])),
                    "precision_at_k": _fmt(p), "recall_at_k": _fmt(r), "mrr": _fmt(mrr),
                })

        # Binary decisions: threshold chosen on dev, applied unchanged to test.
        def universe(split):
            return {(q, f) for q in queries["lost_id"] if split_of[q] == split for f in found_ids}

        dev_pairs, test_pairs = universe("dev"), universe("test")
        threshold = choose_threshold(scores, positives & dev_pairs, dev_pairs)
        for split, pairs_in_split in (("dev", dev_pairs), ("test", test_pairs)):
            row = {"strategy": strategy, "category_blocking": blocking, "split": split,
                   "threshold_chosen_on": "dev", "threshold": _fmt(threshold, 4)}
            if threshold is None:
                row.update(tp="", fp="", fn="", tn="", precision=UNDEFINED, recall=UNDEFINED,
                           f1=UNDEFINED, note="no threshold: F1 undefined on dev")
            else:
                c = binary_counts(scores, positives & pairs_in_split, pairs_in_split, threshold)
                row.update(tp=c.tp, fp=c.fp, fn=c.fn, tn=c.tn, precision=str(c.precision),
                           recall=str(c.recall), f1=_fmt(c.f1), note="")
            binary_rows.append(row)

        # Abstention: how confidently does each config rank something for lost reports that
        # have no true match? Reported per query in per_query.csv.

    return pd.DataFrame(ranked_rows), pd.DataFrame(binary_rows), pd.DataFrame(per_query_rows)


def write_summary(path: Path, ranked, binary, per_query) -> None:
    lines = [
        "# Evaluation summary (generated by findit_ml.evaluation.run_eval)",
        "",
        "> SYNTHETIC evaluation set (evaluation/synthetic_v1). Not real-world accuracy.",
        "",
        "## Ranked retrieval (test split)",
        "",
        "| strategy | blocking | K | P@K | R@K | MRR | hits | queries with match |",
        "|---|---|---|---|---|---|---|---|",
    ]
    for r in ranked[ranked["split"] == "test"].itertuples():
        lines.append(f"| {r.strategy} | {r.category_blocking} | {r.k} | {r.precision_at_k} | "
                     f"{r.recall_at_k} | {r.mrr} | {r.hits} | {r.queries_with_true_match} |")
    lines += ["", "## Binary decision at dev-chosen threshold", "",
              "| strategy | blocking | split | threshold | TP | FP | FN | precision | recall | F1 |",
              "|---|---|---|---|---|---|---|---|---|---|"]
    for b in binary.itertuples():
        lines.append(f"| {b.strategy} | {b.category_blocking} | {b.split} | {b.threshold} | {b.tp} | "
                     f"{b.fp} | {b.fn} | {b.precision} | {b.recall} | {b.f1} |")
    no_match = per_query[per_query["has_true_match"] == "0"]
    lines += ["", "## Lost reports with no true match (top-1 score; lower is better)", "",
              "| strategy | blocking | lost_id | candidates | top1 | top1_score |", "|---|---|---|---|---|---|"]
    for q in no_match.itertuples():
        lines.append(f"| {q.strategy} | {q.category_blocking} | {q.lost_id} | {q.candidates_scored} | "
                     f"{q.top1_found_id or '-'} | {q.top1_score or '-'} |")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    for name in ("queries", "pairs", "reports", "rankings", "out-dir"):
        parser.add_argument(f"--{name}", required=True)
    args = parser.parse_args(argv)
    out = Path(args.out_dir)
    out.mkdir(parents=True, exist_ok=True)
    ranked, binary, per_query = evaluate(*load_inputs(args.queries, args.pairs, args.reports, args.rankings))
    ranked.to_csv(out / "metrics_ranked.csv", index=False)
    binary.to_csv(out / "metrics_binary.csv", index=False)
    per_query.to_csv(out / "per_query.csv", index=False)
    write_summary(out / "summary.md", ranked, binary, per_query)
    print((out / "summary.md").read_text(encoding="utf-8"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
