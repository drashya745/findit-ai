# FindIt AI local JSON API (`findit_server`)

A thin HTTP adapter over the same `AppContext` / `ReportService` / `MatchEngine` / Python TF-IDF
matcher the CLI uses. Every business rule (validation, IDs, matching, confirmation) stays in
C++; the server only routes, checks request shapes and converts to JSON. Source:
`tools/findit_server.cpp` (HTTP, cpp-httplib 0.18.3) and `src/ApiHandlers.cpp` (logic).

## Start and stop (macOS)

```sh
cd oops_project
./build/findit_server                    # http://127.0.0.1:8080, data/ (dataset + data/store)
./build/findit_server --port 8081 --data-dir /path/to/data --text-engine keyword
./build/findit_server --sample           # fictional sample data, nothing saved
# stop: Ctrl-C in its terminal, or: kill <pid>   (SIGINT/SIGTERM -> clean shutdown, lock released)
```

| Option | Default | Notes |
|---|---|---|
| `--port N` | 8080 | 1024–65535; `0` picks a free port (printed on start-up) |
| `--data-dir DIR` | `data` | Uses `DIR/processed` + `DIR/store`; falls back to sample data if `processed/` is missing |
| `--matching-config PATH` | `config/matching.conf` | Same file as the CLI |
| `--text-engine python\|keyword` | from config | Same as the CLI |
| `--sample` | off | Fictional data; writes nothing; no lock |
| `--allow-origin URL` | off | Exact `http://localhost[:port]` or `http://127.0.0.1[:port]` origin for CORS, only needed when a browser app calls the API directly instead of through a dev proxy |

Start it from the project root (like the CLI): `config/`, `.venv/bin/python` and `ml/` are
resolved from there. Exit codes: 0 clean shutdown, 1 start-up error (bad config, port in use,
bad data), 2 bad arguments, 3 data directory locked.

## Security model

- Listens on **127.0.0.1 only**; no option exposes it on the network.
- Rejects requests whose `Host` header isn't `127.0.0.1:<port>` or `localhost:<port>`
  (403 `forbidden_host`), to block DNS-rebinding attacks from web pages. A dev proxy must forward
  with that Host (in Vite: `changeOrigin: true`).
- Only `/api/*` JSON endpoints exist: no static files, no file paths, no command execution.
  The Python interpreter path comes only from `config/matching.conf`, never from a request.
- Request bodies are capped at **16 KB** (413). POST bodies must be `application/json` (415),
  a JSON object, and contain only known fields of the right type (400).
- Requests are handled **one at a time** (a mutex around `AppContext`, which isn't thread-safe).
- Unexpected errors return a generic 500. Details, which may include local paths, go only to
  the server's stderr. `GET /api/health` reports `dataMode`, not paths.
- Responses carry `Cache-Control: no-store`, `X-Content-Type-Options: nosniff` and
  `Referrer-Policy: no-referrer`.

## Data-directory lock

The CLI and the server take an exclusive lock on `<data-dir>/store/.findit.lock` before reading
`store/`, and hold it while running, so they can never use the same data concurrently. A second
process stops with: *"The data directory is in use by another FindIt process (pid=…
program=findit_server since=…)"*. Sample mode writes nothing and takes no lock.

The lock is a kernel `flock`, not the file's existence. It's released automatically when the
owning process exits, **including after a crash or `kill -9`**. The file's text names the last
owner for messages only. The file is never deleted (that would be unsafe), only emptied on clean
shutdown.

**Recovery after a crash.** Just start again: the kernel already released the lock. If you
still see "in use":
1. Find the holder: `lsof data/store/.findit.lock` (or `ps -p <pid>` with the pid from the
   message).
2. If it's a FindIt process you started (e.g. a server in another terminal), stop it with
   Ctrl-C or `kill <pid>`. Use `kill -9 <pid>` only if it doesn't respond.
3. Don't delete `.findit.lock`. That doesn't release a lock held by a running process; it only
   breaks the protection.

The lock is reliable on local disks only (not network shares).

## Conventions

- JSON in and out, UTF-8. Report IDs look like `R000001`, user IDs like `U001`.
- Report `type`: `lost` | `found`. Report `status`: `open` | `matched` | `closed`.
- Errors: `{"error": {"code": "...", "message": "..."}}`.

