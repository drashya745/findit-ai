# Earlier evaluation runs (archived for traceability)

Every run below was produced by `bash evaluation/run_evaluation.sh` on the machine described in
its `environment.txt` (MacBook Air M1, 8 GB, Release build). They are kept so that the
historical numbers quoted in `docs/EVALUATION_REPORT.md` can be traced to generated files. The
**current** results are in `evaluation/results/` (run 4). All quality numbers are on the
**SYNTHETIC** set `evaluation/synthetic_v1`; none of them is real-world accuracy.

| Directory | Run (UTC) | Code version | Used in the report for |
|---|---|---|---|
| `run1_old_request_format/` | 2026-10-22 18:28 | Python received description + category + colour + brand | Earlier runtime (39.1 ms, 1.13 s, 2.66 s fit) |
| `run2_old_request_format/` | 2026-10-22 18:39 | same as run 1 | Pre-fix TF-IDF binary results (dev F1 0.833 / 0.923, test 0.700 / 0.762), R900008 = 0.66, runtime 39.2 ms / 1.13 s / 2.59 s fit |
| `run3_description_only/` | 2026-10-23 04:08 | description-only Python input | Run-to-run variation (30.4 ms, 840 ms) |
| `../` (current) | see `../environment.txt` | description-only, plus match confirmation | All current tables |

Ranking files (`synthetic_v1_rankings.csv`) aren't archived for runs 1–3; their metric files are.
Runs 1 and 2 have byte-identical quality metrics, as do runs 3 and 4. Only runtime differs within
each pair.
