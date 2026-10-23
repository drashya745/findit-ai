# Demo script (3–5 minutes)

## Before the presentation (about 2 minutes, not on stage)

```sh
cd oops_project
export PATH="$PWD/.venv/bin:$PATH"
cmake -S . -B build && cmake --build build -j
ctest --test-dir build                # expect: 100% tests passed out of 2
./build/findit_ingest                 # expect: 30000 read, 30000 accepted
rm -rf data/store                     # start with no user-created reports, so IDs are R030001/2
```

Have `docs/ARCHITECTURE.md` (diagrams) and `docs/EVALUATION_REPORT.md` open in a second window.
For a no-typing backup, keep `./build/findit < scripts/demo_input.txt` ready. It runs the same
flow.

## Script

**0:00 – 0:30 · Problem and design** *(show the system diagram)*
> "Campus lost-and-found desks match items by hand. FindIt AI takes lost and found reports and
> ranks likely matches with reasons. C++ holds the object-oriented core: reports, users,
> repository, matching engine. Python does the text similarity with TF-IDF. They talk through
> JSON over a pipe."

**0:30 – 1:00 · Data**
Run `./build/findit_ingest`.
> "This is a 30,000-row dataset. The original file is never modified, and the hash proves it.
> Every row is validated; rejected rows would be logged with a reason. Here 1,652 brands were
> 'None', which we treat as unknown, not as a brand."

**1:00 – 2:30 · Live use** *(run `./build/findit`)*
1. `7` Register a user: *Demo Student*, *demo.student@example.edu* → **U021**.
2. `1` Report a lost item with description `short` → **shows validation**: "Description must be
   at least 10 characters". Nothing is saved.
3. `1` again: U021, *Water Bottle*, "Blue Milton steel water bottle with a dent and a cat
   sticker", blue, Milton, Gym, 2025-12-02 → **R030001** saved.
4. `2` Register a found item: U001, *Water Bottle*, "Found a blue steel bottle with a cat
   sticker, slightly dented", blue, (no brand), Sports Complex, 2025-12-03, "Security office" →
   **R030002**.
5. `5` Find matches for R030001.
   > "First query: Python starts and fits TF-IDF once on all 30,002 descriptions, which takes
   > about 2 seconds. Our found bottle is #1 with 0.66. The reasons: exact shared words *cat,
   > steel, sticker…* from the descriptions only, plus same category and colour and a related
   > location (Gym and Sports Complex are in the same zone), which are scored separately in C++.
   > #2 and #3 are generic blue bottles at the Gym. The score is a ranking value, not a
   > probability."

**2:30 – 3:15 · Explainability, confirmation, robustness**
6. `6` Explain R030001 vs R030002 → the same breakdown for one specific pair.
7. `10` Confirm R030001 + R030002 → `y` → "now MATCHED". Run `10` again with the same pair →
   "already MATCHED; it cannot be confirmed again".
8. `0` Exit, then run `./build/findit` again → "2 created in this app and 1 confirmed match(es)":
   **persistence**.
9. *(Optional)* `./build/findit --text-engine keyword` → **same app with no Python**. Mention
   that a missing venv triggers an automatic fallback with a clear warning.

**3:15 – 4:15 · Evaluation, honestly** *(show EVALUATION_REPORT.md §2–§6)*
> "The dataset has no real match labels, so I didn't invent accuracy. I built a small synthetic
> test set with known matches and hard negatives, split by case. Both engines rank the true match
> first for 9 of 11 test queries with category blocking on, and 10 of 11 with it off. They miss
> the same cases: a look-alike at the exact location (either setting), and an item filed under a
> different category (blocking on). Scores also aren't calibrated: wrong items can score 0.88.
> The labels passed automatic checks but haven't been reviewed by an independent person; three
> are flagged as uncertain. Timing: about 30 ms per query after a 2 s start."

**4:15 – 5:00 · Tests and limitations**
> "There are 51 C++ unit tests, 7 end-to-end C++→Python tests including crash, timeout,
> missing-dependency and component-independence cases, a menu-level CLI test, and 73 Python
> tests. Limitations: synthetic data, no synonyms, uncalibrated scores, and no real campus
> feedback yet."

## If something goes wrong

| Problem | Say / do |
|---|---|
| Python warning appears | "This is the designed fallback." Continue with keyword results |
| IDs are not R030001/2 | `data/store` wasn't cleared; use the IDs printed on screen |
| Long pause on option 5 | This is the first-query fit (about 3 s); mention it |
