# C++ ↔ Python integration (Phase 5)

## Architecture

```
findit (C++)                                              python -m findit_ml.service
┌────────────────────────────────────────────┐            ┌────────────────────────────┐
│ ReportService → MatchEngine                │            │ Service                    │
│   candidate selection: OPEN, not self,     │            │   TextMatcher (Phase 4)    │
│   category blocking, date window           │  stdin     │   TF-IDF word + char       │
│ FallbackStrategy                           │ ─────────► │   cosine similarity        │
│   └ PythonTfidfStrategy ── PythonTextClient│  JSON Lines│                            │
│        │  (validates every response)       │ ◄───────── │ one response per request   │
│        │  PythonProcess (posix_spawnp)     │  stdout    │ diagnostics → stderr log   │
│        └ category/colour/location from     │            └────────────────────────────┘
│          KeywordBaselineStrategy (C++)     │
│   └ KeywordBaselineStrategy (fallback)     │
└────────────────────────────────────────────┘
```

C++ keeps the domain model, persistence, candidate selection and workflow. Python only scores
text. The final score is combined in C++:

```
score = (w.text·python_text + w.location·location + w.category·category + w.colour·colour) / Σw
```

The defaults are the agreed baseline: 0.60 / 0.20 / 0.10 / 0.10.

**Independent components.** Python receives **only the description**: `{"description": …}`, plus
`report_id` in rank requests. Category, colour and location are scored only in C++, so no
attribute is counted twice. For backward compatibility the service still *accepts* `category`,
`color` and `brand` (older clients sent them), but ignores them. Its `ready` message reports
`"text_fields": ["description"]`. Brand isn't part of the score: this dataset assigns brands at
random. Two tests verify the independence: `findit_integration_tests` (same description with
different structured fields gives the same text score, and the totals differ by exactly
0.20 + 0.10 + 0.10) and `ml/tests/test_service.py::test_only_description_is_scored`.

**Lifecycle.** The Python process starts on the first match request, not at app start-up. C++
then sends **one `fit` request** with the description of every report, which takes about 2 s for
30,000 reports. Every later query sends only `rank` requests to the same process, so TF-IDF is
fitted **once per session**. Reports created during the session can be ranked, but their new
words aren't in the vocabulary until the next session. When the app exits, C++ closes the pipe
and Python exits.

**Safe invocation.** The process is started with `posix_spawnp(executable, ["python", "-m",
"findit_ml.service"])`: an argv array, with no shell and no string concatenation. Report text
travels only inside JSON, as data. `PYTHONPATH` is set explicitly. Writing to a dead process
returns an error instead of a SIGPIPE.

## Protocol

The schema is in [docs/schemas/findit_matcher_v1.schema.json](schemas/findit_matcher_v1.schema.json).
Example messages, copied from a real run and checked by `ml/tests/test_service.py`, are in
[docs/schemas/examples/](schemas/examples/).

| Direction | Message | Purpose |
|---|---|---|
| Python → C++ | `ready` | First line. `status: "error"` + `missing_dependency` if scikit-learn etc. are absent |
| C++ → Python | `fit` → `fit_result` | Learn vocabulary/IDF from `documents` (descriptions only, no IDs or labels) |
| C++ → Python | `rank` → `rank_result` | Score `candidates` against `lost`. `top_k` = number of candidates |
| C++ → Python | `shutdown` (or EOF) | Exit |
| Python → C++ | `error` | `invalid_json`, `invalid_request`, `not_fitted`, `internal_error`; the service keeps running |

Example `rank_result` entry (actual output):
```json
{"candidate_id": "R000002", "rank": 1, "similarity": 0.8057713735,
 "word_similarity": 0.7983590964, "char_similarity": 0.8168897892,
 "shared_terms": ["black", "card", "id", "leather", "student", "wallet"],
 "explanation": "Exact shared terms: black, card, id, leather, student, wallet; general similarity 0.81 (word TF-IDF 0.80, character n-gram 0.82)"}
```

**What C++ checks** in every response (`PythonTextClient::parseRankResponse`):
- valid JSON object, `schema_version` "1.0", and the expected `type`
- `request_id` echoes the request
- every `candidate_id` was sent, and none appears twice; **missing candidate IDs are an error**
  and the message lists them
- `rank` = 1..n, and scores are numbers in [0, 1], non-increasing
- `lost_has_text` is false only with an empty `matches`

Any violation raises `PythonMatcherError("protocol_error", …)`. A Python `error` message
raises `python_error`, with Python's code and message.

