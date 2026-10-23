# FindIt AI — Intelligent Campus Lost-and-Found Matcher

FindIt AI is a lost-and-found application with a command-line interface and a local JSON API.
Students report lost items, the desk registers found items, and the system ranks likely matches
with human-readable reasons.

- **C++17** holds the object-oriented core: reports, users, repository, matching engine, the CLI
  (`findit`) and a local HTTP API (`findit_server`). Both front ends share one `AppContext`.
- **Python 3** with scikit-learn computes TF-IDF text similarity.
- C++ and Python talk through **JSON over a pipe**.
- The app works without Python too, using a C++ keyword matcher.

Individual undergraduate project. No database server, no authentication, no paid services. The
API listens on 127.0.0.1 only. A web UI is planned to use the API but isn't part of the project
yet.

## Features

- **Reports and users.** Report a lost item, register a found item, and register users. Every
  field is validated, and invalid input is rejected with a clear message.
- **Persistence.** Reports, users and confirmed matches created in the app are saved to
  `data/store/` and reloaded on start.
- **Dataset import.** Ingests the 30,000-row CSV through a configurable column mapping. Rejected
  rows are logged with reasons, the summary is reproducible, and the original file is never
  modified.
- **Matching.** Shows the **top 3 matches** with scores and reasons:
  - Exact shared terms and general TF-IDF similarity, computed from the **description only**.
  - Same or related category, colour and location, scored separately in C++.
- **Explain a match.** Answers "why (not) this one?" for any lost/found pair.
- **Confirm a match.** A person confirms a lost/found pair; both become MATCHED, the decision is
  logged, and repeated or invalid confirmations are rejected.
- **Graceful failure.** If the Python matcher is unavailable, the app falls back to the C++
  keyword matcher with a warning. Empty results come with a hint; long lists are paged.
- **Local JSON API.** `findit_server` exposes reports, search, matches and confirmation over HTTP
  on 127.0.0.1 ([docs/API.md](docs/API.md)), using the same rules and storage as the CLI.
- **Safe shared storage.** A data-directory lock stops the CLI and the server from using the same
  `data/store/` at the same time. Every save follows "validate, write, then update memory", so
  a failed write changes nothing.
- **Evaluation and tests.** A reproducible evaluation (`bash evaluation/run_evaluation.sh`) and
  automated tests in C++ and Python.

## Requirements (macOS, Apple Silicon)

| Tool | Version used | Install |
|---|---|---|
| Xcode Command Line Tools (Apple clang, C++17) | clang 21 | `xcode-select --install` |
| CMake ≥ 3.20 | 4.4.4 | `brew install cmake` or `.venv/bin/pip install cmake` |
| Python 3 | 3.14.3 | `brew install python` |
| numpy, scipy, pandas, scikit-learn, pytest | pinned in `ml/requirements.txt` | `pip install -r ml/requirements.txt` |
| nlohmann/json 3.11.3 (MIT) | vendored | already in `third_party/` |
| cpp-httplib 0.18.3 (MIT) | vendored | already in `third_party/` (see `third_party/README.md`) |
| curl | macOS built-in | used by the server end-to-end test |

## Setup, build and test

```sh
cd oops_project                                   # your copy of the project folder
python3 -m venv .venv
.venv/bin/pip install -r ml/requirements.txt
.venv/bin/pip install cmake                       # skip if CMake is already installed
export PATH="$PWD/.venv/bin:$PATH"

cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
./build/findit_ingest                             # builds data/processed/ from data/raw/
ctest --test-dir build --output-on-failure        # C++ unit, C++→Python, CLI and HTTP server tests
.venv/bin/python -m pytest                        # Python tests
```

## Run

```sh
./build/findit                            # full app: dataset + Python TF-IDF matcher
./build/findit --text-engine keyword      # no Python needed
./build/findit --sample                   # 6 fictional reports, nothing saved
./build/findit < scripts/demo_input.txt   # scripted demo (expects an empty data/store/)
```

```
=== FindIt AI - Campus Lost & Found ===
1. Report a lost item            6. Explain a specific lost/found match
2. Register a found item         7. Register a new user
3. List reports                  8. List users
4. View a report                 9. Settings and status
5. Find matches (top 3)         10. Confirm a match
                                 0. Exit
```
*(The real menu lists these options in one column.)*

**Status rules.** A report starts OPEN. Confirming a match (option 10) moves the lost report and
the found report to MATCHED together, and only if both are OPEN. A repeated confirmation, or
reusing an already matched found item, is rejected. MATCHED → CLOSED (item handed over) exists in
the model but isn't in the menu. There is no undo in the app: to reverse a confirmation, remove its
line from `data/store/matches.csv` and restart.

Example: actual output of option 5 from the scripted demo.
```
Matcher: python-tfidf-v1 | weights text 0.60, location 0.20, category 0.10, colour 0.10 | category blocking ON
Lost: R030001  LOST   OPEN     Water Bottle    blue     Gym               2025-12-02
      "Blue Milton steel water bottle with a dent and a cat sticker"
Top 3 candidate(s):
#1  R030002  score 0.66  [category=1.00, colour=1.00, location=0.50, text=0.60]
    - Text (TF-IDF): Exact shared terms: cat, steel, bottle, blue, sticker; general similarity 0.60 (word TF-IDF 0.58, character n-gram 0.63)
    - Same category: Water Bottle
    - Same colour: blue
    - Related location (zone SPORTS): Gym ~ Sports Complex
#2  R017618  score 0.58  [category=1.00, colour=1.00, location=1.00, text=0.30]
    ...
Scores rank candidates against each other; they are not probabilities.
```

