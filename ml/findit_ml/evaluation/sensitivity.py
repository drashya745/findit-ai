"""Sensitivity of binary metrics to the labels flagged "uncertain" in the label review.

    python -m findit_ml.evaluation.sensitivity --set-dir ../evaluation/synthetic_v1 \
        --rankings ../evaluation/results/synthetic_v1_rankings.csv \
        --reviewed ../evaluation/synthetic_v1/review/pairs_reviewed.csv \
        --out ../evaluation/synthetic_v1/review/sensitivity_uncertain_labels.csv

For each configuration, the dev-chosen threshold and the test metrics are computed twice: with
the original labels, and with every pair whose reviewed_label is "uncertain" removed from the
judged pairs. Labels are never changed; this only measures how much the results depend on them.
"""

from __future__ import annotations

import argparse

import pandas as pd

from findit_ml.evaluation.metrics import binary_counts, choose_threshold


def sensitivity(queries, pairs, reports, rankings, uncertain: set[tuple[str, str]]) -> pd.DataFrame:
    positives = {(r.lost_id, r.found_id) for r in pairs.itertuples() if r.label == "1"}
    found_ids = list(reports.loc[reports.report_type == "FOUND", "report_id"])
    split_of = dict(zip(queries.lost_id, queries.split))
    rows = []
    for (strategy, blocking), group in rankings.groupby(["strategy", "category_blocking"]):
        scores = {(r.lost_id, r.found_id): float(r.score) for r in group.itertuples()}
        for variant, excluded in (("original_labels", set()), ("uncertain_excluded", uncertain)):
            def universe(split):
                return {(l, f) for l in queries.lost_id if split_of[l] == split for f in found_ids} - excluded
            dev, test = universe("dev"), universe("test")
            threshold = choose_threshold(scores, positives & dev, dev)
            row = {"strategy": strategy, "category_blocking": blocking, "variant": variant,
                   "pairs_excluded": len(excluded), "dev_threshold": threshold}
            if threshold is not None:
                c = binary_counts(scores, positives & test, test, threshold)
                row.update(test_tp=c.tp, test_fp=c.fp, test_fn=c.fn, test_precision=c.precision.value,
                           test_recall=c.recall.value, test_f1=c.f1)
            rows.append(row)
    return pd.DataFrame(rows)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    for name in ("set-dir", "rankings", "reviewed", "out"):
        parser.add_argument(f"--{name}", required=True)
    args = parser.parse_args(argv)
    read = lambda p: pd.read_csv(p, dtype=str, keep_default_na=False)  # noqa: E731
    queries, pairs, reports = (read(f"{args.set_dir}/{f}") for f in ("queries.csv", "pairs.csv", "reports.csv"))
    reviewed = read(args.reviewed)
    uncertain = {(r.lost_id, r.found_id) for r in reviewed.itertuples() if r.reviewed_label == "uncertain"}
    result = sensitivity(queries, pairs, reports, read(args.rankings), uncertain)
    result.to_csv(args.out, index=False, float_format="%.4f")
    print(result.to_string(index=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