## Build, install and run (macOS, Apple Silicon)

```sh
cd oops_project                                    # your copy of the project folder
python3 -m venv .venv                              # once
.venv/bin/pip install -r ml/requirements.txt       # numpy, scipy, pandas, scikit-learn, pytest
.venv/bin/pip install cmake                        # only if CMake isn't installed (brew install cmake also works)
export PATH="$PWD/.venv/bin:$PATH"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure         # unit tests + end-to-end Python tests
.venv/bin/python -m pytest                         # Python tests
./build/findit_ingest                              # once: builds data/processed/
./build/findit                                     # option 8 = suggest matches (Python TF-IDF)
./build/findit --text-engine keyword               # pure C++ baseline, no Python
```

Run the CLI from the project root, since relative paths in `config/matching.conf` resolve from
there. The JSON library is **vendored**: `third_party/nlohmann/json.hpp`, nlohmann/json 3.11.3,
MIT licence, SHA-256 `9bea4c80…cf103ea6`. To update it, download `json.hpp` from
https://github.com/nlohmann/json/releases into the same place. Alternatively,
`brew install nlohmann-json` and change the include directory in `CMakeLists.txt`. No paid
service is involved.

## Configuration and tuning (`config/matching.conf`)

| Key | Default | Meaning |
|---|---|---|
| `text_engine` | `python` | `python` or `keyword`; can be overridden with `--text-engine` |
| `weight.text/location/category/colour` | 0.60/0.20/0.10/0.10 | normalized by their sum |
| `python.executable` | `.venv/bin/python` | interpreter (path, or a name on `PATH`) |
| `python.module_path` | `ml` | becomes `PYTHONPATH` |
| `python.log_file` | `python_matcher.log` | Python stderr, overwritten each session. Relative values go inside `<data-dir>/store/` (default: `data/store/python_matcher.log`); absolute paths are used as given |
| `python.timeout_seconds` | 120 | per response (the first fit included) |
| `python.fallback` | `true` | use the keyword baseline if Python fails; `false` = show the error |

**How to tune the weights:**
1. Change one weight at a time.
2. Re-run the evaluation (Phase 6) on a labelled set that is **separate** from the examples you
   tune on.
3. Keep the change only if ranking metrics improve on held-out pairs.
4. Watch for redundancy *in the data*. The code sends only descriptions to Python, but this
   dataset's descriptions themselves often contain the item name and colour ("Found a yellow
   marker"). A higher `weight.text` therefore still partly re-rewards those attributes.
5. With `category_blocking = true`, the category component is always 1, so its weight only
   shifts every score equally.
6. Never tune on the evaluation's test split.

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `[!] Python matcher unavailable: Cannot start Python interpreter '.venv/bin/python': No such file or directory` | No venv: `python3 -m venv .venv && .venv/bin/pip install -r ml/requirements.txt`, or set `python.executable` |
| `… reported missing_dependency: No module named 'sklearn'` | `.venv/bin/pip install -r ml/requirements.txt` |
| `Python matcher exited unexpectedly (exit code N)` plus a stderr excerpt | See `<data-dir>/store/python_matcher.log` (default `data/store/python_matcher.log`). Try `PYTHONPATH=ml .venv/bin/python -m findit_ml.service` by hand |
| `No response from Python matcher within 120 s` | Very large corpus or a stuck process; raise `python.timeout_seconds` |
| `Invalid response from Python matcher: …` | Protocol mismatch (e.g. edited service); the message names the offending field |
| Results say `keyword-baseline-v1 (fallback: …)` | Python failed earlier in this session; fix it and restart the CLI |
| Integration test shows **Skipped** in CTest | The interpreter in `-DFINDIT_PYTHON` (default `.venv/bin/python`) is missing or lacks dependencies. A skip is never a pass |

Manual protocol check:
```sh
cat docs/schemas/examples/fit_request.json | tr -d '\n' > /tmp/req.jsonl; echo >> /tmp/req.jsonl
PYTHONPATH=ml .venv/bin/python -m findit_ml.service < /tmp/req.jsonl
```

## Tests

- **`findit_tests`** (C++, no Python): request building, response validation for each rule
  above, missing interpreter, fallback behaviour, config parsing.
- **`findit_integration_tests`** (C++ → real Python): one lost report and four found
  candidates, with checks on the ranked response, the score formula, single fitting and the
  no-text case. Failure cases: missing dependency (with fallback), invalid JSON, crash with
  stderr, and timeout.
- **`ml/tests/test_service.py`**: the protocol run as a subprocess, plus the documented examples.
