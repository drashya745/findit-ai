# Label review: synthetic_v1

> **Status: NOT independently human-reviewed.** This review has two kinds of evidence:
> **automatic checks** (deterministic code) and **AI-assistant judgements** (written by the AI
> coding assistant that also drafted the labels, so they are *not* independent). Every row in
> `pairs_reviewed.csv` has `human_verified = no`. A person should go through §4 and record their
> decisions.

## 1. What was reviewed, and what was not changed

| File | SHA-256 | Changed? |
|---|---|---|
| `evaluation/synthetic_v1/pairs.csv` (original labels) | `a209ab08b26e6e3cc9b633ce4f5c499e78cf9f604da4d922b2930221f1cb96b2` | **No**, still used by `run_evaluation.sh` |
| `evaluation/synthetic_v1/queries.csv`, `reports.csv` | unchanged | No |

New files in `evaluation/synthetic_v1/review/`:

| File | Kind | Content |
|---|---|---|
| `automatic_checks.csv` | automatic | One row per finding of checks A01–A12 |
| `assistant_judgements.csv` | AI-assistant | A note, confidence and optional proposed label per flagged pair |
| `pairs_reviewed.csv` | merged | 37 pairs: `original_label` (verbatim), `reviewed_label` (differs only where a change is proposed), flags, notes, `human_verified` |

Regenerate the review with:
```sh
cd ml && ../.venv/bin/python -m findit_ml.evaluation.review_labels --set-dir ../evaluation/synthetic_v1 \
  --rankings ../evaluation/results/synthetic_v1_rankings.csv \
  --judgements ../evaluation/synthetic_v1/review/assistant_judgements.csv \
  --out-dir ../evaluation/synthetic_v1/review
```

## 2. Automatic checks

| Check | Meaning | Findings |
|---|---|---|
| A01 / A02 | IDs exist; lost_id is LOST and found_id is FOUND | **0** |
| A03 / A04 | Duplicate pairs; contradictory labels for one pair | **0** |
| A05 | At most one positive per lost and per found report | **0** |
| A06 | `queries.has_true_match` agrees with the positives | **0** |
| A07 | Group IDs agree across pairs, queries and `source_ref` | **0** |
| A08 | Every lost report has a query row | **0** |
| A09 | Positive whose category, colour or location differ | 7 (info) |
| A10 | Unlabelled pair with the same category and colour (negative only by the closed-world rule) | 2 (warning) |
| A11 | Unlabelled pair scoring ≥ the lowest positive score, in any configuration | 11 rows / 6 pairs (warning) |
| A12 | Lost description with ≤ 2 informative words | 2 (info) |

There were **no structural errors** (A01–A08). The warnings are judgement questions; they don't
prove a label wrong.

## 3. AI-assistant judgements (not independent)

**Keep as labelled (high confidence)** — 6 positives with field conflicts (A09):
- G03: typo, same zone
- G04: silver vs grey; Casio and label RK match
- G05: Bag vs Laptop Bag; the lost text says "laptop bag"
- G09: Adidas with stripes
- G15: Dell, frayed cable
- G16: Tumbler vs Flask; the found text says "tumbler"

**Clear negatives (high confidence)** — all 6 A11 pairs. Each was flagged only because the lowest
positive score is low (0.40, R900008).

**Questionable: needs a human decision**

| Pair | Original | Proposed | Why |
|---|---|---|---|
| R900007 → R950011 (G07) | 0 | **uncertain** (low confidence) | The lost text is only "Lost my earphones". Nothing in it contradicts the found "white earphones with blue ear tips"; only Hostel Block A vs B differs. The negative is a definition, not evidence. |
| R900001 → R950003 (G01 lost vs G02 found) | closed-world 0 | **uncertain** (medium) | Both are black wallets. "Zip coin pocket" doesn't contradict "leather, student ID"; only the location differs. |
| R900002 → R950001 (G02 lost vs G01 found) | closed-world 0 | **uncertain** (medium) | The mirror case: G01 and G02 are mutually consistent across groups. |
| R900006 → R950008 (G06) | 1 | keep 1 (medium) | Linked only by the "wooden hook handle"; Library vs Main Gate is unconstrained |
| R900007 → R950010 (G07) | 1 | keep 1 (medium) | Only the hostel block links them |
| R900008 → R950012 (G08) | 1 | keep 1 (medium) | Evidence is category + colour + location only. `queries.csv` tags it `no_text`, but "gym" is an informative word (A12); a better tag would be `location_only_text` |

## 4. Effect on reported results (measured, not assumed)

The 3 "uncertain" pairs all rank **below** their lost report's true match, so **ranked metrics
(P@K, R@K, MRR) are unaffected**. For binary metrics, the dev-chosen threshold was recomputed with
the 3 pairs excluded:

| Configuration | Test F1 with original labels | Test F1 with the 3 pairs excluded |
|---|---|---|
| Keyword, blocking on / off | 0.750 / 0.800 | 0.750 / 0.800 (no change) |
| TF-IDF, blocking on / off | 0.818 / 0.870 | **0.783 / 0.833** |

These numbers are in `sensitivity_uncertain_labels.csv`. Regenerate it with
`cd ml && ../.venv/bin/python -m findit_ml.evaluation.sensitivity --set-dir ../evaluation/synthetic_v1 --rankings ../evaluation/results/synthetic_v1_rankings.csv --reviewed ../evaluation/synthetic_v1/review/pairs_reviewed.csv --out ../evaluation/synthetic_v1/review/sensitivity_uncertain_labels.csv`.

For TF-IDF the G07 negative (score 0.617) sits between two candidate thresholds. Excluding it moves
the dev threshold from 0.6457 to 0.6020, which adds one false positive on test. **A single
questionable label moves a reported F1 by about 0.04**, more evidence that the binary comparison
is within noise.

## 5. For the human reviewer

1. Open **`HUMAN_REVIEW_CHECKLIST.csv`**: the 18 flagged pairs with both reports' text side by
   side, the current label, the automatic flags, a question per pair, and *blank*
   `human_decision`, `reviewer`, `review_date` and `reason` columns. For each row, decide
   positive (1), negative (0) or uncertain, and say why.
2. Save the completed copy as `review/human_decisions.csv`. Don't edit `pairs.csv` in place.
3. If labels change, create `synthetic_v2/` with the changes and a changelog, then re-run the
   evaluation on it. Report v1 and v2 side by side.

## 6. Change log

| Date | Change | By |
|---|---|---|
| 2026-10-23 | Automatic checks added; AI-assistant judgements recorded; 3 labels **proposed** as uncertain; **no labels changed** in `pairs.csv` | AI coding assistant (not independent) |
