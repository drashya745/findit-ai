"""JSON Lines matching service used by the C++ application.

    PYTHONPATH=ml python -m findit_ml.service

Protocol (schema_version "1.0", documented in docs/INTEGRATION.md and docs/schemas/):
  * On start the service writes one "ready" message. If a dependency is missing it writes a
    ready message with status "error" (code "missing_dependency") and exits with code 3.
  * Then it reads one JSON request per line on stdin and writes exactly one JSON response
    per line on stdout: "fit" (learn TF-IDF once), "rank" (score candidates), "shutdown".
  * End of stdin also shuts down. stdout carries protocol messages only; diagnostics go to stderr.

Text input: only `description` is used for TF-IDF. Category, colour and location are scored by
the C++ application, so they must not also enter the text score. `category`, `color` and
`brand` are still accepted in requests (older clients sent them) but are ignored. The ready
message lists the fields actually used in `text_fields`.
"""

from __future__ import annotations

import json
import platform
import sys
from typing import Any

SCHEMA_VERSION = "1.0"
EXIT_MISSING_DEPENDENCY = 3
TEXT_FIELDS = ("description", "category", "color", "brand")  # Accepted in requests.
SCORED_TEXT_FIELDS = ("description",)  # Actually used for TF-IDF.


class RequestError(Exception):
    def __init__(self, code: str, message: str) -> None:
        super().__init__(message)
        self.code = code


def _emit(message: dict[str, Any]) -> None:
    # ASCII-only JSON keeps each message on a single line regardless of content.
    sys.stdout.write(json.dumps(message, ensure_ascii=True, separators=(",", ":")) + "\n")
    sys.stdout.flush()


def _error(request_id: Any, code: str, message: str) -> dict[str, Any]:
    return {"schema_version": SCHEMA_VERSION, "type": "error", "request_id": request_id,
            "status": "error", "error": {"code": code, "message": message}}


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise RequestError("invalid_request", message)


def _text_record(value: Any, where: str, *, need_id: bool) -> dict[str, Any]:
    _require(isinstance(value, dict), f"{where} must be an object")
    record: dict[str, Any] = {}
    for field in TEXT_FIELDS:
        field_value = value.get(field, "")
        _require(field_value is None or isinstance(field_value, str),
                 f"{where}.{field} must be a string")
        record[field] = field_value or ""
    if need_id:
        report_id = value.get("report_id")
        _require(isinstance(report_id, str) and report_id.strip() != "",
                 f"{where}.report_id must be a non-empty string")
        record["report_id"] = report_id
    return record


class Service:
    def __init__(self, matcher_factory) -> None:
        self._factory = matcher_factory
        self._matcher = None

    def handle(self, request: Any) -> dict[str, Any] | None:
        _require(isinstance(request, dict), "request must be a JSON object")
        _require(request.get("schema_version") == SCHEMA_VERSION,
                 f"unsupported schema_version {request.get('schema_version')!r}; "
                 f"expected {SCHEMA_VERSION!r}")
        kind = request.get("type")
        if kind == "shutdown":
            return None
        if kind == "fit":
            return self._fit(request)
        if kind == "rank":
            return self._rank(request)
        raise RequestError("invalid_request", f"unknown request type {kind!r}")

    def _fit(self, request: dict[str, Any]) -> dict[str, Any]:
        documents = request.get("documents")
        _require(isinstance(documents, list), "documents must be an array")
        records = [_text_record(d, f"documents[{i}]", need_id=False) for i, d in enumerate(documents)]
        matcher = self._factory()
        try:
            matcher.fit(records)
        except ValueError as exc:  # No usable text at all.
            raise RequestError("invalid_request", str(exc)) from exc
        self._matcher = matcher
        return {"schema_version": SCHEMA_VERSION, "type": "fit_result",
                "request_id": request.get("request_id"), "status": "ok",
                "documents_fitted": matcher.n_documents_fitted_,
                "vocabulary_size": matcher.vocabulary_size}

    def _rank(self, request: dict[str, Any]) -> dict[str, Any]:
        if self._matcher is None:
            raise RequestError("not_fitted", "send a 'fit' request before 'rank'")
        top_k = request.get("top_k")
        _require(isinstance(top_k, int) and not isinstance(top_k, bool) and top_k >= 1,
                 "top_k must be a positive integer")
        lost = _text_record(request.get("lost"), "lost", need_id=True)
        raw_candidates = request.get("candidates")
        _require(isinstance(raw_candidates, list), "candidates must be an array")
        candidates = [_text_record(c, f"candidates[{i}]", need_id=True)
                      for i, c in enumerate(raw_candidates)]
        try:
            matches = self._matcher.rank(lost, candidates, top_k=top_k)
        except ValueError as exc:  # e.g. duplicate candidate IDs.
            raise RequestError("invalid_request", str(exc)) from exc
        return {"schema_version": SCHEMA_VERSION, "type": "rank_result",
                "request_id": request.get("request_id"), "status": "ok",
                "lost_has_text": bool(self._matcher.document(lost)),
                "matches": [m.to_dict() for m in matches]}


def main() -> int:
    try:
        import sklearn

        from findit_ml.matcher import TextMatcher
    except ImportError as exc:
        _emit({"schema_version": SCHEMA_VERSION, "type": "ready", "status": "error",
               "error": {"code": "missing_dependency",
                         "message": f"{exc}. Install the Python dependencies with: "
                                    ".venv/bin/pip install -r ml/requirements.txt"}})
        return EXIT_MISSING_DEPENDENCY

    _emit({"schema_version": SCHEMA_VERSION, "type": "ready", "status": "ok",
           "service": "findit_ml.service", "text_fields": list(SCORED_TEXT_FIELDS),
           "versions": {"python": platform.python_version(), "scikit_learn": sklearn.__version__}})

    # Description-only documents: no category/colour/brand appended (see module docstring).
    service = Service(lambda: TextMatcher(extra_fields=()))
    for line in sys.stdin:
        if not line.strip():
            continue
        request_id = None
        try:
            request = json.loads(line)
            if isinstance(request, dict):
                request_id = request.get("request_id")
            response = service.handle(request)
        except json.JSONDecodeError as exc:
            response = _error(None, "invalid_json", f"request is not valid JSON: {exc}")
        except RequestError as exc:
            response = _error(request_id, exc.code, str(exc))
        except Exception as exc:  # Report, keep serving.
            print(f"findit_ml.service: internal error: {exc!r}", file=sys.stderr)
            response = _error(request_id, "internal_error", f"{type(exc).__name__}: {exc}")
        if response is None:
            break
        _emit(response)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (BrokenPipeError, KeyboardInterrupt):
        sys.exit(1)
