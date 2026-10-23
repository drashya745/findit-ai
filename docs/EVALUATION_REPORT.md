# Evaluation report (Phase 6)

> **Every quality number below comes from a small, hand-written SYNTHETIC evaluation set.**
> It shows how the matcher behaves on designed cases. **It is not real-world accuracy**, and with
> 11 test queries the differences between configurations are mostly within noise.

Reproduce with `bash evaluation/run_evaluation.sh`, which takes about 50 s. **Where each number
comes from:**

| Numbers | File |
|---|---|
| Current tables §2–§5 | `evaluation/results/` (`metrics_ranked.csv`, `metrics_binary.csv`, `per_query.csv`, `runtime.csv`, `summary.md`) |
| Pre-fix ("before") figures | `evaluation/results/history/run2_old_request_format/` |
| Earlier-run variation | `evaluation/results/history/run1_old_request_format/` and `run3_description_only/` |
| Uncertain-label sensitivity | `evaluation/synthetic_v1/review/sensitivity_uncertain_labels.csv` |

Every decimal number in this report was checked automatically against those files during the
release audit (2026-10-23).

## 1. Ground truth: what exists and what was used

| Source | Trustworthy match labels? | Used for |
|---|---|---|
| `data/raw/…30k.csv` (real dataset) | **No.** No label/match/pair column (checked by `findit_ml.data.detect_label_columns`). | **Runtime only** |
| Row-adjacency pattern in the dataset (row 2k-1 lost, 2k found; 80% share all structured fields) | **No.** It's an undocumented artefact of the generator. Its "positives" are defined by the same fields the matcher scores, so evaluating on it would be circular and would amount to assuming similar records are matches. | Not used |
| `evaluation/synthetic_v1/` | Synthetic labels, written by hand with a rationale per pair. Machine-checked, with AI-assistant notes ([review](../evaluation/synthetic_v1/review/REVIEW_REPORT.md)); **not independently human-reviewed** | **Quality metrics** |

**The synthetic set** has 23 lost and 32 found reports with its own ID range (`R9xxxxx`) and
`source_ref = SYNTHETIC:*`. It is kept separate from `data/` and never merged.
- 18 lost reports have exactly one true match, and 5 have none.
- 11 explicit hard negatives (same category and colour, contradicting details) plus 3 noise items.
- Difficulty tags: easy, synonym, typo, near colour, cross-category, location mismatch/zone,
  vague text, almost no text, fine detail, and no match.

**Leakage control.**
- **Splits:** dev has 9 groups (7 with a match) and test has 14 (11 with a match), split by
  group. A lost report, its true match and its hard negatives never cross splits.
- **Tuning:** the decision threshold is chosen on dev (maximum F1) and applied unchanged to test.
  The weights were not tuned (agreed baseline 0.60/0.20/0.10/0.10).
- **TF-IDF:** fitted on the set's unlabelled text, the same way the app fits on its repository.
  No labels or IDs are used. This is mild transductive exposure to test vocabulary (see
  `docs/PYTHON_MATCHER.md`).

## 2. Ranked retrieval: test split, 11 queries with a true match

All candidates are scored. P@K = relevant in top K / K. With one true match per query, P@K can't
exceed 1/K. Queries with no true match make P@K and R@K undefined; they're counted and analysed
separately in §4, never treated as 0 or 1.

| Strategy | Category blocking | P@1 = R@1 | R@3 | R@5 | MRR |
|---|---|---|---|---|---|
| C++ keyword baseline | on | 0.818 (9/11) | 0.909 (10/11) | 0.909 | 0.864 |
| C++ keyword baseline | off | 0.909 (10/11) | 1.000 (11/11) | 1.000 | 0.955 |
| Python TF-IDF (integrated) | on | 0.818 (9/11) | 0.909 (10/11) | 0.909 | 0.864 |
| Python TF-IDF (integrated) | off | 0.909 (10/11) | 1.000 (11/11) | 1.000 | 0.955 |

Rank of the true match, across all 18 queries with one:

| Case (split) | Keyword on/off | TF-IDF on/off | Why |
|---|---|---|---|
| R900003 typo (dev) | 2 / 2 | 2 / 2 | A hard negative at the **exact** location beats the true match in the same zone. Location (0.20) outweighs text differences |
| R900006 location mismatch (test) | 2 / 2 | 2 / 2 | Same pattern: the true umbrella was handed in at another zone |
| R900005, R900016 cross-category | not scored / 1 | not scored / 1 | Blocking removes the true match (Bag vs Laptop Bag, Tumbler vs Flask) |
| other 14 | 1 | 1 | |

**Finding:** on this set the two text engines produce **identical rankings**. The structured
components (location, category, colour) decide the hard cases for both. Category blocking costs
recall whenever a reporter files an item under a related category.

## 3. Binary decision: "match if score ≥ threshold", pair level

Pairs removed by blocking count as predicted non-matches. The threshold is chosen on dev only.

| Strategy | Blocking | Threshold (dev) | Test TP / FP / FN | Test precision | Test recall | Test F1 |
|---|---|---|---|---|---|---|
| Keyword | on | 0.5714 | 9 / 4 / 2 | 0.692 (9/13) | 0.818 (9/11) | 0.750 |
| Keyword | off | 0.5714 | 10 / 4 / 1 | 0.714 (10/14) | 0.909 (10/11) | 0.800 |
| TF-IDF | on | 0.6457 | 9 / 2 / 2 | 0.818 (9/11) | 0.818 (9/11) | 0.818 |
| TF-IDF | off | 0.6457 | 10 / 2 / 1 | 0.833 (10/12) | 0.909 (10/11) | 0.870 |

