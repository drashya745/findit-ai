# Likely viva questions and answers

## OOP and C++

**Q: Why is `ItemReport` abstract?**
A lost or found report is always one or the other; a plain "report" makes no sense.
`type()` and `dateLabel()` are pure virtual, so `ItemReport` can't be instantiated. Shared
fields and validation live in the base class once.

**Q: Where is runtime polymorphism used?**
In three places:
- `printReports` calls `summary()` through `const ItemReport*`.
- `FoundReport::details()` overrides the base version and calls `ItemReport::details()` before
  adding "Held at".
- `MatchEngine` calls `strategy_->score(...)` without knowing which strategy it holds: keyword,
  Python or fallback.

**Q: Why `unique_ptr`, and why is copying deleted?**
The repository owns every report, so `vector<unique_ptr<ItemReport>>` makes that ownership
explicit and automatic. Copying a polymorphic object through its base would slice off derived
data, so copy is deleted. Other code receives non-owning `const` pointers.

**Q: Why a virtual destructor?**
Reports are deleted through `unique_ptr<ItemReport>`. Without a virtual destructor, deleting a
`FoundReport` through a base pointer is undefined behaviour.

**Q: How does encapsulation protect state?**
- Fields are private.
- Constructors validate: IDs like `R000001`, description length 10–500, real dates.
- `setStatus` allows only OPEN→MATCHED→CLOSED.
- The CLI only ever gets `const` reports, so it can't change a status directly.

**Q: Which design patterns did you use?**
- Strategy (`SimilarityStrategy`)
- Fallback/decorator (`FallbackStrategy`)
- Facade (`ReportService`)
- Template method (`details()` uses the virtual `dateLabel()`)
- Adapter (`DatasetAdapter` maps source columns to canonical fields)

**Q: What does `mutable` do in `PythonTfidfStrategy`?**
`score()` is `const` because scoring doesn't change the strategy's observable behaviour. The
lazily started Python client is a cache, so it's `mutable`.

## Data

**Q: Does the dataset have labels?**
No. There's no match column. Rows come in adjacent lost/found pairs, but that pattern is
undocumented and defined by the same fields we score, so using it would be circular. I used a
separate, hand-written synthetic set and labelled it as synthetic.

**Q: How do you handle missing values?**
- **During ingestion:** empty fields and tokens like `None` or `N/A` become unknown. Required
  fields that are missing reject the row, with a logged reason.
- **During matching:** a missing value scores 0 and the explanation says so. It never counts as
  agreement.

**Q: How do you know the original data is unchanged?**
It's copied, never moved, to `data/raw/` as read-only. Its SHA-256 is printed and stored in
`summary.json`. The ingester refuses to write into the input's directory.

## TF-IDF, cosine similarity, ranking

**Q: Explain TF-IDF.**
Each word in a document gets the weight TF × IDF:
- TF is how often the word appears in this report (we use `1 + log(count)`).
- IDF is `log((1+N)/(1+df)) + 1`. It's large for rare words.

So "leather" or "Priya" counts for much more than "bottle", which appears in thousands of
reports.

**Q: Explain cosine similarity.**
It's the cosine of the angle between two TF-IDF vectors, `A·B / (|A||B|)`. It's 1 when the term
profiles are identical and 0 when no terms are shared. It ignores document length. scikit-learn
L2-normalizes the vectors, so the dot product equals the cosine.

**Q: Why both word and character n-grams?**
- **Word TF-IDF** gives explainable *exact shared terms*.
- **Character 3–5-grams** catch partial wording such as `scratched`/`scratches` and typos.

The similarity is 0.6 × word + 0.4 × character, and both parts appear in the explanation.

**Q: How is the final score computed?**
`(0.60·text + 0.20·location + 0.10·category + 0.10·colour) / sum of weights`. Category, colour
and location use 1 for the same value, 0.5 for the same configured group or zone, and 0 otherwise.

**Q: Is the score a probability?**
No. It's an uncalibrated ranking value. Evaluation shows hard negatives scoring up to 0.88.