| Status | `code` | When |
|---|---|---|
| 400 | `invalid_request`, `invalid_json`, `invalid_query`, `invalid_id`, `validation_failed`, `not_a_lost_report` | Bad shape, bad JSON or query, malformed ID, domain validation (e.g. description < 10 characters, unknown reporter, wrong report types) |
| 403 | `forbidden_host`, `cors_not_allowed` | Host check; CORS preflight from another origin |
| 404 | `not_found` | Unknown endpoint or report |
| 405 | `method_not_allowed` | Wrong method for an endpoint |
| 409 | `conflict`, `not_open` | Report already MATCHED (repeated confirmation, or a matched lost report asked for matches) |
| 413 | `payload_too_large` | Body over 16 KB |
| 415 | `unsupported_media_type` | POST without `application/json` |
| 500 | `internal_error` | E.g. writing to `data/store` failed; **nothing was changed** |
| 503 | `matcher_unavailable` | Python failed and the keyword fallback is disabled |

**Report object**
```json
{"id": "R950033", "type": "lost", "status": "open", "category": "Wallet",
 "description": "Black leather wallet with a student ID card", "color": "black",
 "brand": null, "location": "Library", "date": "2026-03-20", "reporterId": "U005",
 "heldAt": null, "origin": "app", "sourceRef": null}
```
`origin` is `dataset` for imported rows (`sourceRef` = their dataset `item_id`; this project's
dataset is synthetic) and `app` for reports created in the CLI or API.

## Endpoints

| Method & path | Query / body | Success |
|---|---|---|
| `GET /api/health` | — | `{status, dataMode: "dataset"\|"sample", persistent, sampleData, matcher: {strategy, textEngine, fallbackActive, categoryBlocking}}` |
| `GET /api/stats` | — | `{totalReports, lostReports, foundReports, openLostReports, openFoundReports, confirmedMatches, reportsCreatedInApp}` |
| `GET /api/reports` | `q` (≤ 100 chars, ≤ 10 words, all must appear in id/description/category/colour/location/brand), `type`, `status`, `category` (exact, case-insensitive), `origin` (`dataset`\|`app`), `sort` (`recent` default: newest date first \| `id`), `limit` 1–100 (default 20), `offset` ≥ 0 | `{items: [Report], total, limit, offset}` |
| `GET /api/reports/:id` | — | Report |
| `GET /api/categories` | — | `{items: [{name, count}]}` sorted by name |
| `GET /api/users` | — | `{items: [{id, name}]}` (contact details are not exposed) |
| `POST /api/users` | `{name, contact?}` | 201 `{id, name, persisted}` |
| `POST /api/reports` | `{type, reporterId, category, description, color, location, date (YYYY-MM-DD), brand?, heldAt? (found only)}` | 201 Report + `persisted` |
| `GET /api/reports/:id/matches` | `k` 1–10 (default 3) | `{lost: Report, matcher: {...}, scoreNote, matches: [{rank, score, components: {text, location, category, colour}, explanations: [..], found: Report}], emptyReason: null\|"no_open_found_reports_in_category"\|"no_open_found_reports"}` |
| `POST /api/matches` | `{lostId, foundId}` | 200 `{lost, found, confirmedOn, persisted}`, sent **only after** the confirmation was appended to `store/matches.csv` and both reports are MATCHED |

`score` and `components` are relative ranking values in [0, 1], **not probabilities**
(`scoreNote` says so in every matches response). `persisted` is `false` only in sample mode.

## Examples

```sh
B=http://127.0.0.1:8080
curl -s $B/api/health
curl -s "$B/api/reports?type=lost&q=black%20wallet&limit=5"
curl -s -X POST $B/api/reports -H 'Content-Type: application/json' \
  -d '{"type":"lost","reporterId":"U001","category":"Wallet","description":"Black leather wallet with a student ID card","color":"black","location":"Library","date":"2026-03-20"}'
curl -s "$B/api/reports/R030001/matches?k=3"
curl -s -X POST $B/api/matches -H 'Content-Type: application/json' -d '{"lostId":"R030001","foundId":"R000002"}'
```
