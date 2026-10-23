"""Tests for the JSON Lines service, run as a real subprocess exactly as C++ runs it."""

import json
import os
import subprocess
import sys
from pathlib import Path

ML_DIR = Path(__file__).resolve().parents[1]


def run_service(lines, *, python_args=("-m", "findit_ml.service")):
    env = dict(os.environ, PYTHONPATH=str(ML_DIR))
    completed = subprocess.run(
        [sys.executable, *python_args],
        input="".join(line + "\n" for line in lines),
        capture_output=True, text=True, env=env, timeout=120, check=False,
    )
    messages = [json.loads(line) for line in completed.stdout.splitlines()]
    return completed.returncode, messages, completed.stderr


def request(kind, request_id, **fields):
    return json.dumps({"schema_version": "1.0", "type": kind, "request_id": request_id, **fields})


DOCUMENTS = [
    {"description": "Black leather wallet with student card", "category": "Wallet", "color": "black"},
    {"description": "Green folding umbrella", "category": "Umbrella", "color": "green"},
    {"description": "Brown wallet without cards", "category": "Wallet", "color": "brown", "brand": ""},
]
LOST = {"report_id": "R000001", "description": "Lost black leather wallet, student card",
        "category": "Wallet", "color": "black", "brand": ""}
CANDIDATES = [
    {"report_id": "R000002", "description": "Green folding umbrella", "category": "Umbrella",
     "color": "green", "brand": ""},
    {"report_id": "R000003", "description": "Black leather wallet with student card",
     "category": "Wallet", "color": "black", "brand": ""},
]


def test_ready_fit_rank_shutdown():
    code, messages, _ = run_service([
        request("fit", "1", documents=DOCUMENTS),
        request("rank", "2", top_k=5, lost=LOST, candidates=CANDIDATES),
        request("shutdown", "3"),
        request("rank", "4", top_k=5, lost=LOST, candidates=CANDIDATES),  # Never processed.
    ])
    assert code == 0
    ready, fitted, ranked = messages
    assert ready["type"] == "ready" and ready["status"] == "ok"
    assert fitted == {"schema_version": "1.0", "type": "fit_result", "request_id": "1",
                      "status": "ok", "documents_fitted": 3,
                      "vocabulary_size": fitted["vocabulary_size"]}
    assert ranked["type"] == "rank_result" and ranked["request_id"] == "2"
    assert ranked["lost_has_text"] is True
    assert [m["candidate_id"] for m in ranked["matches"]] == ["R000003", "R000002"]
    assert [m["rank"] for m in ranked["matches"]] == [1, 2]
    top = ranked["matches"][0]
    assert 0 < top["similarity"] <= 1 and "Exact shared terms" in top["explanation"]


def test_only_description_is_scored():
    """Category/colour/brand are accepted (backward compatibility) but never enter the text score."""
    with_fields = [request("fit", "1", documents=DOCUMENTS),
                   request("rank", "2", top_k=5, lost=LOST, candidates=CANDIDATES)]
    strip = lambda r: {"report_id": r["report_id"], "description": r["description"]}  # noqa: E731
    without_fields = [request("fit", "1", documents=[{"description": d["description"]} for d in DOCUMENTS]),
                      request("rank", "2", top_k=5, lost=strip(LOST),
                              candidates=[strip(c) for c in CANDIDATES])]
    swapped = [request("fit", "1", documents=DOCUMENTS),
               request("rank", "2", top_k=5, lost=dict(LOST, category="Umbrella", color="green"),
                       candidates=[dict(c, category="Wallet", color="black") for c in CANDIDATES])]
    _, a, _ = run_service(with_fields)
    _, b, _ = run_service(without_fields)
    _, c, _ = run_service(swapped)
    assert a[0]["text_fields"] == ["description"]
    assert a[1] == b[1] == c[1]  # Same fit (vocabulary size) regardless of structured fields.
    assert a[2]["matches"] == b[2]["matches"] == c[2]["matches"]
    umbrella = next(m for m in a[2]["matches"] if m["candidate_id"] == "R000002")
    assert umbrella["shared_terms"] == []  # "green"/"umbrella" are only structured fields.


def test_errors_are_reported_and_service_keeps_running():
    code, messages, _ = run_service([
        request("rank", "1", top_k=5, lost=LOST, candidates=CANDIDATES),  # Before fit.
        "{not json",
        json.dumps({"schema_version": "9.9", "type": "fit", "request_id": "3", "documents": []}),
        request("fit", "4", documents="nope"),
        request("fit", "5", documents=[{"description": "pls help"}]),  # No usable text.
        request("fit", "6", documents=DOCUMENTS),
        request("rank", "7", top_k=0, lost=LOST, candidates=CANDIDATES),
        request("rank", "8", top_k=5, lost={"description": "x"}, candidates=CANDIDATES),
        request("rank", "9", top_k=5, lost=LOST, candidates=[CANDIDATES[0], CANDIDATES[0]]),
        request("explode", "10"),
        request("rank", "11", top_k=5, lost=LOST, candidates=CANDIDATES),
    ])
    assert code == 0
    responses = messages[1:]
    codes = [r.get("error", {}).get("code") for r in responses]
    assert codes == ["not_fitted", "invalid_json", "invalid_request", "invalid_request",
                     "invalid_request", None, "invalid_request", "invalid_request",
                     "invalid_request", "invalid_request", None]
    assert responses[0]["request_id"] == "1" and responses[1]["request_id"] is None
    assert "schema_version" in responses[2]["error"]["message"]
    assert "Duplicate candidate ID" in responses[8]["error"]["message"]
    assert responses[-1]["status"] == "ok"


def test_lost_without_text():
    _, messages, _ = run_service([
        request("fit", "1", documents=DOCUMENTS),
        request("rank", "2", top_k=5, lost=dict(LOST, description="   "), candidates=CANDIDATES),
    ])
    assert messages[2]["lost_has_text"] is False and messages[2]["matches"] == []


def test_documented_examples_match_the_service():
    """docs/schemas/examples must stay identical to what the service actually returns."""
    examples = ML_DIR.parent / "docs" / "schemas" / "examples"
    load = lambda name: json.loads((examples / name).read_text(encoding="utf-8"))  # noqa: E731
    _, messages, _ = run_service([json.dumps(load("fit_request.json")),
                                  json.dumps(load("rank_request.json"))])
    ready, fitted, ranked = messages
    assert ready == dict(load("ready.json"), versions=ready["versions"])
    assert fitted == load("fit_response.json")
    assert ranked == load("rank_response.json")
    schema = json.loads((ML_DIR.parent / "docs" / "schemas" / "findit_matcher_v1.schema.json")
                        .read_text(encoding="utf-8"))
    match_fields = set(schema["$defs"]["match"]["required"])
    assert all(set(m) == match_fields for m in ranked["matches"])


def test_missing_dependency_is_reported():
    # Make "import sklearn" fail, then run the service module as C++ would.
    script = ("import sys, runpy; sys.modules['sklearn'] = None; "
              "runpy.run_module('findit_ml.service', run_name='__main__')")
    code, messages, _ = run_service([], python_args=("-c", script))
    assert code == 3
    assert messages[0]["type"] == "ready" and messages[0]["status"] == "error"
    assert messages[0]["error"]["code"] == "missing_dependency"
    assert "pip install -r ml/requirements.txt" in messages[0]["error"]["message"]
