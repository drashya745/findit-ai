#!/usr/bin/env bash
# Reproduces every number in docs/EVALUATION_REPORT.md. Run from anywhere:
#   bash evaluation/run_evaluation.sh
# Needs: .venv with ml/requirements.txt and CMake on PATH (e.g. .venv/bin/cmake).
# Writes only to build-release/ and evaluation/results/. Never touches data/raw/.
set -euo pipefail
cd "$(dirname "$0")/.."
export PATH="$PWD/.venv/bin:$PATH"
RESULTS=evaluation/results
SET=evaluation/synthetic_v1

echo "== Release build (timings use optimized code)"
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build build-release -j --target findit_eval findit_ingest > /dev/null

if [ ! -f data/processed/reports.csv ]; then
  echo "== data/processed missing: running findit_ingest"
  ./build-release/findit_ingest > /dev/null
fi

rm -f "$RESULTS"/*.csv "$RESULTS"/summary.md "$RESULTS"/environment.txt
mkdir -p "$RESULTS"

echo "== Environment"
{
  echo "date_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "machine_model: $(sysctl -n hw.model)"
  echo "cpu: $(sysctl -n machdep.cpu.brand_string)"
  echo "logical_cpus: $(sysctl -n hw.ncpu)"
  echo "memory_gb: $(( $(sysctl -n hw.memsize) / 1073741824 ))"
  echo "macos: $(sw_vers -productVersion)"
  echo "compiler: $(c++ --version | head -1)"
  echo "build_type: Release"
  echo "python: $(.venv/bin/python --version 2>&1)"
  echo "scikit_learn: $(.venv/bin/python -c 'import sklearn; print(sklearn.__version__)')"
  echo "note: single run, other applications not controlled; timings are wall-clock (std::chrono::steady_clock)"
} > "$RESULTS/environment.txt"
cat "$RESULTS/environment.txt"

echo "== Quality: synthetic set, both strategies, blocking on/off"
./build-release/findit_eval --reports "$SET/reports.csv" --users "$SET/users.csv" \
  --dataset-name synthetic_v1 --rankings-out "$RESULTS/synthetic_v1_rankings.csv" \
  --runtime-out "$RESULTS/runtime.csv" --strategies keyword,python --blocking on,off

echo "== Runtime: real dataset (no labels; timing only), fixed random sample of lost reports"
./build-release/findit_eval --reports data/processed/reports.csv --users data/processed/users.csv \
  --dataset-name real_30k_sample200 --runtime-out "$RESULTS/runtime.csv" \
  --strategies keyword,python --blocking on --lost-sample 200 --seed 42
./build-release/findit_eval --reports data/processed/reports.csv --users data/processed/users.csv \
  --dataset-name real_30k_sample20 --runtime-out "$RESULTS/runtime.csv" \
  --strategies keyword,python --blocking off --lost-sample 20 --seed 42

echo "== Metrics"
(cd ml && ../.venv/bin/python -m findit_ml.evaluation.run_eval \
  --queries "../$SET/queries.csv" --pairs "../$SET/pairs.csv" --reports "../$SET/reports.csv" \
  --rankings "../$RESULTS/synthetic_v1_rankings.csv" --out-dir "../$RESULTS")

echo "== Runtime table"
column -s, -t < "$RESULTS/runtime.csv"
echo "== Raw dataset unchanged:"
shasum -a 256 data/raw/lost_found_dataset_realistic_metadata_30k.csv
