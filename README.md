# FindIt AI — Intelligent Campus Lost-and-Found Matcher

FindIt AI is an intelligent campus lost-and-found system featuring an object-oriented **C++17** core, a **Python scikit-learn** TF-IDF text similarity service communicating over a structured JSON pipe, a local JSON HTTP API server, an interactive terminal CLI, and a modern single-page **React + TypeScript + Tailwind CSS** web application.

---

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [Features and Scope](#2-features-and-scope)
3. [Technology Stack and Tools](#3-technology-stack-and-tools)
4. [Architecture and Component Interaction](#4-architecture-and-component-interaction)
5. [Directory and File Structure](#5-directory-and-file-structure)
6. [Complete Class and Object Documentation](#6-complete-class-and-object-documentation)
7. [OOP Concepts Used in the Project](#7-oop-concepts-used-in-the-project)
8. [Core Workflows and Data Flow](#8-core-workflows-and-data-flow)
9. [Matching Algorithms and AI/ML](#9-matching-algorithms-and-aiml)
10. [Data Model and Persistence](#10-data-model-and-persistence)
11. [HTTP API Reference](#11-http-api-reference)
12. [Frontend Documentation](#12-frontend-documentation)
13. [Build and Installation Instructions](#13-build-and-installation-instructions)
14. [Running the Application](#14-running-the-application)
15. [Testing and Verification](#15-testing-and-verification)
16. [Configuration Reference](#16-configuration-reference)
17. [Error Handling, Security, and Limitations](#17-error-handling-security-and-limitations)
18. [Troubleshooting](#18-troubleshooting)
19. [Development Workflow and Contribution Guide](#19-development-workflow-and-contribution-guide)
20. [Future Enhancements](#20-future-enhancements)
21. [Project Summary for Academic Evaluation and Interviews](#21-project-summary-for-academic-evaluation-and-interviews)

---

## 1. Project Overview

### Problem Statement
University campuses handle thousands of misplaced belongings each semester—water bottles, student IDs, keys, electronics, notebooks, and bags. Conventional campus lost-and-found operations suffer from critical friction points:
- **Dispersed physical desks:** Enquiries require walking between academic buildings, library checkouts, and security offices.
- **Inflexible keyword lookups:** Manual record books or basic spreadsheet searches require exact terminology. If a student reports a *"Navy hydroflask with gym sticker"* while the desk records a *"blue water bottle"*, simple keyword filters fail.
- **Labor-intensive manual pairing:** Desk staff must manually read hundreds of pending descriptions to guess matches.
- **Weak accountability:** Paper logs lack strict lifecycle validation, leading to double-handover errors or unverified closures.

### Proposed Solution
FindIt AI implements an automated, explainable matching pipeline:
1. **Unified Reporting:** Students log lost belongings; desk staff register retrieved items.
2. **Hybrid Scoring Engine:** Combines sub-word/token TF-IDF cosine similarity on free-form descriptions with C++ domain scoring over categorical metadata (location zones, category groups, and colour groups).
3. **Ranked Match Suggestions:** Generates top candidate pairings with human-readable explanations answering *"Why this item?"* (e.g. shared rare terms, matching category, proximate campus zone).
4. **Human-in-the-Loop Confirmation:** Suggestions never automatically close incidents. A person verifies the physical item and confirms the match, which atomically transitions both records to `MATCHED` and permanently persists the decision log.

### Target Users & Use Cases
- **Students & Campus Community:** Submit detailed lost reports; view ranked candidate matches with reasons; browse active reports.
- **Lost & Found Desk Officers:** Register found items; search inventory; review candidate lost reports; confirm physical handovers.
- **Administrators & Auditors:** Review ingestion statistics, match audit logs, and data integrity metrics.

### Status and Dataset Provenance
> [!NOTE]
> **Dataset Disclosure:** The 30,000-row dataset provided with this project (`data/raw/lost_found_dataset_realistic_metadata_30k.csv`) is **synthetic sample data** generated for benchmarking. It does not represent verified, real-world campus incidents. Quality evaluations reported in `docs/EVALUATION_REPORT.md` are calculated against self-authored synthetic test sets. Matching scores represent **relative ranking values in $[0, 1]$**, not calibrated real-world probabilities.

---

## 2. Features and Scope

### Feature Matrix

| Feature | CLI | HTTP API | Web UI | Implementation Status | Verification Method |
|---|:---:|:---:|:---:|:---:|---|
| **Lost Item Reporting** | ✅ | ✅ | ✅ | Implemented & Verified | Automated C++ unit tests, API tests, Vitest UI test |
| **Found Item Registration** | ✅ | ✅ | ✅ | Implemented & Verified | `test_domain.cpp`, Vitest `ReportPage.test.tsx` |
| **User Registration** | ✅ | ✅ | ✅ | Implemented & Verified | `test_domain.cpp`, `POST /api/users`, Vitest |
| **Domain Validation** | ✅ | ✅ | ✅ | Implemented & Verified | Strict character length, enum, ISO date validation |
| **CSV Dataset Ingestion** | ✅ | N/A | N/A | Implemented & Verified | `tools/findit_ingest.cpp`, checksum verification |
| **Atomic Persistence** | ✅ | ✅ | ✅ | Implemented & Verified | Temp-file rename, `data/store/matches.csv` log |
| **Data Directory Lock** | ✅ | ✅ | ✅ | Implemented & Verified | Kernel `flock` on `data/store/.findit.lock` |
| **TF-IDF Python Matcher** | ✅ | ✅ | ✅ | Implemented & Verified | Pipe JSON Lines, 73 pytest unit tests |
| **Keyword Baseline Matcher** | ✅ | ✅ | ✅ | Implemented & Verified | Fallback & standalone C++ strategy |
| **Explainable Matching** | ✅ | ✅ | ✅ | Implemented & Verified | Component score breakdown + term explanations |
| **Human Match Confirmation** | ✅ | ✅ | ✅ | Implemented & Verified | Lifecycle validation (`OPEN -> MATCHED`), audit log |
| **Keyword/Faceted Search** | ❌ (Listing) | ✅ | ✅ | Implemented & Verified | Multi-word containment, category, type, status |
| **Server-Side Pagination** | ❌ | ✅ | ✅ | Implemented & Verified | `limit` & `offset` query parameters |
| **User Authentication / RBAC** | ❌ | ❌ | ❌ | Excluded by Design | Single-tenant campus workstation / kiosk scope |
| **Network Multi-Tenancy** | ❌ | ❌ | ❌ | Excluded by Design | Server strictly bound to `127.0.0.1` |

---

## 3. Technology Stack and Tools

### Core Engineering Stack
- **Language Standard:** C++17 (`clang` / Apple Clang on macOS, GCC 9+ compatible)
- **Build System:** CMake ≥ 3.20, generating native Makefiles
- **Text & Machine Learning Engine:** Python 3.14 with `scikit-learn`, `numpy`, `scipy`, `pandas`
- **Inter-Process Protocol:** JSON Lines over bidirectional anonymous POSIX pipes (`stdin`/`stdout`) via `posix_spawnp`
- **HTTP Server:** Local loopback HTTP daemon using header-only `cpp-httplib` 0.18.3 (MIT)
- **Serialization:** Modern C++ JSON parsing via `nlohmann/json` 3.11.3 (MIT)
- **Frontend SPA:** React 18.3, TypeScript 5.9, Vite 6.4, Tailwind CSS 3.4, Lucide React icons
- **Test Frameworks:**
  - C++: Custom lightweight runner (`TestFramework.h`) integrated with CTest
  - Python: `pytest` 9.1
  - Frontend: `vitest` 4.1 + React Testing Library + `@testing-library/jest-dom`

### External Dependencies & Vendoring
| Dependency | Version | License | Placement | Purpose |
|---|---|---|---|---|
| `nlohmann/json` | 3.11.3 | MIT | `third_party/nlohmann/` | C++ JSON parsing and serialization |
| `cpp-httplib` | 0.18.3 | MIT | `third_party/httplib/` | C++ single-threaded HTTP loopback server |
| `scikit-learn` | 1.6+ | BSD-3-Clause | `.venv/lib/` | TF-IDF vectorizers and sparse cosine similarity |
| `lucide-react` | 0.577.0 | ISC | `frontend/node_modules/` | Accessible UI iconography |

---

## 4. Architecture and Component Interaction

FindIt AI follows a layered architecture where **C++ owns all domain rules, validation, persistence, and state transitions**, while **Python is an isolated, stateless text-processing worker**.

```mermaid
flowchart TD
    subgraph UI_Layer["Presentation Layer"]
        BROWSER["Web Browser (React 18 + TS)"]
        CLI_APP["findit Interactive CLI (main.cpp)"]
    end

    subgraph Server_Layer["HTTP & Adapter Layer"]
        VITE_DEV["Vite Dev Server (Proxy /api)"]
        HTTP_SRV["findit_server (cpp-httplib)"]
        API_HND["ApiHandlers (Request routing & JSON mapping)"]
    end

    subgraph Domain_Layer["Application & Core Domain Layer (C++17)"]
        CTX["AppContext (Session & Facade)"]
        LOCK["DataDirLock (Kernel flock)"]
        SVC["ReportService (Business Rules & Orchestration)"]
        REPO["ReportRepository (In-Memory Aggregate Store)"]
        ENG["MatchEngine (Candidate Filtering & Ranking)"]

        subgraph Domain_Models["Domain Entities"]
            ITEM["ItemReport (Abstract Base)"]
            LOST["LostReport"]
            FOUND["FoundReport"]
            USER["User"]
            MATCH["MatchResult"]
        end
    end

    subgraph Matcher_Layer["Strategy & Machine Learning Layer"]
        STRAT["SimilarityStrategy (Interface)"]
        FALLBACK["FallbackStrategy (Proxy/Decorator)"]
        KW_STRAT["KeywordBaselineStrategy (C++ Dice Overlap)"]
        PY_STRAT["PythonTfidfStrategy (C++)"]
        CLIENT["PythonTextClient (Protocol Translator)"]
        PROC["PythonProcess (posix_spawnp RAII Wrapper)"]

        subgraph Python_Subsystem["Python 3 Worker (ml/findit_ml)"]
            PY_SERV["findit_ml.service (JSON Lines IO)"]
            MATCHER["TextMatcher (Word + Char TF-IDF)"]
        end
    end

    subgraph Storage_Layer["Persistence Layer"]
        CSV_STORE["persistence::* (ReportCsvStore)"]
        DISK_PROC[("data/processed/<br/>users.csv, reports.csv")]
        DISK_STORE[("data/store/<br/>users.csv, reports.csv, matches.csv")]
    end

    BROWSER -->|HTTP JSON| VITE_DEV
    VITE_DEV -->|Reverse Proxy :8080| HTTP_SRV
    HTTP_SRV --> API_HND
    API_HND --> CTX
    CLI_APP --> CTX

    CTX --> LOCK
    CTX --> SVC
    CTX --> REPO
    CTX --> ENG

    SVC --> REPO
    SVC --> ENG
    REPO --> ITEM
    ITEM <|-- LOST
    ITEM <|-- FOUND
    REPO --> USER

    ENG --> STRAT
    STRAT <|.. FALLBACK
    STRAT <|.. KW_STRAT
    STRAT <|.. PY_STRAT

    FALLBACK o-- KW_STRAT
    FALLBACK o-- PY_STRAT

    PY_STRAT --> CLIENT
    CLIENT --> PROC
    PROC <== "JSON Lines Pipe (stdin/stdout)" ==> PY_SERV
    PY_SERV --> MATCHER

    CTX --> CSV_STORE
    CSV_STORE <--> DISK_PROC
    CSV_STORE <--> DISK_STORE
```

### Sequence: Report to Match Workflow

```mermaid
sequenceDiagram
    autonumber
    actor Officer as Desk Officer / Student
    participant UI as Web Frontend (React)
    participant API as findit_server / ApiHandlers
    participant Svc as ReportService
    participant Repo as ReportRepository
    participant Eng as MatchEngine
    participant PyStrat as PythonTfidfStrategy
    participant PyProc as Python Worker (service.py)
    participant Store as persistence (CSV)

    Officer->>UI: Submit Lost Report (Category, Description, Color, Location, Date)
    UI->>API: POST /api/reports {type: "lost", ...}
    API->>Svc: submitReport(Lost, ReportInput)
    Svc->>Svc: Validate fields (length, date format, reporter ID)
    Svc->>Store: appendReport("data/store/reports.csv", report)
    Store-->>Svc: Write successful
    Svc->>Repo: addReport(std::make_unique<LostReport>(...))
    API-->>UI: 201 Created {id: "R030001", persisted: true}

    Officer->>UI: Request Matches for R030001
    UI->>API: GET /api/reports/R030001/matches?k=3
    API->>Svc: suggestMatches("R030001", 3)
    Svc->>Repo: findById("R030001"), openFoundReports()
    Svc->>Eng: findMatches(lostReport, eligibleCandidates, 3)
    Eng->>Eng: Category blocking & status filter
    Eng->>PyStrat: score(lostReport, filteredCandidates)

    alt First query of session
        PyStrat->>PyProc: {"type":"fit", "documents":[...descriptions]}
        PyProc-->>PyStrat: {"type":"fit_result", "vocabulary_size":3210}
    end

    PyStrat->>PyProc: {"type":"rank", "lost":{description}, "candidates":[{id, description}]}
    PyProc-->>PyStrat: {"type":"rank_result", "matches":[{id, similarity, shared_terms, explanation}]}

    PyStrat->>PyStrat: Combine: 0.60*text + 0.20*loc + 0.10*cat + 0.10*col
    PyStrat-->>Eng: std::vector<MatchResult>
    Eng->>Eng: Sort descending by score, tie-break by ID, assign ranks #1..#3
    Eng-->>Svc: Top 3 MatchResults
    API-->>UI: 200 OK {matches: [...], scoreNote: "..."}
    UI-->>Officer: Render Top 3 Candidate Cards with score bars & reasons

    Officer->>UI: Click "Confirm Match" with Candidate R000452
    UI->>UI: Prompt ConfirmDialog modal
    Officer->>UI: Confirm "Yes"
    UI->>API: POST /api/matches {lostId: "R030001", foundId: "R000452"}
    API->>Svc: confirmMatch("R030001", "R000452")
    Svc->>Svc: Validate both exist, one LOST, one FOUND, both OPEN
    Svc->>Store: appendConfirmedMatch("data/store/matches.csv", R030001, R000452, date)
    Store-->>Svc: Flushed to disk
    Svc->>Repo: updateStatus("R030001", MATCHED)
    Svc->>Repo: updateStatus("R000452", MATCHED)
    API-->>UI: 200 OK {lost: {status: "matched"}, found: {status: "matched"}, persisted: true}
    UI-->>Officer: Success notification; statuses updated
```

---

## 5. Directory and File Structure

```
oops_project/
├── CMakeLists.txt                    # Root build configuration for core library, CLI, tools, tests
├── README.md                         # Primary project documentation and operations manual
├── pytest.ini                        # Python test configuration
├── config/
│   ├── dataset_mapping.conf          # Column mappings & missing-value vocabulary for ingestion
│   └── matching.conf                 # Weights, top-k, zones, colour groups, Python settings
├── data/
│   ├── raw/
│   │   ├── README.md                 # Dataset provenance and licensing note
│   │   └── lost_found_dataset_...csv # Read-only 30k source synthetic dataset (3.3 MB)
│   ├── processed/                    # Output of findit_ingest (users.csv, reports.csv, summary.json)
│   └── store/                        # Active application storage: app reports, users, matches.csv, .findit.lock
├── docs/                             # Exhaustive technical documentation
│   ├── API.md                        # HTTP endpoint specs, error codes, security model
│   ├── ARCHITECTURE.md               # Architecture diagrams and sequence flows
│   ├── DATASET.md / DATA_MAPPING.md  # Provenance, column mappings, and normalization rules
│   ├── DESIGN.md                     # OOP design decisions, trade-offs, and file references
│   ├── MATCHING.md / PYTHON_MATCHER.md# Similarity scoring, TF-IDF mechanics, formulas
│   └── INTEGRATION.md                # C++/Python JSON protocol specification
├── include/findit/                   # Public C++ header files
│   ├── ApiHandlers.h                 # JSON API routing, payload parsing, exception translation
│   ├── AppContext.h                  # High-level session lifecycle, options, lock management
│   ├── Csv.h                         # RFC 4180 CSV parser and serializer
│   ├── DataDirLock.h                 # RAII kernel flock locking mechanism
│   ├── DatasetAdapter.h              # Normalizes raw CSV records to canonical schema
│   ├── DatasetConfig.h               # Key-value mapping for raw dataset columns
│   ├── DatasetLoader.h               # Bulk loader populating ReportRepository from CSV
│   ├── Date.h                        # Immutable calendar date class (YYYY-MM-DD)
│   ├── FoundReport.h                 # Found item entity (heldAt field, specialized details)
│   ├── IngestSummary.h               # JSON & text reporting for data ingestion
│   ├── ItemReport.h                  # Abstract base entity for all lost/found reports
│   ├── KeyValueConfig.h              # Parser for .conf key-value configuration files
│   ├── KeywordBaselineStrategy.h     # Rule-based Dice keyword similarity matcher
│   ├── LostReport.h                  # Lost item entity
│   ├── MatchEngine.h                 # Candidate filtering, strategy invocation, top-K ranking
│   ├── MatchResult.h                 # Value object for candidate score, components, reasons
│   ├── MatchingConfig.h              # Struct containing parsed weights, groups, and options
│   ├── PythonProcess.h               # RAII posix_spawnp child process manager
│   ├── PythonTextClient.h            # Protocol serializer/deserializer talking to service.py
│   ├── PythonTfidfStrategy.h         # TF-IDF strategy combining Python text with C++ metadata
│   ├── ReportCsvStore.h              # Atomic CSV disk persistence and replay logic
│   ├── ReportRepository.h            # In-memory aggregate repository holding reports and users
│   ├── ReportService.h               # Application use-case facade enforcing domain rules
│   ├── SampleData.h                  # Built-in seed data for --sample mode
│   ├── Sha256.h                      # SHA-256 implementation for provenance verification
│   ├── SimilarityStrategy.h          # Abstract strategy interface for matching algorithms
│   ├── TextKeywords.h                # Basic C++ tokenizer, stopword filtering, keyword extraction
│   ├── Types.h                       # Enums: ReportType, ReportStatus, and string conversions
│   ├── User.h                        # Campus user entity (immutable ID, name, contact)
│   └── Validation.h                  # Stateless sanitizers and domain validators
├── ml/                               # Python ML subsystem
│   ├── requirements.txt              # Pinned Python package dependencies
│   ├── findit_ml/
│   │   ├── data.py                   # Data utilities and dataset schemas
│   │   ├── matcher.py                # Dual word/character n-gram TF-IDF cosine matcher
│   │   ├── preprocess.py             # Text cleaning and document concatenation
│   │   └── service.py                # Line-oriented JSON protocol service entry point
│   └── tests/                        # 73 unit tests covering preprocessing, TF-IDF, protocol
├── src/                              # C++ implementation files
│   ├── ApiHandlers.cpp, AppContext.cpp, Csv.cpp, DataDirLock.cpp, DatasetAdapter.cpp,
│   ├── DatasetConfig.cpp, DatasetLoader.cpp, Date.cpp, FoundReport.cpp, IngestSummary.cpp,
│   ├── ItemReport.cpp, KeyValueConfig.cpp, KeywordBaselineStrategy.cpp, LostReport.cpp,
│   ├── MatchEngine.cpp, MatchResult.cpp, MatchingConfig.cpp, PythonProcess.cpp,
│   ├── PythonTextClient.cpp, PythonTfidfStrategy.cpp, ReportCsvStore.cpp, ReportRepository.cpp,
│   ├── ReportService.cpp, SampleData.cpp, Sha256.cpp, TextKeywords.cpp, Types.cpp,
│   ├── User.cpp, Validation.cpp, and main.cpp (CLI executable entry point)
├── tools/                            # Standalone utility executables
│   ├── findit_ingest.cpp             # Pre-processes data/raw into data/processed
│   ├── findit_server.cpp             # HTTP daemon listening on 127.0.0.1:8080
│   └── findit_eval.cpp               # Headless benchmarking tool for evaluation sets
├── tests/                            # C++ unit, integration, and E2E test suites
│   ├── TestFramework.h / .cpp        # Custom lightweight assertion harness
│   ├── test_domain.cpp               # Entity validation and lifecycle tests
│   ├── test_matching.cpp             # Keyword baseline and MatchEngine tests
│   ├── test_app_context.cpp          # Facade and persistence tests
│   ├── test_data_dir_lock.cpp        # Kernel lock collision tests
│   ├── test_api.cpp                  # Mocked HTTP handler regression tests
│   └── integration/                  # End-to-end pipe tests with Python
├── third_party/                      # Vendored open-source libraries (MIT)
│   ├── httplib/httplib.h             # cpp-httplib HTTP library
│   └── nlohmann/json.hpp             # Modern JSON for C++
└── frontend/                         # Single Page Application
    ├── package.json, vite.config.ts, tailwind.config.js, tsconfig.json
    ├── src/
    │   ├── App.tsx                   # Main route configuration
    │   ├── main.tsx                  # Application bootstrap
    │   ├── index.css                 # Tailwind directives and design tokens
    │   ├── types/api.ts              # TypeScript interfaces mirroring C++ API
    │   ├── services/api.ts           # Centralized Fetch client with custom ApiError
    │   ├── components/               # Layout, Badges, States, ConfirmDialog, ReportCard
    │   └── pages/                    # DashboardPage, ReportPage, BrowsePage, MatchesPage
    └── tests/                        # 35 Vitest component and API integration tests
```

---

## 6. Complete Class and Object Documentation

### Comprehensive Class Reference Table

| Class / Struct | Header File | Primary Responsibility | Important Data Members | Important Methods | Key Relationships |
|---|---|---|---|---|---|
| `Date` | `Date.h` | Immutable calendar date (ISO 8601 YYYY-MM-DD) | `int year_`, `month_`, `day_` (private) | `parse(text)`, `toString()`, `daysSinceEpoch()`, `operator==`, `operator<` | Used by `ItemReport`, `CsvReader` |
| `User` | `User.h` | Immutable campus member entity | `string id_`, `name_`, `contact_` (private) | `id()`, `name()`, `contact()`, `toString()` | Aggregated by `ReportRepository` |
| `ReportInput` | `ItemReport.h` | Plain data carrier for unvalidated report submission | `reporterId`, `category`, `description`, `color`, `brand`, `location`, `date`, `sourceRef` | N/A (Struct) | Input parameter to `ReportService`, `ItemReport` |
| `ItemReport` | `ItemReport.h` | Abstract base entity for all lost/found items | `string id_`, `category_`, `description_`, `color_`, `location_`, `Date date_`, `ReportStatus status_`, `optional<string> brand_` | `type()=0`, `setStatus()`, `summary()`, `details()`, `dateLabel()=0` | Base class of `LostReport`, `FoundReport` |
| `LostReport` | `LostReport.h` | Concrete entity representing a lost item incident | Inherits all `ItemReport` members | `type() override`, `lostDate()`, `dateLabel() override` | Inherits from `ItemReport` |
| `FoundReport` | `FoundReport.h` | Concrete entity representing a found item retrieved | Inherits `ItemReport` + `string heldAt_` (private) | `type() override`, `heldAt()`, `details() override`, `dateLabel() override` | Inherits from `ItemReport` |
| `MatchResult` | `MatchResult.h` | Value object containing candidate score and explanations | `string lostId_`, `foundId_`, `double score_`, `int rank_`, `map<string, double> components_`, `vector<string> explanations_` | `addComponent()`, `addExplanation()`, `setRank()`, `score()`, `rank()` | Produced by `SimilarityStrategy`, consumed by `MatchEngine` |
| `SimilarityStrategy` | `SimilarityStrategy.h` | Pure abstract strategy interface for matching algorithms | None | `score(lost, candidates) = 0`, `name() = 0` | Implemented by `KeywordBaselineStrategy`, `PythonTfidfStrategy`, `FallbackStrategy` |
| `KeywordBaselineStrategy` | `KeywordBaselineStrategy.h` | Rule-based matcher computing Dice overlap and categorical metadata | `MatchingConfig config_` (private) | `score() override`, `textScore()`, `categoryScore()`, `colourScore()`, `locationScore()` | Implements `SimilarityStrategy` |
| `PythonTfidfStrategy` | `PythonTfidfStrategy.h` | Composite strategy delegating text to Python and metadata to C++ | `mutable unique_ptr<PythonTextClient> client_`, `KeywordBaselineStrategy structured_`, `CorpusProvider corpus_` | `score() override`, `warmUp()`, `fitCount()`, `lastFit()` | Implements `SimilarityStrategy`, owns `PythonTextClient` |
| `FallbackStrategy` | `PythonTfidfStrategy.h` | Decorator/Proxy switching to fallback if primary throws `StrategyUnavailableError` | `unique_ptr<SimilarityStrategy> primary_`, `fallback_`, `mutable bool failed_`, `mutable string reason_` | `score() override`, `name() override`, `usingFallback()` | Implements `SimilarityStrategy`, owns two `SimilarityStrategy` objects |
| `MatchEngine` | `MatchEngine.h` | Evaluates eligibility, delegates scoring, filters, and ranks | `unique_ptr<SimilarityStrategy> strategy_`, `double threshold_`, `bool categoryBlocking_`, `int dateWindowDays_` | `setStrategy()`, `findMatches()`, `scorePair()`, `isEligible()` | Aggregates `SimilarityStrategy`; owned by `AppContext` |
| `ReportRepository` | `ReportRepository.h` | In-memory aggregate storage owning entities | `vector<unique_ptr<ItemReport>> reports_`, `map<string, size_t> reportIndex_`, `map<string, User> users_` | `addReport()`, `addUser()`, `findById()`, `openFoundReports()`, `updateStatus()`, `nextReportId()` | Owns `ItemReport` and `User` instances; non-copyable |
| `ReportService` | `ReportService.h` | Application use-case facade enforcing domain invariants | `ReportRepository& repository_`, `MatchEngine& engine_` (references) | `submitLost()`, `submitFound()`, `suggestMatches()`, `explainMatch()`, `confirmMatch()`, `validateConfirmation()` | References `ReportRepository` and `MatchEngine` |
| `DataDirLock` | `DataDirLock.h` | RAII advisory process lock using kernel `flock` | `string path_`, `int fd_` (private) | `path()` | Owned by `AppContext`; non-copyable, non-movable |
| `AppContext` | `AppContext.h` | Root container managing configuration, persistence, and lifecycle | `unique_ptr<DataDirLock> lock_`, `MatchingConfig config_`, `ReportRepository repository_`, `MatchEngine engine_`, `ReportService service_` | `load()`, `registerUser()`, `submitReport()`, `confirmMatch()`, `service()`, `engine()` | Composition root for C++ application |
| `ApiHandlers` | `ApiHandlers.h` | Transport-independent HTTP request dispatcher | `AppContext& context_`, `Logger logger_` | `handle(ApiRequest) -> ApiResponse` | Dispatches requests to `AppContext` / `ReportService` |
| `PythonProcess` | `PythonProcess.h` | Low-level RAII wrapper managing Python child process | `pid_t pid_`, `int toChild_`, `fromChild_`, `PythonProcessOptions options_` | `start()`, `writeLine()`, `readLine()`, `stop()`, `logTail()` | Low-level IPC controller; non-copyable |
| `PythonTextClient` | `PythonTextClient.h` | High-level protocol client translating C++ queries to JSON Lines | `PythonProcess process_`, `long requestCounter_`, `bool fitted_` | `start()`, `fit()`, `rank()`, `buildFitRequest()`, `parseRankResponse()` | Owns `PythonProcess` |
| `CsvReader` | `Csv.h` | RFC 4180 compliant CSV stream tokenizer | `string text_`, `size_t pos_`, `line_`, `blankLines_` | `next(CsvRecord&)`, `fromFile(path)` | Used by ingestion and persistence layers |
| `DatasetAdapter` | `DatasetAdapter.h` | Transforms raw heterogeneous CSV rows to canonical records | `DatasetConfig config_`, `vector<string> header_`, `array<optional<size_t>, 9> index_` | `adapt(CsvRecord) -> AdaptOutcome` | Owned by `DatasetLoader` |
| `DatasetLoader` | `DatasetLoader.h` | Bulk ingester validating rows and submitting to `ReportService` | `DatasetConfig config_`, `ReportService& service_`, `map<string, string> userIdByName_` | `load(CsvReader) -> IngestResult` | Orchestrates ingestion pipeline |
| `KeyValueConfig` | `KeyValueConfig.h` | Parser for `.conf` configuration syntax | `map<string, string> entries_`, `string sourceName_` | `parse()`, `loadFile()`, `getOr()`, `getDouble()`, `getBool()`, `getList()` | Base parser for all config files |
| `MatchingConfig` | `MatchingConfig.h` | Holds validated matching hyperparameters and weights | `BaselineWeights weights`, `map<string, string> locationZone`, `colourGroup`, `categoryGroup`, `PythonSettings python` | `fromKeyValues()`, `loadFile()` | Configures `MatchEngine` and strategies |

---

### In-Depth Class Analyses with Code Examples

#### 1. `ItemReport`, `LostReport`, `FoundReport` (Domain Entity Hierarchy)
- **Purpose:** Represents the core lost-and-found entity while guaranteeing data validity upon construction.
- **Access Control & Encapsulation:** All attributes (`id_`, `description_`, `category_`, `date_`, `status_`) are strictly private. Access is provided via `const noexcept` getters. Status mutation is gated by `setStatus()`, which verifies valid lifecycle transitions (`OPEN -> MATCHED` or `OPEN -> CLOSED`).
- **Memory & Slicing Protection:** Copy constructor and copy assignment operator are explicitly deleted (`= delete`). Polymorphic destruction is guaranteed via `virtual ~ItemReport() = default;`.

```cpp
// Illustrative snippet adapted from include/findit/ItemReport.h & LostReport.h
namespace findit {

class ItemReport {
public:
    virtual ~ItemReport() = default;
    ItemReport(const ItemReport&) = delete;
    ItemReport& operator=(const ItemReport&) = delete;

    virtual ReportType type() const noexcept = 0; // Pure virtual
    const std::string& id() const noexcept { return id_; }
    ReportStatus status() const noexcept { return status_; }

    void setStatus(ReportStatus next) {
        if (!canTransition(status_, next)) {
            throw std::logic_error("Invalid status transition");
        }
        status_ = next;
    }

protected:
    ItemReport(std::string id, const ReportInput& input);
    virtual std::string dateLabel() const = 0; // Template method hook

private:
    std::string id_;
    ReportStatus status_ = ReportStatus::Open;
};

class LostReport final : public ItemReport {
public:
    LostReport(std::string id, const ReportInput& input)
        : ItemReport(std::move(id), input) {}

    ReportType type() const noexcept override { return ReportType::Lost; }

protected:
    std::string dateLabel() const override { return "Lost on"; }
};

} // namespace findit
```

#### 2. `SimilarityStrategy` and `FallbackStrategy` (Strategy & Decorator Patterns)
- **Purpose:** Decouples candidate scoring algorithms from `MatchEngine`. Allows swapping between C++ keyword matching and Python TF-IDF without modifying candidate filtering logic.
- **Fallback Decorator:** `FallbackStrategy` intercepts `StrategyUnavailableError`. If the Python child process crashes or fails to fit, it cleanly degrades to `KeywordBaselineStrategy` and triggers a user-facing notification.

```cpp
// Illustrative snippet adapted from include/findit/PythonTfidfStrategy.h
class FallbackStrategy final : public SimilarityStrategy {
public:
    FallbackStrategy(std::unique_ptr<SimilarityStrategy> primary,
                     std::unique_ptr<SimilarityStrategy> fallback,
                     std::function<void(const std::string&)> onFallback = nullptr)
        : primary_(std::move(primary)), fallback_(std::move(fallback)),
          onFallback_(std::move(onFallback)) {}

    std::vector<MatchResult> score(const LostReport& lost,
                                   const std::vector<const FoundReport*>& candidates) const override {
        if (!failed_) {
            try {
                return primary_->score(lost, candidates);
            } catch (const StrategyUnavailableError& e) {
                failed_ = true;
                reason_ = e.what();
                if (onFallback_) onFallback_(reason_);
            }
        }
        return fallback_->score(lost, candidates);
    }

    std::string name() const override {
        return failed_ ? fallback_->name() + " (fallback)" : primary_->name();
    }

private:
    std::unique_ptr<SimilarityStrategy> primary_;
    std::unique_ptr<SimilarityStrategy> fallback_;
    std::function<void(const std::string&)> onFallback_;
    mutable bool failed_ = false;
    mutable std::string reason_;
};
```

#### 3. `DataDirLock` (RAII Process Safety)
- **Purpose:** Protects the CSV storage directory (`data/store/`) from concurrent write corruption between the interactive CLI and HTTP server.
- **Kernel Flock:** Uses `flock(fd, LOCK_EX | LOCK_NB)` on `.findit.lock`. The lock is associated with the open file table in the kernel and is released automatically on process death or crash.

```cpp
// Illustrative snippet adapted from src/DataDirLock.cpp
DataDirLock::DataDirLock(const std::string& storeDir, const std::string& programName) {
    path_ = storeDir + "/" + kFileName;
    fd_ = ::open(path_.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd_ < 0) throw std::runtime_error("Cannot open lock file: " + path_);

    if (::flock(fd_, LOCK_EX | LOCK_NB) != 0) {
        std::string holder = readLockInfo(fd_);
        ::close(fd_);
        fd_ = -1;
        throw DataDirLockedError("The data directory is in use by another FindIt process (" + holder + ")");
    }
    writeLockInfo(fd_, programName);
}

DataDirLock::~DataDirLock() {
    if (fd_ >= 0) {
        ::ftruncate(fd_, 0);
        ::flock(fd_, LOCK_UN);
        ::close(fd_);
    }
}
```

---

## 7. OOP Concepts Used in the Project

The table below demonstrates where and how object-oriented principles are applied in FindIt AI:

| Concept | Status in Project | Concrete Code Evidence (File & Symbol) | Specific Benefit to FindIt AI |
|---|:---:|---|---|
| **Classes & Objects** | **USED** | `Date.h` (`Date`), `User.h` (`User`), `ItemReport.h` (`ItemReport`) | Encapsulates domain identity and validation inside cohesive abstractions. |
| **Encapsulation** | **USED** | `ItemReport.h` (`status_`, `id_`), `MatchResult.h` (`score_`, `rank_`) | Prevents external code from tampering with report status or scores; enforces `canTransition()`. |
| **Abstraction** | **USED** | `SimilarityStrategy.h`, `ItemReport.h` | Consumers like `MatchEngine` depend on abstract interfaces, remaining agnostic to Python vs. C++ text engines. |
| **Inheritance** | **USED** | `LostReport.h` & `FoundReport.h` : `ItemReport`; `PythonMatcherError` : `StrategyUnavailableError` | Avoids duplicate schema code across lost/found reports while specializing `FoundReport::heldAt_`. |
| **Runtime Polymorphism** | **USED** | `ItemReport::summary()`, `ItemReport::details()`, `SimilarityStrategy::score()` | Dynamic dispatch invokes correct `details()` formatting and strategy calculation at runtime. |
| **Compile-Time Polymorphism** | **USED** | `validation::requireText`, `KeyValueConfig::getOr` | Function overloading on parameter types allows ergonomic configuration lookups. |
| **Pure Virtual Functions** | **USED** | `ItemReport::type() = 0`, `ItemReport::dateLabel() = 0`, `SimilarityStrategy::score() = 0` | Guarantees that abstract base classes cannot be instantiated and enforces complete concrete interfaces. |
| **Constructors & Destructors**| **USED** | `User::User()`, `ItemReport::~ItemReport()`, `DataDirLock::~DataDirLock()` | Guarantees class invariants upon creation; destructors release files, child processes, and kernel locks. |
| **Copy Semantics Deletion** | **USED** | `ItemReport(const ItemReport&) = delete;`, `ReportRepository`, `DataDirLock` | Prevents slicing of polymorphic entities and avoids double-free bugs on unique system resources. |
| **Access Specifiers** | **USED** | `ItemReport.h` (`protected: ItemReport(...)`, `private: id_`) | Restricts base constructor access to derived classes while completely shielding raw member state. |
| **`this` Pointer** | **USED** | `MatchEngine.cpp`, `ReportRepository.cpp`, `KeyValueConfig.cpp` | Disambiguates scope during member assignments and passes self-references to collaborating objects. |
| **Composition** | **USED** | `AppContext` owns `ReportRepository`, `MatchEngine`, `DataDirLock` | Strict parent-child lifetime: when `AppContext` terminates, internal stores and locks destruct cleanly. |
| **Aggregation** | **USED** | `ReportService` holds `ReportRepository&`, `MatchEngine&` | Service coordinates components without owning them; repositories outlive individual use-case invocations. |
| **Association / Dependency** | **USED** | `MatchEngine::findMatches` consumes `LostReport` & `FoundReport` | Operates on domain models by const reference without managing their storage or lifetime. |
| **RAII** | **USED** | `DataDirLock` (`flock`), `PythonProcess` (`posix_spawnp`, pipes, `kill`) | Guarantees file descriptors, locks, and child processes never leak even if exceptions are thrown. |
| **Smart Pointers** | **USED** | `std::unique_ptr<ItemReport>`, `std::unique_ptr<SimilarityStrategy>` | Expresses exclusive ownership semantics across heterogeneous polymorphic object graphs. |
| **Raw Non-Owning Pointers** | **USED** | `const ItemReport*`, `const FoundReport*` in `ReportRepository` queries | Allows efficient lookups and candidate lists without transferring ownership or duplicating heap memory. |
| **`const` Correctness** | **USED** | `Date::year() const noexcept`, `SimilarityStrategy::score() const` | Compiler-enforced immutability; guarantees getters and matching algorithms cannot mutate state. |
| **Pass-by-Reference** | **USED** | `requireText(const std::string&, ...)` | Eliminates redundant string allocations across high-throughput data loops. |
| **Static Member Functions** | **USED** | `Date::parse()`, `KeyValueConfig::loadFile()`, `validation::isValidReportId()` | Factory methods and stateless validation routines co-located with their relevant domain classes. |
| **Operator Overloading** | **USED** | `Date::operator==`, `Date::operator<` | Enables natural relational expressions, direct date sorting, and range queries. |
| **Templates** | **PARTIALLY USED**| Uses STL templates (`std::vector`, `std::map`, `std::optional`, `std::unique_ptr`); no custom templates | Keeps compilation rapid and debugging straightforward; custom templating was not needed for domain logic. |
| **Exception Handling** | **USED** | `DataDirLockedError`, `PythonMatcherError`, `StrategyUnavailableError`, `ApiError` | Typed exceptions separate domain rule violations (400) from system failures (503/500). |
| **SOLID Principles** | **USED** | Single Responsibility (Storage vs. Scoring), Open/Closed (New Strategies), Liskov Substitution | Core architectural backbone across all service and repository interfaces. |

---

## 8. Core Workflows and Data Flow

### 1. Application Startup & Configuration
1. CLI (`main.cpp`) or Server (`findit_server.cpp`) parses command-line flags into `AppOptions`.
2. `AppContext::loadMatchingConfig()` reads `config/matching.conf` via `KeyValueConfig`.
3. If `--text-engine python` is selected (or defaulted in config), `AppContext` constructs a `PythonTfidfStrategy` wrapped in a `FallbackStrategy`.
4. `AppContext::load()` checks if running in `--sample` mode:
   - If persistent, acquires `DataDirLock` on `<dataDir>/store/.findit.lock`.
   - Ingests canonical users and reports from `data/processed/` and `data/store/`.
   - Replays confirmed matches from `data/store/matches.csv` through `ReportService::confirmMatch()`.

### 2. Creating a Report (Lost or Found)
1. User provides raw attributes through CLI prompts or web form (`POST /api/reports`).
2. Input is bundled into a `ReportInput` struct.
3. `ReportService::submitLost()` or `submitFound()`:
   - Checks that `reporterId` exists in `ReportRepository`.
   - Validates description length ($10 \le \text{length} \le 500$), category, colour, location, and date format.
   - Computes the next monotonic ID (e.g. `R030001`).
4. In persistent mode, `AppContext` appends the record to `data/store/reports.csv` first.
5. Upon successful disk write, the newly instantiated `LostReport` or `FoundReport` is added to `ReportRepository`.

### 3. Match Generation & Candidate Ranking
1. User requests matches for an open lost report ID (`R030001`).
2. `ReportService::suggestMatches()` fetches the lost report and all `openFoundReports()`.
3. `MatchEngine::findMatches()` executes candidate pre-filtering:
   - Drops candidates that are not `ReportStatus::Open`.
   - Drops candidate if ID equals the query report ID.
   - **Category Blocking:** If enabled (default), drops candidates whose category does not match.
   - **Date Window:** If enabled, drops candidates outside the specified day window.
4. Active strategy computes scores for surviving candidates.
5. Scores are filtered by `threshold`, sorted descending by score, tie-broken by ID ascending, and truncated to `topK` (default 3).

### 4. Human Match Confirmation
1. Officer reviews candidate details and explanations, then confirms the match.
2. `ReportService::confirmMatch()` executes verification:
   - Verifies both report IDs exist.
   - Verifies one is `ReportType::Lost` and the other is `ReportType::Found`.
   - Verifies both reports are currently in `ReportStatus::Open`.
3. `AppContext` appends an audit row to `data/store/matches.csv` (`lost_id,found_id,confirmed_on`).
4. `ReportRepository::updateStatus()` transitions both items to `ReportStatus::Matched`.

---

## 9. Matching Algorithms and AI/ML

### Hybrid Scoring Formula
FindIt AI uses a weighted composite scoring model to rank found candidates against a lost query:

$$\text{Final Score} = \frac{w_{\text{text}} \cdot S_{\text{text}} + w_{\text{loc}} \cdot S_{\text{loc}} + w_{\text{cat}} \cdot S_{\text{cat}} + w_{\text{col}} \cdot S_{\text{col}}}{w_{\text{text}} + w_{\text{loc}} + w_{\text{cat}} + w_{\text{col}}}$$

Default weights configured in `config/matching.conf`:
- $w_{\text{text}} = 0.60$ (Description text similarity)
- $w_{\text{loc}} = 0.20$ (Location / campus zone proximity)
- $w_{\text{cat}} = 0.10$ (Item category agreement)
- $w_{\text{col}} = 0.10$ (Colour agreement)

With default normalized weights summing to $1.0$, the score simplifies to:

$$\text{Final Score} = 0.60 S_{\text{text}} + 0.20 S_{\text{loc}} + 0.10 S_{\text{cat}} + 0.10 S_{\text{col}} \in [0.0, 1.0]$$

---

### Component Scoring Mechanics

#### 1. Text Similarity ($S_{\text{text}}$)
- **Primary Engine (`PythonTfidfStrategy`):**
  - Uses `scikit-learn`'s `TfidfVectorizer` on the `description` field only.
  - Computes two complementary sparse vector representations:
    1. **Word Unigrams:** Captures exact rare terms (e.g. *"Milton"*, *"dent"*, *"hydroflask"*).
    2. **Character N-grams ($3 \le n \le 5$ within word boundaries):** Captures inflectional variants (*"scratch"*, *"scratched"*, *"scratches"*) and minor typographical errors.
  - Vector similarity is evaluated via Cosine Similarity:
    $$S_{\text{text}} = 0.60 \cdot \cos(\mathbf{v}_{\text{word}}^{\text{lost}}, \mathbf{v}_{\text{word}}^{\text{found}}) + 0.40 \cdot \cos(\mathbf{v}_{\text{char}}^{\text{lost}}, \mathbf{v}_{\text{char}}^{\text{found}})$$
  - Explanations list up to 8 exact shared informative terms, sorted rarest-first by IDF weight.

- **Baseline Fallback (`KeywordBaselineStrategy`):**
  - Pure C++ implementation with zero Python dependency.
  - Normalizes text, splits into alphanumeric tokens, fixes zero-digit typo patterns (`"l0st"` $\to$ `"lost"`), filters common English stopwords, and calculates the **Dice Similarity Coefficient**:
    $$S_{\text{text}} = \frac{2 \cdot |A \cap B|}{|A| + |B|}$$
    where $A$ and $B$ are unique keyword sets extracted from descriptions.

#### 2. Structured Metadata Scoring ($S_{\text{cat}}, S_{\text{col}}, S_{\text{loc}}$)
All structured scoring occurs in C++:
- **Category:** $1.0$ if exact match; $0.5$ if different categories belong to the same configured group (e.g., *"Phone"* and *"Electronics"* in `gadgets`); $0.0$ otherwise.
- **Colour:** $1.0$ if exact match; $0.5$ if colours share a configured family (e.g., *"navy"* and *"blue"* in `blue_tones`); $0.0$ otherwise.
- **Location:** $1.0$ if exact match; $0.5$ if locations reside in the same campus zone (e.g., *"Gym"* and *"Sports Complex"* in zone `SPORTS`); $0.0$ otherwise.
- **Missing Value Handling:** Missing or unknown attributes score $0.0$ and are explicitly explained as missing; they are **never** treated as agreement.

---

### Score vs. Probability Distinction
> [!IMPORTANT]
> **Scores are relative ranking values, not probabilities.**
> A candidate score of $0.78$ does not imply a $78\%$ probability that the two records represent the identical physical item. Hard negatives (e.g., two distinct black leather wallets lost in the same library) may achieve scores above $0.80$. The score serves solely to sort candidates so that the most plausible matches appear at the top of the desk officer's review queue.

---

## 10. Data Model and Persistence

### Storage Directory Layout
```
data/
├── raw/
│   └── lost_found_dataset_realistic_metadata_30k.csv  # Read-only source
├── processed/                                         # Generated by findit_ingest
│   ├── users.csv
│   ├── reports.csv
│   ├── rejected_rows.csv
│   └── summary.json
└── store/                                             # Created by runtime app
    ├── users.csv                                      # Registered users
    ├── reports.csv                                    # Reports logged via CLI/API
    ├── matches.csv                                    # Confirmed match append-log
    ├── python_matcher.log                             # Python worker diagnostics
    └── .findit.lock                                   # 0-byte kernel flock file
```

### Canonical CSV Schemas

#### 1. `reports.csv`
```csv
report_id,report_type,status,category,description,color,brand,location,date,reporter_id,held_at,source_ref
R000001,lost,open,Wallet,Black leather wallet with student ID,black,,Library,2026-03-20,U001,,1
R030002,found,open,Water Bottle,Blue Milton steel bottle with cat sticker,blue,Milton,Gym,2026-03-21,U002,Desk B,
```
- `report_id`: Unique identifier formatted as `R` followed by 6 digits (`R000001` - `R999999`).
- `report_type`: `lost` | `found`.
- `status`: `open` | `matched` | `closed`.
- `held_at`: Specified only for found reports (location where the item is physically stored).
- `source_ref`: Populated with source dataset row ID for imported items; blank for app-created reports.

#### 2. `users.csv`
```csv
user_id,name,contact
U001,Suma Patil,suma.patil@example.edu
U002,Desk Attendant,desk@campus.edu
```
- `user_id`: Formatted as `U` followed by digits (`U001` - `U999`).
- `contact`: Optional phone number or campus email.

#### 3. `matches.csv` (Confirmation Audit Log)
```csv
lost_id,found_id,confirmed_on
R030001,R030002,2026-03-22
```
- Confirmed matches are recorded as an append-only log. The baseline `reports.csv` file is never destructively edited to update status. On startup, `AppContext::load()` replays each confirmation through `ReportService::confirmMatch()`. If a row in `matches.csv` references a missing ID or invalid state, startup halts with a clear error and line number.

---

## 11. HTTP API Reference

The local server executable (`./build/findit_server`) exposes a JSON REST API on `http://127.0.0.1:8080`.

### Security Constraints
- **Loopback Only:** Bound exclusively to `127.0.0.1`.
- **Host Header Validation:** Rejects requests with HTTP 403 `forbidden_host` if the `Host` header is not `127.0.0.1:<port>` or `localhost:<port>`, blocking DNS-rebinding attacks.
- **Payload Limits:** Maximum request body size is capped at 16 KB (HTTP 413 `payload_too_large`).
- **Response Headers:** Injects `Cache-Control: no-store`, `X-Content-Type-Options: nosniff`, and `Referrer-Policy: no-referrer`.

---

### Endpoints Specification

#### `GET /api/health`
Checks server readiness, storage mode, and active text engine.
- **Response `200 OK`:**
  ```json
  {
    "status": "ok",
    "dataMode": "dataset",
    "persistent": true,
    "sampleData": false,
    "matcher": {
      "strategy": "python-tfidf-v1",
      "textEngine": "python",
      "fallbackActive": false,
      "categoryBlocking": true
    }
  }
  ```

#### `GET /api/stats`
Returns system-wide inventory and match counters.
- **Response `200 OK`:**
  ```json
  {
    "totalReports": 30002,
    "lostReports": 15001,
    "foundReports": 15001,
    "openLostReports": 15000,
    "openFoundReports": 15000,
    "confirmedMatches": 1,
    "reportsCreatedInApp": 2
  }
  ```

#### `GET /api/reports`
Searches and paginates reports.
- **Query Parameters:**
  - `q`: Search keywords ($\le 100$ chars, $\le 10$ words; all must appear).
  - `type`: `lost` | `found`.
  - `status`: `open` | `matched` | `closed`.
  - `category`: Exact category name (case-insensitive).
  - `origin`: `dataset` | `app`.
  - `sort`: `recent` (default, date desc) | `id` (ID asc).
  - `limit`: Integer $1 \dots 100$ (default 20).
  - `offset`: Integer $\ge 0$ (default 0).
- **Response `200 OK`:**
  ```json
  {
    "items": [
      {
        "id": "R030001",
        "type": "lost",
        "status": "open",
        "category": "Wallet",
        "description": "Black leather wallet with student ID card",
        "color": "black",
        "brand": null,
        "location": "Library",
        "date": "2026-03-20",
        "reporterId": "U001",
        "heldAt": null,
        "origin": "app",
        "sourceRef": null
      }
    ],
    "total": 1,
    "limit": 20,
    "offset": 0
  }
  ```

#### `POST /api/reports`
Creates and persists a new report.
- **Request Body:**
  ```json
  {
    "type": "lost",
    "reporterId": "U001",
    "category": "Wallet",
    "description": "Black leather wallet with student ID card",
    "color": "black",
    "location": "Library",
    "date": "2026-03-20",
    "brand": "Bellroy"
  }
  ```
- **Response `201 Created`:** Report JSON + `"persisted": true`.
- **Errors:** `400 validation_failed` if description $< 10$ characters or reporter unknown.

#### `GET /api/reports/:id/matches?k=3`
Retrieves top candidate matches for an open lost report.
- **Response `200 OK`:**
  ```json
  {
    "lost": { "id": "R030001", "type": "lost", "status": "open", "description": "..." },
    "matcher": { "strategy": "python-tfidf-v1", "fallbackActive": false, "categoryBlocking": true },
    "scoreNote": "Scores are relative ranking values between 0 and 1 for comparing candidates; they are not probabilities.",
    "matches": [
      {
        "rank": 1,
        "score": 0.82,
        "components": { "text": 0.70, "location": 1.00, "category": 1.00, "colour": 1.00 },
        "explanations": [
          "Text (TF-IDF): Exact shared terms: wallet, leather; general similarity 0.70",
          "Same category: Wallet",
          "Same colour: black",
          "Exact location: Library"
        ],
        "found": { "id": "R000452", "type": "found", "status": "open", "description": "..." }
      }
    ],
    "emptyReason": null
  }
  ```
- **Errors:** `409 not_open` if lost report is already `matched`.

#### `POST /api/matches`
Confirms a physical lost-and-found match.
- **Request Body:** `{"lostId": "R030001", "foundId": "R000452"}`
- **Response `200 OK`:**
  ```json
  {
    "lost": { "id": "R030001", "status": "matched" },
    "found": { "id": "R000452", "status": "matched" },
    "confirmedOn": "2026-03-22",
    "persisted": true
  }
  ```
- **Errors:** `409 conflict` if either report is already `matched`.

---

## 12. Frontend Documentation

### Architecture & Routing
The frontend is built with React 18, TypeScript, and Tailwind CSS. The app runs as a single-page application (SPA) routed via `react-router-dom`:
- `/` &rarr; [`DashboardPage`](file:///Users/drashyamehta/Downloads/oops_project/frontend/src/pages/DashboardPage.tsx): High-level inventory statistics, recent submissions, and quick navigation.
- `/report` &rarr; [`ReportPage`](file:///Users/drashyamehta/Downloads/oops_project/frontend/src/pages/ReportPage.tsx): Dynamic form for reporting lost/found items with client validation and inline user registration.
- `/browse` &rarr; [`BrowsePage`](file:///Users/drashyamehta/Downloads/oops_project/frontend/src/pages/BrowsePage.tsx): Inventory browser with multi-keyword search, category/type/status filtering, and server-side pagination.
- `/matches` &rarr; [`MatchesPage`](file:///Users/drashyamehta/Downloads/oops_project/frontend/src/pages/MatchesPage.tsx): Candidate inspector displaying ranked cards, component score progress bars, detailed textual reasons, and the confirmation dialog modal.

### API Service Layer (`src/services/api.ts`)
- Never calls raw `fetch()` directly from components.
- Centralizes all HTTP communication inside an exported `api` object.
- Wraps errors into an `ApiError` class with properties `status`, `code`, and helper `isUnavailable`.
- Dispatches requests through Vite's local development proxy (`/api` $\to$ `http://127.0.0.1:8080`), eliminating any requirement for permissive CORS headers in development.

---

## 13. Build and Installation Instructions

Tested on **macOS (Apple Silicon / Intel)** with Xcode Command Line Tools.

### Prerequisites
1. **Xcode Command Line Tools:**
   ```sh
   xcode-select --install
   ```
2. **Python 3 (3.10+):**
   ```sh
   python3 --version
   ```
3. **Node.js (≥ 20.19.0) and npm:**
   ```sh
   node --version
   npm --version
   ```

### Step-by-Step Setup

```sh
# 1. Clone or navigate to the project directory
cd oops_project

# 2. Set up Python virtual environment and install ML dependencies
python3 -m venv .venv
.venv/bin/pip install --upgrade pip
.venv/bin/pip install -r ml/requirements.txt
.venv/bin/pip install cmake                       # Optional: installs CMake if not present on system
export PATH="$PWD/.venv/bin:$PATH"

# 3. Configure and build C++ backend binaries
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j

# 4. Ingest raw dataset into canonical processed storage
./build/findit_ingest

# 5. Install frontend dependencies
cd frontend
npm install
cd ..
```

---

## 14. Running the Application

### Option A: Web Application (Browser UI)

Starting the full application requires two separate terminals:

**Terminal 1 — Start the Backend Server:**
```sh
cd oops_project
./build/findit_server
```
*Output: `findit_server listening on 127.0.0.1:8080 (dataset mode, persistent)`*

**Terminal 2 — Start the Frontend Development Server:**
```sh
cd oops_project/frontend
npm run dev
```
*Output: `VITE v6.4.4 ready in 250 ms -> Local: http://127.0.0.1:5173/`*

Open **[http://127.0.0.1:5173](http://127.0.0.1:5173)** in your browser.

---

### Option B: Interactive Terminal CLI

> [!WARNING]
> **Data Directory Lock Conflict:**
> Ensure `findit_server` is stopped before running the interactive CLI against the same data directory. Both tools take an exclusive kernel lock on `data/store/.findit.lock`.

```sh
cd oops_project
./build/findit
```
For non-persistent demonstration with zero disk writes:
```sh
./build/findit --sample
```

---

### Safe Shutdown Procedure
- **Frontend Server:** Press `Ctrl+C` in Terminal 2.
- **Backend Server:** Press `Ctrl+C` in Terminal 1. The server traps `SIGINT`/`SIGTERM`, completes any in-flight write, flushes `data/store/matches.csv`, and releases the kernel lock on `.findit.lock`.

---

## 15. Testing and Verification

FindIt AI includes automated test coverage spanning C++, Python, and React components.

### 1. Frontend Test Suite (Vitest & RTL)
```sh
cd frontend && npm test
```
**Results (35 tests passing):**
- `api.test.ts` (10 tests): Query parameter encoding, JSON payload structures, structured 400/409/503 errors, network failure translation.
- `App.test.tsx` (3 tests): Navigation rendering, synthetic banner display, offline alert states.
- `BrowsePage.test.tsx` (6 tests): Search execution, multi-facet filtering, server pagination, empty states.
- `MatchesPage.test.tsx` (9 tests): Top-3 candidate ranking, relative score disclaimer, confirmation dialog, 409 conflict handling.
- `ReportPage.test.tsx` (7 tests): Form validation, inline user creation, double-submit protection, field error mapping.

### 2. Frontend Production Build Check
```sh
cd frontend && npm run build
```
Executes `tsc --noEmit` and `vite build` to guarantee type safety and bundling integrity.

### 3. Python ML & Protocol Tests (pytest)
```sh
.venv/bin/python -m pytest ml/tests/
```
**Results (73 tests passing):**
- `test_preprocess.py` (23 tests): Tokenization, stopword removal, document concatenation.
- `test_matcher.py` (21 tests): Word TF-IDF, character n-gram TF-IDF, cosine similarity, rare-term explanations.
- `test_metrics.py` (15 tests): Mean Reciprocal Rank (MRR), Precision@K, Recall@K.
- `test_service.py` (6 tests): Stdin/stdout JSON Lines protocol, error reporting, shutdown handling.
- `test_data.py` (4 tests) & `test_review_labels.py` (4 tests): Dataset schemas and evaluation label integrity.

### 4. C++ Test Suite (CTest)
```sh
.venv/bin/ctest --test-dir build -R "findit_tests|findit_cli_tests|findit_integration_tests" --output-on-failure
```
**Results (3 targets passing):**
- `findit_tests`: Domain model validation, date arithmetic, repository insertion, keyword Dice baseline.
- `findit_cli_tests`: CLI script execution and confirm options.
- `findit_integration_tests`: C++-to-Python anonymous pipe IPC, `fit`, `rank`, and fallback recovery.

---

## 16. Configuration Reference

### 1. `config/matching.conf`

| Setting Key | Default Value | Allowed Values | Description |
|---|---|---|---|
| `weights.text` | `0.60` | $\ge 0.0$ | Relative weight of description similarity |
| `weights.location` | `0.20` | $\ge 0.0$ | Relative weight of location/zone match |
| `weights.category` | `0.10` | $\ge 0.0$ | Relative weight of category match |
| `weights.colour` | `0.10` | $\ge 0.0$ | Relative weight of colour match |
| `category_blocking`| `true` | `true`, `false` | When true, only candidates in the exact same category are evaluated |
| `threshold` | `0.0` | $0.0 \dots 1.0$ | Minimum composite score required to appear in candidates |
| `top_k` | `3` | Integer $\ge 1$ | Maximum number of candidates returned per lost report |
| `date_window_days` | `0` | Integer $\ge 0$ | Max days between lost & found dates ($0 = \text{disabled}$) |
| `text_engine` | `python` | `python`, `keyword` | Default text similarity engine |
| `python.executable`| `.venv/bin/python` | File path | Python interpreter executable |
| `python.module_path`| `ml` | Directory path | Added to child process `PYTHONPATH` |
| `python.timeout_seconds` | `120` | Integer $\ge 1$ | Max wait time for child IPC response |
| `python.fallback_to_keyword` | `true` | `true`, `false` | Auto-switch to C++ keyword matcher if Python crashes |
| `zone.<name>` | Lists | Comma-separated locations | Defines campus zones (e.g. `zone.SPORTS = Gym, Pool`) |
| `colour_group.<name>` | Lists | Comma-separated colours | Defines colour groups (e.g. `colour_group.blue = navy, sky`) |

### 2. `config/dataset_mapping.conf`
Configures column name translations from the raw CSV without modifying C++ source code:
- `column.report_type = Type`
- `column.category = Category`
- `column.description = Description`
- `column.location = Location`
- `column.timestamp = Date`
- `values.lost = Lost`
- `values.found = Found`
- `missing_tokens = None, unknown, N/A, -`

---

## 17. Error Handling, Security, and Limitations

### Security Architecture
- **No Remote Exposure:** Server binds exclusively to `127.0.0.1`.
- **DNS Rebinding Protection:** Validates incoming `Host` headers.
- **Buffer & Payload Safety:** 16 KB request body limit prevents memory exhaustion.
- **Injection Safety:** The Python worker is spawned directly via `posix_spawnp` with an explicit argument vector. No shell (`/bin/sh`) is invoked.
- **Fail-Safe Persistence:** Atomic temporary-file renames (`.tmp` $\to$ final) ensure that crashes during disk operations never corrupt repository files.

### Known Limitations
1. **Uncalibrated Ranking Scores:** Scores are relative metrics for sorting, not calibrated probabilities.
2. **Fixed TF-IDF Vocabulary:** Python fits its TF-IDF model on startup across existing descriptions. Novel terms introduced in newly reported items receive character n-gram credit, but full word IDF weights are only updated on restart.
3. **Category Blocking Trade-off:** Enabling category blocking reduces candidate search from 15,000 items to $\approx 480$, providing major latency improvements. However, items misclassified at intake (e.g., a tablet logged under *"Electronics"* vs. *"Computers"*) will not be compared.
4. **Single-User Local Concurrency:** The C++ `AppContext` is not thread-safe. `findit_server` serializes requests through an internal mutex.

---

## 18. Troubleshooting

### 1. "The data directory is in use by another FindIt process"
- **Cause:** Another instance of `findit_server` or the `findit` CLI holds the kernel lock on `data/store/.findit.lock`.
- **Fix:** Locate and terminate the process holding the lock:
  ```sh
  lsof data/store/.findit.lock
  kill <PID>
  ```
  *(Never delete `.findit.lock`; the file is an empty inode placeholder and deleting it breaks protection).*

### 2. "API not reachable" in Frontend UI
- **Cause:** The backend server is not running on port 8080.
- **Fix:**
  1. Verify the server is active:
     ```sh
     curl -s http://127.0.0.1:8080/api/health
     ```
  2. If running on an alternative port (e.g. 8081), launch the frontend with:
     ```sh
     FINDIT_API_TARGET=http://127.0.0.1:8081 npm run dev
     ```

### 3. "Python matcher unavailable — using keyword fallback"
- **Cause:** Virtual environment missing dependencies or Python binary path incorrect.
- **Fix:**
  1. Re-install Python requirements:
     ```sh
     .venv/bin/pip install -r ml/requirements.txt
     ```
  2. Inspect Python crash details:
     ```sh
     cat data/store/python_matcher.log
     ```

---

## 19. Development Workflow and Contribution Guide

### Branching & Checkpoint Safety
- All new features and documentation updates must be developed on a dedicated feature branch (e.g., `feat/complete-frontend`).
- The canonical root commit checkpoint is preserved on `main`.

### Quality Verification Checklist Before Committing
1. **Frontend Tests & Build:**
   ```sh
   cd frontend && npm test && npm run build && cd ..
   ```
2. **Python Suite:**
   ```sh
   .venv/bin/python -m pytest ml/tests/
   ```
3. **C++ Unit Tests:**
   ```sh
   .venv/bin/ctest --test-dir build -R "findit_tests|findit_cli_tests|findit_integration_tests" --output-on-failure
   ```
4. **Dataset Integrity:** Ensure `data/raw/lost_found_dataset_realistic_metadata_30k.csv` and `evaluation/synthetic_v1/` remain unmodified (`git status`).

---

## 20. Future Enhancements

- [ ] **Dual-Encoder Dense Embeddings:** Augment TF-IDF with lightweight on-device sentence transformers (e.g., MiniLM via ONNX Runtime in C++) for semantic understanding (e.g., mapping *"shades"* $\leftrightarrow$ *"sunglasses"*).
- [ ] **Image Upload & Embedding Matching:** Enable photo attachments with visual similarity matching using mobile Vision Transformers.
- [ ] **Multi-Tenant Campus RBAC:** Implement JWT authentication with separated roles (Student, Desk Officer, SuperAdmin).
- [ ] **Automated Email Notifications:** Notify students automatically when a found item matches their lost report with a score exceeding $0.85$.

---

## 21. Project Summary for Academic Evaluation and Interviews

### Executive Technical Summary
FindIt AI was engineered as an individual undergraduate demonstration of robust, production-grade Object-Oriented Programming (OOP) in C++17, hybridized with modern Python Information Retrieval techniques and a responsive React frontend.

### Key Architectural Decisions
1. **Why C++ for the Core?**
   C++ provides deterministic memory management, zero-cost abstractions, strict type safety, and complete control over system resources. Enforcing lifecycle state transitions (`OPEN -> MATCHED -> CLOSED`) and atomic CSV logging in C++ guarantees that data invariants cannot be violated by scripting errors.
2. **Why Python for Text Matching?**
   Natural language preprocessing and sparse vector math benefit from Python's mature `scikit-learn` ecosystem. Isolating Python in an external child worker keeps the C++ domain codebase clean and allows graceful fallback to C++ keyword matching if the ML environment fails.
3. **Why Loopback HTTP + Reverse Proxy?**
   Running a single-origin architecture via Vite's proxy eliminates complex Cross-Origin Resource Sharing (CORS) configurations, minimizes browser attack surfaces, and protects the system from DNS-rebinding attacks via Host header validation.

### Demonstrable OOP Highlights for Technical Viva
- **Polymorphism & Abstraction:** `ItemReport` abstract base class with pure virtual methods (`type()`, `dateLabel()`), dynamic dispatch in `details()`, and deleted copy semantics preventing slicing.
- **Strategy & Decorator Patterns:** `SimilarityStrategy` interface allowing zero-overhead swapping between `KeywordBaselineStrategy` and `PythonTfidfStrategy`, wrapped transparently by `FallbackStrategy`.
- **Facade Pattern:** `ReportService` provides a unified, validated API for high-level use cases while isolating `ReportRepository` and `MatchEngine`.
- **Robust RAII:** `DataDirLock` guarantees kernel-level file lock release on termination, and `PythonProcess` guarantees pipe and child process cleanup.
