# Baseline matching (Phase 3)

`KeywordBaselineStrategy` implements `SimilarityStrategy`; `MatchEngine` filters candidates
and ranks results. A Python strategy can replace it later without changing either class.

## Candidate eligibility (MatchEngine)

Excluded before scoring: null candidates, the lost report itself (same ID), found reports that
are not `OPEN`, and, with `category_blocking = true` (the default), found reports of a different
category. Status filtering is supported because status is managed by the app (everything
imported starts `OPEN`). **The date filter is off** (`date_window_days = 0`): in this dataset
the found date precedes the lost date in about half of the generated pairs, so the source does
not support a date rule. The filter is implemented and tested for future data.

## Components (each in [0, 1])

| Component | Rule |
|---|---|
| text | Dice overlap of description keywords: `2·|A∩B| / (|A|+|B|)`. Keywords = lowercase alphanumeric tokens, `0`→`o` inside words (`l0st`), minus stopwords and report boilerplate (`lost`, `found`, `pls`, `someone`, …) |
| category | 1 same (case-insensitive); 0.5 same `category_group.*`; else 0 |
| colour | 1 same; 0.5 same `colour_group.*`; else 0 |
| location | 1 same; 0.5 same `zone.*`; else 0 |

`score = (0.60·text + 0.20·location + 0.10·category + 0.10·colour) / sum(weights)`. Weights are
in `config/matching.conf` and validated (non-negative, not all zero). Dividing by the sum keeps
the score in [0, 1] for any weights. **The score is a relative ranking value, not a probability.**
Results are sorted by score descending, ties by found ID ascending, then cut to top K.

## Missing fields

A missing value never counts as agreement. If either side's category/colour/location is empty,
`unknown` or `none`, or a description has no usable keywords, that component scores **0**. The
explanation says so (e.g. *"Colour missing on found report (not counted as a match)"*). Weights
are not redistributed, so a sparse report cannot outrank a complete one by having less to
disagree with. Brand is not part of the Phase 3 formula.

## Example (actual output on the processed dataset)

```
Lost: R000001  LOST   OPEN     Keychain        yellow   Garden            2025-09-23
      "Can't find my keychain (yellow), has a sticker."

#1  R000002  score 0.88  [category=1.00, colour=1.00, location=1.00, text=0.80]
    - Shared keywords (2 of 3 lost / 2 found, overlap 0.80): keychain, yellow
    - Same category: Keychain
    - Same colour: yellow
    - Same location: Garden
#2  R026372  score 0.88  [...same components...]
#3  R006526  score 0.68  [category=1.00, colour=1.00, location=0.00, text=0.80]
    - Different location: Garden vs Seminar Hall
```

## Limitations of keyword matching

- **Ties are common.** Descriptions come from a few templates, so many candidates share the same
  keywords (above, #1 and #2 tie at 0.88; #1 wins only on ID order). The baseline cannot tell them
  apart.
- **Redundant signals.** Most descriptions repeat the item name and colour, so the text score
  largely double-counts category and colour rather than adding independent evidence.
- **No meaning, only spelling.** No synonyms (`purse`≠`wallet`), no stemming (`scratch`≠`scratches`),
  and no typo tolerance beyond the `0`→`o` fix (`scraztches` won't match).
- **Hand-made groups.** Zones and colour/category groups are design assumptions in a config
  file, not facts from the data.
- **Brand and dates unused.** Brand is ignored because the dataset assigns it independently of
  the item. Dates are unused because the data doesn't support a date rule.
- **Not calibrated or evaluated.** There are no genuine match labels; any accuracy claim would
  need the evaluation phase.
