# Design

## Responsibilities

| Layer | Classes | Responsibility |
|---|---|---|
| Domain | `User`, `ItemReport` (abstract), `LostReport`, `FoundReport`, `Date`, `MatchResult` | Valid objects only. Each constructor validates and throws `std::invalid_argument` |
| Storage | `ReportRepository`, `persistence::*` (`ReportCsvStore`) | Owns reports and users; canonical CSV load/save/append |
| Ingestion | `CsvReader`, `DatasetConfig`, `DatasetAdapter`, `DatasetLoader`, `IngestSummary` | Raw CSV → validated canonical records; logs rejected rows; reproducible summary |
| Matching | `SimilarityStrategy`, `KeywordBaselineStrategy`, `PythonTfidfStrategy`, `FallbackStrategy`, `MatchEngine`, `MatchingConfig` | Candidate selection (engine) is kept separate from scoring (strategies) |
| Integration | `PythonProcess`, `PythonTextClient` | Child process plus JSON protocol |
| Application | `ReportService`, `main.cpp`, `tools/*.cpp` | Use cases (submit, suggest, explain, confirm) and user interaction |

## OOP concepts and where they appear

| Concept | Where | How |
|---|---|---|
| **Abstraction** | `include/findit/ItemReport.h`, `include/findit/SimilarityStrategy.h` | `ItemReport::type()` and `dateLabel()` are pure virtual. `SimilarityStrategy` is a pure interface. Callers depend on these and not on concrete classes |
| **Encapsulation** | every domain class | All fields are private. State changes go through validated methods (`setStatus` enforces OPEN→MATCHED→CLOSED). Outside code gets `const ItemReport*`, so only `ReportRepository::updateStatus` can change a status, and `ReportService::confirmMatch` is the only use case that does |
| **Inheritance** | `LostReport`, `FoundReport` : `ItemReport`; strategies : `SimilarityStrategy`; `PythonMatcherError` : `StrategyUnavailableError` : `std::runtime_error` | Shared fields and validation live once in the base class |
| **Runtime polymorphism** | `printReports` (`summary()`), `ItemReport::details()`, `MatchEngine::findMatches` (`strategy_->score`) | `FoundReport::details()` overrides and extends the base version. The engine works with any strategy |
| **Constructors / RAII** | `ItemReport(id, input)`, `PythonProcess` | Objects are valid from construction. `~PythonProcess()` always stops the child process |
| **Destructors** | virtual destructors in `ItemReport` and `SimilarityStrategy` | Deleting through a base pointer (`unique_ptr<ItemReport>`) is safe |
| **Ownership** | `ReportRepository` (`vector<unique_ptr<ItemReport>>`), `MatchEngine` (`unique_ptr<SimilarityStrategy>`) | No raw owning pointers. Copying a report is deleted to prevent slicing. `ReportService` holds references |
| **const-correctness** | getters are `const noexcept`; `score()` is `const` | Lazily started Python state is `mutable` and documented as a cache |
| **Design patterns** | Strategy (`SimilarityStrategy`), Decorator/Fallback (`FallbackStrategy`), Facade (`ReportService`), Template Method (`details()` calls `dateLabel()`), Adapter (`DatasetAdapter`) | |

## C++ / Python boundary

- **C++ owns:** the domain model, validation, persistence, candidate selection (status, self,
  category, date), the structured scores (category, colour, location), how scores are combined,
  ranking, top-K, and the user interface.
- **Python owns:** text similarity only, from the **description field alone**. It normalizes text, fits TF-IDF once and computes cosine
  similarity. It explains its result with exact shared terms and general similarity. It holds no
  application state other than the fitted model.
- **Protocol:** JSON Lines over a child process's stdin/stdout (`docs/INTEGRATION.md`,
  `docs/schemas/`), versioned with `schema_version`. C++ validates every field.
- **Failure policy:** `PythonMatcherError` (a `StrategyUnavailableError`) → `FallbackStrategy`
  switches to the C++ keyword baseline for the session and says so. Evaluation runs with no
  fallback, so failures there are loud.
- **Why this split:** scikit-learn gives a tested TF-IDF implementation. C++ keeps the OOP core
  independent of Python, and the app still works without it (`--text-engine keyword`).

## Key decisions

| Decision | Reason |
|---|---|
| Copy the dataset to `data/raw/` (read-only) and never edit it | Reproducibility; SHA-256 recorded in `summary.json` |
| Configurable column mapping | No source column names in code |
| `brand = "None"` → unknown | 1,652 rows; "None" is a missing value, not a brand |
| Date filter off | In this data the found date precedes the lost date in about half of the generated pairs |
| Category blocking on by default | Fast (≈480 candidates instead of 15,000). Costs recall on cross-category reports (measured in evaluation) |
| Persistent Python process, fit once | Fitting 30k descriptions takes ~2 s; queries then take ~30 ms |
| Python receives the description only | Category, colour and location are scored once, in C++; the components stay independent |
| Confirmations stored as an append-only log (`data/store/matches.csv`) replayed through `ReportService::confirmMatch` | The baseline `reports.csv` is never rewritten; each replayed row is validated by the same rules (both OPEN), so a corrupt log fails loudly with its line number |
| Missing values never count as agreement | Avoids sparse reports outranking complete ones |
| No weights tuned on evaluation data | No trustworthy labels; avoids overfitting a 23-query synthetic set |