TF-IDF's test F1 is higher, but its **dev** F1 is lower (0.667 / 0.769 vs keyword 0.706 / 0.778).
These rows come from the description-only Python input (§7). Before that fix, the order was
reversed: TF-IDF dev F1 0.833 / 0.923, test 0.700 / 0.762. The thresholds come from only 7 dev
positives, and excluding a single questionable label moves TF-IDF test F1 to 0.783 / 0.833 (see
the label review, §4). **No configuration is shown to be better**: the differences are one or two
pairs out of 11.

## 4. Lost reports with no true match

Top-1 score per configuration (from `summary.md`):

| Lost | Blocking on: keyword / TF-IDF | Blocking off: keyword / TF-IDF |
|---|---|---|
| R900019 green bottle | 0.600 / 0.591 | 0.600 / 0.591 |
| R900020 hoodie | no candidates / no candidates | 0.309 / 0.292 |
| R900021 Samsung case | 0.700 / 0.640 | same |
| R900022 golf umbrella | 0.667 / 0.643 | same |
| R900023 pencil box | 0.880 / 0.833 | same |

Hard negatives score 0.59–0.88, which overlaps the true matches (0.40–0.95). **Scores are not
calibrated**, so no threshold separates "no match" from "match" reliably. R900020 shows the clean
**empty-candidate** path: with blocking on, no hoodie exists, so the result is "no eligible found
reports" rather than a wrong suggestion.

## 5. Runtime

Machine: MacBook Air (M1, 2020; `MacBookAir10,1`), Apple M1, 8 logical CPUs, 8 GB RAM, macOS
27.0.1. Build: Apple clang 21, **Release**. Python 3.14.3 with scikit-learn 1.9.1. Single run,
other applications not controlled, wall-clock time via `std::chrono::steady_clock`.

| Dataset | Strategy | Blocking | Queries | Candidates per query | Fit (once) | Mean / p95 per query |
|---|---|---|---|---|---|---|
| Real 30k, random 200 lost (seed 42) | keyword | on | 200 | 483 | — | 1.71 / 1.95 ms |
| Real 30k, random 200 lost (seed 42) | TF-IDF | on | 200 | 483 | 2.04 s | 28.9 / 32.6 ms |
| Real 30k, random 20 lost (seed 42) | keyword | off | 20 | 15,000 | — | 34.1 / 35.3 ms |
| Real 30k, random 20 lost (seed 42) | TF-IDF | off | 20 | 15,000 | 2.07 s | 917 / 983 ms |
| Synthetic | TF-IDF | on/off | 23 | 1.6 / 32 | 0.96 / 0.88 s | 1.4 / 3.7 ms |

These values are from `evaluation/results/runtime.csv`, the latest run, made after the switch to
description-only Python input. Two runs with that version differed by up to 9% (e.g. 30.4 vs
28.9 ms; 840 vs 917 ms). The requests are smaller now, so TF-IDF got faster: two earlier runs
with the old request format measured 39 ms and 1.13 s per query and a 2.6 s fit. Keyword timings
stayed within 11% across all runs.

TF-IDF's fit time includes starting Python and importing scikit-learn. A query includes JSON
encoding, the pipe round trip, Python scoring and validation. With blocking on (the default),
the integrated matcher answers in about 30 ms after a one-off ~2 s start. Without blocking it
needs about 0.9 s per query, mostly from serialising 15,000 candidates.

## 6. What can and cannot be concluded

**Can be concluded:**
- The integrated pipeline runs end to end, ranks obvious matches first on designed cases, and
  handles empty candidate sets and missing text.
- The structured components dominate ranking under the agreed weights.
- Category blocking removes cross-category true matches.
- Scores don't separate matches from hard negatives.
- Interactive latency is acceptable with blocking on.

**Cannot be concluded:**
- Any real-world precision or recall: there are no real labels.
- Whether TF-IDF beats the keyword baseline: the rankings are identical here and the binary
  differences are within noise on 11 positives.
- How the matcher performs on real campus reports, which are messier, multilingual and
  photo-dependent, and often have no match at all.

## 7. Limitations

- **Synthetic labels:** drafted with an AI coding assistant during development, with only 23
  queries. The automatic checks found no structural errors, but **no independent human review has
  happened**. The cases were designed with knowledge of how the matcher works, which tends to make
  results look better.
- **Imperfect ground truth:** the closed-world assumption treats every unlabelled pair as a
  non-match. The label review flags 3 pairs as uncertain: the G07 negative and two black-wallet
  pairs whose text doesn't contradict a match. Excluding them leaves the ranked metrics unchanged
  and moves TF-IDF binary F1 by about 0.04.
- **Calibration:** scores are weighted sums for ranking, not probabilities. A threshold chosen on
  7 dev positives won't transfer reliably.
- **Double counting (fixed):** earlier, the Python document appended category and colour, which
  C++ also scores. R900008, whose description shares no words with its match, got text 0.43
  (total 0.66). Python now receives only the description, so R900008's text component is 0 and its
  total is 0.40, the structured part alone. This is checked by
  `findit_integration_tests: text score ignores category, colour and location`.
- **Offline vs real use:** real users report late and in vague words, pick categories
  inconsistently, and often never see a match. Real use needs a feedback log of confirmed matches
  to build trustworthy labels over time.
