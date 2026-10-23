# SYNTHETIC evaluation set v1

> **Synthetic.** Every report and label here was written by hand for evaluation. Nothing is taken
> from or merged into the real dataset (`data/raw/`, `data/processed/`). IDs are in a separate
> range: `R900001…` (lost) and `R950001…` (found). Every `source_ref` starts with `SYNTHETIC:`.
> Results on this set show behaviour on designed cases. **They are not real-world accuracy.**

## Files

| File | Content |
|---|---|
| `reports.csv` | 23 lost and 32 found reports, canonical schema (loadable by the C++ app) |
| `users.csv` | 4 fictional reporters |
| `queries.csv` | One row per lost report: group, dev/test split, whether a true match exists, difficulty, notes |
| `pairs.csv` | 29 labelled pairs with a written rationale: 18 positives and 11 hard negatives |

## Ground-truth rules

- Each of G01–G18 has exactly one true found report (`label = 1`). N01–N05 have none.
- **Closed world:** any (lost, found) pair not labelled 1 is a non-match, by construction. Explicit
  hard negatives (`label = 0`) are listed so reviewers can check the most confusable pairs.
- Each found report belongs to one group (`source_ref`). `NOISE` reports belong to no case.

## Leakage control

- Splits are by **group**. A lost report, its true match and its hard negatives are all in the same
  split (dev: 9 groups, test: 14).
- The binary threshold is chosen on **dev only** and then applied unchanged to test.
- Matching weights are the agreed baseline and were not tuned on this set. TF-IDF is fitted on the
  set's unlabelled report text (as the app does on its repository); labels and IDs are never used.

## Review

Read `pairs.csv` rationales against `reports.csv`. If you disagree with a label, change it and
re-run `evaluation/run_evaluation.sh`. A second independent reviewer would make this set more
trustworthy.
