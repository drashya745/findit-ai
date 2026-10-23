# Dataset description and provenance

## Provenance

Each row says whether it is **verified**, and how, or **UNVERIFIED**. Nothing here is guessed.

| Item | Value | Status / evidence |
|---|---|---|
| Dataset name | Not stated anywhere inside the file; only the filename `lost_found_dataset_realistic_metadata_30k.csv` | Verified: the file has no title or metadata row |
| Source | Most likely **GitHub**; the exact repository, page or file URL is **unknown** | Partly verified: macOS download metadata (`kMDItemWhereFroms`) records only `https://github.com/` (browsers may store a referrer or truncated value here, so the file itself could have come from a GitHub-hosted URL such as raw.githubusercontent.com); the quarantine attribute names the Arc browser |
| Original URL | **UNVERIFIED.** A web search for the exact filename (2026-10-23) found no public page | Fill in from your browser history (see below) |
| Author / publisher | **UNVERIFIED** | Depends on the repository |
| Licence | **UNVERIFIED.** No licence file, header or notice came with the CSV | Must be read from the source repository's `LICENSE` file or README |
| Attribution requirements | **UNVERIFIED** (depend on the licence) | Until known: cite as "lost_found_dataset_realistic_metadata_30k.csv, downloaded from GitHub on 2026-10-22, source repository not recorded", and do not redistribute |
| Access (download) date | **2026-10-22 17:00:29 UTC** | Verified: macOS quarantine timestamp (`0x6ada412d`) matches the file's creation date |
| File | `lost_found_dataset_realistic_metadata_30k.csv` | |
| Location | Project root (original, untouched) and `data/raw/` (byte-identical read-only copy) | Verified by SHA-256 |
| SHA-256 | `117f92940977ef8855d03416b9ca5abd49023b74daa714da4437497614567730` | Verified (`shasum -a 256`) |
| Size / encoding | 3,464,978 bytes, ASCII (valid UTF-8), LF line endings | Verified |
| Nature | **Synthetic, generated data**, not real campus reports | Inferred from content: near-uniform value distributions, every timestamp at 17:34:15, template descriptions with injected noise (`L0st`, `lol`, `scraztches`), and brands assigned regardless of item (e.g. "Pearson earphones") |

### How to complete the missing provenance

1. In Arc, open History and search for `lost_found` or `github.com` around **22 October 2026,
   22:30 IST** (17:00 UTC; this Mac's time zone is +05:30). Or search GitHub for the filename while signed in, since code
   search finds files that web search doesn't index.
2. Note the repository URL, its owner, and the commit or release you downloaded.
3. Open the repository's `LICENSE` file and README, and record the licence name (e.g. MIT,
   CC BY 4.0, or none) and any attribution text.
4. Replace the UNVERIFIED rows above. **If the repository has no licence, the default is "all
   rights reserved"**: use it for private coursework only, and don't publish the CSV (or
   `tests/fixtures/realistic_sample.csv`, which holds 12 of its rows) in a public repository.

## Preprocessing (summary; details in [DATA_MAPPING.md](DATA_MAPPING.md))

1. The original is copied, never moved, to `data/raw/` and made read-only. Its SHA-256 is
   recorded in `data/processed/summary.json`.
2. `findit_ingest` parses the file as RFC 4180 CSV and maps columns through
   `config/dataset_mapping.conf`. It trims and collapses whitespace, lowercases colour, and turns
   `None`/`null`/`nan`/`n/a`/`na`/empty into *unknown*. Description wording and typos are kept.
3. Rows are validated (required fields, lost/found value, date, duplicates). Rejected rows would
   go to `rejected_rows.csv`; none were rejected.
4. The output is `data/processed/users.csv` (20 reporters) and `reports.csv` (30,000 reports,
   all OPEN). Re-running gives byte-identical files.

## Contents (measured)

- 30,000 rows and 9 columns: `item_id, lost_or_found, item_name, description, color, brand,
  location, user, timestamp`.
- 15,000 lost and 15,000 found reports.
- 31 item categories, 11 colours, 18 brand values, 19 campus locations and 20 reporter first
  names.
- Dates run from 2025-08-03 to 2025-12-01.
- **Missing values:** `brand` is `None` in 1,652 rows; nothing else is missing.
- **Duplicates:** no duplicate rows and no duplicate IDs.
- **Descriptions:** 9,889 distinct raw descriptions (9,717 after whitespace normalization).
- 2,329 found reports use one of five generic "unrelated item" descriptions, spelled exactly
  (e.g. "Found something totally unrelated."). Typo variants of these exist too and are not
  counted here.

## Labels

There is **no match-label column**. Rows come in adjacent (lost, found) pairs, and 12,017 of
those pairs share category, colour, brand and location. But this pattern is undocumented, and
it is defined by the very fields the matcher scores. It is **not** used as ground truth; see
`docs/EVALUATION_REPORT.md`.

## Personal data

The `user` column contains first names only, with no surnames, contact details or IDs. Because
the data is synthetic, these are presumably invented. Treat them as personal data anyway, and
don't publish them alongside real identities.

## Known limitations

- **Synthetic content:** results on it say nothing about real campus reports.
- **Unusable dates:** every timestamp has the same time of day, and found dates precede lost dates
  in about half of the generated pairs. The app's date filter is off for this reason.
- **Random brands:** brands are assigned independently of the item, so brand is not used in
  matching.
- **Templated text:** descriptions come from a few templates and repeat the item name and colour,
  which leads to many near-ties.
- **No match labels:** the adjacent-row pairing is not documented ground truth.
- **Unknown provenance:** licence and source are not yet verified (see above).