**Q: Why category blocking?**
Speed and precision: about 480 candidates instead of 15,000, and about 30 ms instead of about
0.9 s per query with Python. The cost is that items filed under a related category are missed.
In the synthetic evaluation (not real-world accuracy), recall@1 rose from 9/11 to 10/11 with
blocking off.

**Q: How are ties broken?**
By found report ID, so results are deterministic. Python also rounds scores to 10 decimals, so
mathematically equal scores tie exactly.

**Q: Why fit TF-IDF only once?**
Fitting 30k descriptions takes about 2 s. It uses no labels, so fitting on all unlabelled text is
fine in the app. For evaluation, fitting on test text gives a small unsupervised advantage;
fitting on training text only would be stricter.

## Integration

**Q: How do C++ and Python communicate?**
A persistent child process exchanges JSON Lines over stdin/stdout:
1. `ready`
2. `fit` → `fit_result` (once)
3. `rank` → `rank_result` (per query)

It's versioned with `schema_version` and documented as a JSON Schema.

**Q: Is the process call safe?**
Yes. It uses `posix_spawnp` with an argv array and no shell, so report text can never be
executed as a command. It travels only as JSON data.

**Q: Could the same attribute be counted twice?**
It used to be. The Python document appended category and colour, which C++ also scores. Now C++
sends only the description, and the service ignores any other field. An end-to-end test gives
two found reports the same description but different category, colour and location. Their text
scores are identical, and their totals differ by exactly 0.20 + 0.10 + 0.10. In the synthetic evaluation, a lost
report whose description shares no words with its match dropped from 0.66 to 0.40, the
structured part alone.

**Q: How does match confirmation work, and why a log file?**
Option 10 shows the pair's score and reasons and asks for y/N. `ReportService::confirmMatch`
checks that both reports exist, have the right types and are both OPEN, then sets both to MATCHED
together. Repeats fail with "already MATCHED". The decision is appended to
`data/store/matches.csv`, and on restart each row is replayed through the same `confirmMatch`.
That way the baseline `reports.csv` is never rewritten, and a corrupt log fails with its line
number.

**Q: What if Python is missing or returns garbage?**
The client checks the `ready` message (`missing_dependency`), request IDs, every candidate ID
(unknown, duplicate or missing), ranks, number ranges and sort order. Any failure raises
`PythonMatcherError`, and `FallbackStrategy` switches to the C++ keyword matcher with a
warning. Timeouts and crashes report Python's stderr.

## Testing and evaluation

**Q: How did you test?**
- 51 C++ unit tests (dependency-free runner under CTest).
- 7 end-to-end C++→Python tests: a real ranking and component independence, plus missing
  dependency, invalid JSON, crash and timeout.
- A menu-level CLI test that drives the real binary to confirm a match, rejects invalid
  confirmations, and checks the result after a restart.
- 73 pytest tests covering preprocessing, the matcher, the service protocol, metrics (including
  undefined-metric edge cases) and the label-review checks.

**Q: What metrics did you use and why?**
- Precision@K, Recall@K and MRR for ranking, since the app shows the top 3.
- Precision, recall and F1 for the "is this a match" decision at a threshold chosen on dev only.

Undefined cases, such as no true match or no predictions, are reported as undefined, never as 0.

**Q: Which matcher is better?**
On the synthetic test set they produce identical rankings. The binary F1 differences are within
noise for 11 positives, so I can't claim either is better.

**Q: What are the main limitations?**
- Synthetic data, and synthetic labels that no independent human has reviewed. Automatic checks
  passed; 3 pairs are flagged as uncertain.
- Bag of words: no synonyms.
- Uncalibrated scores.
- The dataset's descriptions often repeat item name and colour (a data issue, not a code issue).
- Blocking misses cross-category reports.
- Confirmed matches are logged, but there aren't enough real ones yet to serve as labels.

**Q: What would you do next?**
- Get the uncertain labels reviewed by a person, and collect real confirmations from
  `data/store/matches.csv` as labels.
- Tune weights on held-out labels.
- Try sentence embeddings, offline and free.
- Add photos.
