# Python TF-IDF text matcher (Phase 4)

Package: `ml/findit_ml/`
- `preprocess.py`: normalization and documents
- `matcher.py`: `TextMatcher`
- `data.py`: loads `data/processed/reports.csv`
- `demo.py`: command-line demo

## Setup (macOS, Apple Silicon)

```sh
cd oops_project                                # your copy of the project folder
python3 -m venv .venv                          # once; Homebrew Python 3.14 was used
.venv/bin/pip install -r ml/requirements.txt   # numpy, scipy, pandas, scikit-learn, pytest
.venv/bin/python -m pytest                     # runs ml/tests (config in pytest.ini)
```

## Demo

```sh
cd ml
../.venv/bin/python -m findit_ml.demo                  # fictional examples
../.venv/bin/python -m findit_ml.demo --reports ../data/processed/reports.csv --lost-id R000001 --top-k 4
../.venv/bin/python -m findit_ml.demo --json           # structured output
```

Actual output (fictional data):
```
Lost L1 [Wallet, black]: "Lost my black leather wallet in the library, student ID card inside"
  #1 F1  similarity 0.793
     Exact shared terms: black, card, leather, library, student, wallet; general similarity 0.79 (word TF-IDF 0.76, character n-gram 0.84)
  #2 F5  similarity 0.225
     Exact shared terms: wallet; general similarity 0.22 (word TF-IDF 0.18, character n-gram 0.28)
Lost L4 [Keys, silver]: "   "
  (no ranking: the lost report has no usable text, or there are no candidates)
```
Actual output (real dataset, R000001):
```
Fitted on 30000 documents, vocabulary 1665 words. Label columns in dataset: none.
Candidates: 492 open found reports in category 'Keychain'.
  #1 R000002  similarity 0.904   Exact shared terms: keychain, milton, yellow; ...
  #2 R021412  similarity 0.904   Exact shared terms: keychain, milton, yellow; ...
```
Each result has these fields: `candidate_id`, `rank`, `similarity`, `word_similarity`,
`char_similarity`, `shared_terms` and `explanation`.

## How it works

**Text representation.** Each report's description is normalized: accents removed, lowercased,
apostrophes dropped, punctuation turned into spaces, and `0` replaced by `o` inside words (`l0st`).
Stopwords and boilerplate (`lost`, `found`, `pls`, `someone`, …) are removed. The description is
repeated twice so it outweighs the short fields. In the **standalone** `TextMatcher` (this demo),
category, colour and brand can be appended via `extra_fields`. The **integrated service used by
C++ sets `extra_fields=()`**, so the app's text score comes from the description alone (see
`docs/INTEGRATION.md`, "Independent components"). When enabled, category, colour and brand are appended,
each only if present. A report whose description has no informative words gets an empty document.
Its category and colour alone don't count as text, because C++ already scores those separately.
IDs and labels are never read.

**TF-IDF.** Each term is weighted by how often it appears in the document (TF, here
`1 + log(count)`) and how rare it is across the corpus (IDF, `log((1+N)/(1+df)) + 1`). Each
document vector is then L2-normalized. A word in most reports, like `sticker`, gets little weight.
A rare word, like `leather`, gets a lot.

**Cosine similarity.** This is the cosine of the angle between two vectors. It's 1 for identical
term profiles and 0 when no terms are shared. It ignores document length.

**Two views, kept distinct.**
- *Exact shared terms* (word TF-IDF): whole words found in both documents. These are listed in
  the explanation, rarest first.
- *General similarity* (character 3–5-gram TF-IDF within words): catches partial wording
  (`scratched`/`scratches`, `sunglass`/`sunglasses`) and typos (`scraztches`).

`similarity = 0.6 × word cosine + 0.4 × character cosine`, in [0, 1]. A result can show
*"No exact shared terms; general similarity 0.31"*. That means the overlap is only partial
wording. **The similarity ranks candidates; it is not a probability.**

**Determinism.** Scores are rounded to 10 decimals before sorting, and ties are broken by
candidate ID. Results don't depend on candidate order.

## Fitting once, and the evaluation trade-off

`TextMatcher.fit()` learns the vocabulary and IDF weights. After that, `rank()` reuses them for
any number of queries without refitting (about 2–3 s for 30,000 reports). Words first seen in a
later query are ignored by the word view, though the character view may still pick them up.
Refit when the corpus changes a lot.

- **Fit on all unlabelled descriptions (lost and found)**: the right choice for the running app.
  It uses no labels, IDs or outcomes, so nothing about the correct answer leaks. The IDF just
  reflects the full vocabulary the matcher will see.
- **Fit on training data only**: the right choice for evaluation. If the vectorizer sees the test
  reports, their words get IDF weights tuned to the test set (a mild, unsupervised form of
  leakage). Results would then look slightly better than on truly new reports. During
  evaluation, fit on the training split, transform the test split, and report which choice was
  made.

## Evaluation status

`data.detect_label_columns()` finds **no label columns** in the processed dataset. So **no
accuracy, precision or recall is reported**. The only label-like test data is
`ml/tests/fixtures/SYNTHETIC_matching_fixture.csv`: 10 rows written by hand for unit tests and
marked synthetic. Passing those tests shows that obvious pairs rank first. It is not a measure of
model accuracy.

## Limitations

- **No synonyms.** Bag-of-words, so `purse` and `wallet` don't match.
- **Templates dominate.** Many dataset descriptions share templates, which gives many near-ties
  (as for R000001 above).
- **Random brands.** Brand is included when present (as specified), but this dataset assigns it
  at random, so shared brand terms like `milton` above are weak evidence.