## Local API server

Run it from the project root, in its own terminal. Stop the CLI first: only one of them can use a
data directory at a time.

```sh
./build/findit_server                     # http://127.0.0.1:8080 (dataset + data/store)
./build/findit_server --port 8081         # another port
./build/findit_server --sample            # fictional data, nothing saved
```

- **Check it:** `curl -s http://127.0.0.1:8080/api/health`
- **Stop it:** press **Ctrl-C** in its terminal (or `kill <pid>`). It shuts down cleanly and
  releases the data-directory lock.
- **Endpoints, errors and security:** see [docs/API.md](docs/API.md).

**If the CLI or server says "The data directory is in use by another FindIt process":**
1. Another `findit` or `findit_server` is running on the same `data/`. The message names its
   program and pid. Stop it with Ctrl-C, or with `kill <pid>`.
2. After a crash there's nothing to clean up: the operating system releases the lock when a
   process dies, so just start again.
3. `lsof data/store/.findit.lock` shows who holds it.
4. **Never delete `.findit.lock`.** Deleting it doesn't release a lock held by a running process;
   it only removes the protection.

## Configuration

- **`config/dataset_mapping.conf`:** source column names, lost/found values and missing-value
  tokens.
- **`config/matching.conf`:** weights (`0.60 text, 0.20 location, 0.10 category, 0.10 colour`),
  `top_k = 3`, category blocking, zones and colour/category groups, `text_engine`, and the
  Python interpreter, path, timeout and fallback.

Run the app from the project root; relative paths resolve from there. The exception is
`python.log_file`: a relative value is placed inside `<data-dir>/store/`, so `--data-dir`
never writes into the project's own `data/`.

## Project layout

```
config/            dataset mapping and matching settings
data/raw/          read-only copy of the source CSV (original also kept in the project root)
data/processed/    generated by findit_ingest (git-ignored)
data/store/        reports, users and confirmed matches (matches.csv) created in the app,
                   Python log, .findit.lock (git-ignored)
docs/              design, architecture, API, dataset, integration, evaluation, demo, viva notes
evaluation/        synthetic_v1/ (hand-written labelled set), run_evaluation.sh, results/
include/findit/    C++ headers         src/   C++ implementation + main.cpp (CLI)
tools/             findit_ingest.cpp, findit_eval.cpp, findit_server.cpp (HTTP API)
ml/findit_ml/      Python matcher, JSON service, evaluation metrics
tests/             C++ unit tests, integration/ (C++→Python), cli/, server/ (HTTP e2e), fixtures/
ml/tests/          pytest tests
third_party/       nlohmann/json, cpp-httplib (vendored, MIT; see third_party/README.md)
```

## Documentation

| Document | Content |
|---|---|
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | System diagram, class diagram, sequence diagrams |
| [docs/DESIGN.md](docs/DESIGN.md) | OOP concepts with file references; C++/Python boundary; decisions |
| [docs/DATASET.md](docs/DATASET.md) · [docs/DATA_MAPPING.md](docs/DATA_MAPPING.md) | Dataset provenance/description; column mapping and ingestion results |
| [docs/MATCHING.md](docs/MATCHING.md) · [docs/PYTHON_MATCHER.md](docs/PYTHON_MATCHER.md) | Keyword baseline; TF-IDF and cosine similarity |
| [docs/INTEGRATION.md](docs/INTEGRATION.md) · [docs/schemas/](docs/schemas/) | JSON protocol, schema, examples, tuning, troubleshooting |
| [docs/API.md](docs/API.md) | Local HTTP API: start/stop, endpoints, errors, security model, data-directory lock |
| [docs/EVALUATION_REPORT.md](docs/EVALUATION_REPORT.md) | Labels, metrics, comparison, runtime, limitations |
| [docs/DEMO_SCRIPT.md](docs/DEMO_SCRIPT.md) · [docs/VIVA_QA.md](docs/VIVA_QA.md) | 3–5 minute demo; likely viva questions |

## Limitations

- **The dataset is synthetic and has no match labels.** Quality was measured only on a small,
  hand-written **synthetic** set (11 test queries). That is not real-world accuracy, and the two
  text engines can't be told apart on it.
- **Scores are uncalibrated ranking values**, not probabilities. Hard negatives can score 0.88.
- **Bag-of-words text**, so no synonyms: `purse` and `wallet` match only through other words.
  The dataset's own descriptions often repeat the item name and colour, so text and structured
  scores overlap *in the data*. The code no longer sends those fields to Python.
- **Synthetic labels not human-reviewed.** Automatic checks passed; 3 pairs are flagged as
  uncertain (`evaluation/synthetic_v1/review/REVIEW_REPORT.md`).
- **Category blocking** (the default) misses items filed under a related category.
- **API is local and single-user.** No authentication; one request at a time; loopback only.
  It's not designed to be exposed on a network.
- **Fixed vocabulary.** TF-IDF is fitted once per session, so words that first appear in new
  reports are only partly used until the app restarts.
- **Local only.** Single user, no authentication, no concurrent access to `data/store/`.
