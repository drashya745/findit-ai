# Architecture

> **Previewing the diagrams.** GitHub renders these Mermaid blocks natively. In VS Code, install
> the "Markdown Preview Mermaid Support" extension (`bierner.markdown-mermaid`) and open the
> Markdown preview (Cmd+Shift+V). To render offline to SVG with the official CLI (checked with
> 11.4.2 against the installed Google Chrome; nothing is installed globally):
> ```sh
> mkdir -p /tmp/mmd && cd /tmp/mmd && PUPPETEER_SKIP_DOWNLOAD=1 npm install @mermaid-js/mermaid-cli@11.4.2
> echo '{"executablePath":"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"}' > p.json
> ./node_modules/.bin/mmdc -p p.json -i <project>/docs/ARCHITECTURE.md -o arch.md   # writes arch-1.svg … arch-5.svg
> ```

## System architecture

```mermaid
flowchart LR
    subgraph Inputs
        RAW[("data/raw/…30k.csv<br/>read-only")]
        CFG1["config/dataset_mapping.conf"]
        CFG2["config/matching.conf"]
    end

    subgraph CPP["C++17 application"]
        ING["findit_ingest<br/>CsvReader → DatasetAdapter → DatasetLoader"]
        CLI["findit CLI<br/>(main.cpp)"]
        SVC["ReportService"]
        REPO["ReportRepository"]
        ENG["MatchEngine"]
        STRAT["SimilarityStrategy<br/>Fallback → PythonTfidf | KeywordBaseline"]
        EVAL["findit_eval"]
    end

    subgraph PY["Python 3 (ml/findit_ml)"]
        SERV["service.py<br/>JSON Lines"]
        TM["TextMatcher<br/>TF-IDF + cosine"]
        METRICS["evaluation/run_eval.py"]
    end

    PROC[("data/processed/<br/>users, reports,<br/>rejected_rows, summary")]
    STORE[("data/store/<br/>CLI-created users/reports,<br/>matches.csv (confirmations)")]
    RES[("evaluation/results/")]

    RAW --> ING
    CFG1 --> ING
    ING --> PROC
    PROC --> CLI
    STORE <--> CLI
    CFG2 --> CLI
    CLI --> SVC
    SVC --> REPO
    SVC --> ENG
    ENG --> STRAT
    STRAT -- "stdin: fit / rank (JSON, descriptions only)" --> SERV
    SERV -- "stdout: results (JSON)" --> STRAT
    SERV --> TM
    EVAL --> ENG
    EVAL --> RES
    RES --> METRICS
```

## Class diagram (core domain and matching)

```mermaid
classDiagram
    class User {
        -string id_
        -string name_
        -string contact_
        +id() string
        +name() string
        +toString() string
    }
    class ItemReport {
        <<abstract>>
        -string id_, reporterId_, category_, description_, color_, location_
        -optional~string~ brand_
        -Date date_
        -ReportStatus status_
        -string sourceRef_
        #ItemReport(id, ReportInput)
        +type()* ReportType
        #dateLabel()* string
        +summary() string
        +details() string
        +setStatus(ReportStatus)
    }
    class LostReport {
        +type() ReportType
        +lostDate() Date
    }
    class FoundReport {
        -string heldAt_
        +type() ReportType
        +details() string
        +heldAt() string
    }
    class MatchResult {
        -string lostId_, foundId_
        -double score_
        -int rank_
        -map~string,double~ components_
        -vector~string~ explanations_
        +addComponent(name, value)
        +addExplanation(text)
        +toString() string
    }
    class SimilarityStrategy {
        <<interface>>
        +score(LostReport, vector~FoundReport*~)* vector~MatchResult~
        +name()* string
    }
    class KeywordBaselineStrategy {
        -MatchingConfig config_
        +textScore(set, set)$ Component
        +categoryScore() Component
        +colourScore() Component
        +locationScore() Component
    }
    class PythonTfidfStrategy {
        -unique_ptr~PythonTextClient~ client_
        -KeywordBaselineStrategy structured_
        -CorpusProvider corpus_
        +warmUp()
    }
    class FallbackStrategy {
        -unique_ptr~SimilarityStrategy~ primary_
        -unique_ptr~SimilarityStrategy~ fallback_
    }
    class PythonTextClient {
        -PythonProcess process_
        +start()
        +fit(corpus) FitSummary
        +rank(lost, candidates) TextRanking
        +parseRankResponse(...)$ TextRanking
    }
    class PythonProcess {
        -pid_t pid_
        -int toChild_, fromChild_
        +start()
        +writeLine(string)
        +readLine() string
        +stop()
    }
    class MatchEngine {
        -unique_ptr~SimilarityStrategy~ strategy_
        -double threshold_
        -bool categoryBlocking_
        +findMatches(lost, candidates, k) vector~MatchResult~
        +scorePair(lost, found) MatchResult
        +isEligible(lost, found) bool
    }
    class ReportRepository {
        -vector~unique_ptr~ItemReport~~ reports_
        -map~string,User~ users_
        +addReport(unique_ptr~ItemReport~)
        +findById(id) const ItemReport* or nullptr
        +openFoundReports() vector~const FoundReport*~
        +updateStatus(id, status)
    }
    class ReportService {
        -ReportRepository& repository_
        -MatchEngine& engine_
        +registerUser(name, contact) string
        +submitLost(ReportInput) string
        +submitFound(ReportInput, heldAt) string
        +suggestMatches(lostId, k) vector~MatchResult~
        +explainMatch(lostId, foundId, considered) MatchResult
        +confirmMatch(lostId, foundId)
    }

    ItemReport <|-- LostReport
    ItemReport <|-- FoundReport
    SimilarityStrategy <|.. KeywordBaselineStrategy
    SimilarityStrategy <|.. PythonTfidfStrategy
    SimilarityStrategy <|.. FallbackStrategy
    FallbackStrategy o-- "2" SimilarityStrategy
    PythonTfidfStrategy *-- PythonTextClient
    PythonTfidfStrategy *-- KeywordBaselineStrategy
    PythonTextClient *-- PythonProcess
    MatchEngine *-- SimilarityStrategy
    ReportRepository *-- "*" ItemReport
    ReportRepository *-- "*" User
    ItemReport ..> User : reporterId
    ReportService --> ReportRepository
    ReportService --> MatchEngine
    SimilarityStrategy ..> MatchResult : creates
```

The supporting classes (`Date`, `CsvReader`, `DatasetConfig`, `DatasetAdapter`, `DatasetLoader`,
`KeyValueConfig`, `MatchingConfig`, `persistence::*`) are left out to keep the diagram readable.
They are listed in [DESIGN.md](DESIGN.md).

## Sequence: submitting a report

```mermaid
sequenceDiagram
    actor U as Student
    participant CLI as findit CLI
    participant S as ReportService
    participant L as LostReport
    participant R as ReportRepository
    participant P as persistence
    U->>CLI: 1. Report a lost item (fields)
    CLI->>S: submitLost(ReportInput)
    S->>R: findUser(reporterId)
    alt unknown reporter
        S-->>CLI: invalid_argument
        CLI-->>U: "Error: Unknown reporter ID", nothing saved
    end
    S->>R: nextReportId()
    S->>L: new LostReport(id, input) via make_unique
    alt invalid field (description < 10 chars, bad date, ...)
        L-->>S: invalid_argument
        S-->>CLI: error
        CLI-->>U: "Error: …", nothing saved
    end
    S->>R: addReport(unique_ptr) (ownership moves)
    S-->>CLI: "R030001"
    CLI->>P: appendReport(data/store/reports.csv)
    CLI-->>U: "Saved R030001"
```

## Sequence: retrieving matches

```mermaid
sequenceDiagram
    actor U as Student
    participant CLI as findit CLI
    participant S as ReportService
    participant E as MatchEngine
    participant F as FallbackStrategy
    participant PS as PythonTfidfStrategy
    participant C as PythonTextClient
    participant PY as Python service
    U->>CLI: 5. Find matches (R030001)
    CLI->>S: suggestMatches(id, 3)
    S->>E: findMatches(lost, openFoundReports, 3)
    E->>E: filter: OPEN, not self, same category, date window
    E->>F: score(lost, eligible)
    F->>PS: score(lost, eligible)
    opt first query of the session
        PS->>C: start() (posix_spawnp, read "ready")
        PS->>C: fit(all reports' descriptions)
        C->>PY: {"type":"fit", documents: [{description}]}
        PY-->>C: {"type":"fit_result"}
    end
    PS->>C: rank(lost, eligible)
    C->>PY: {"type":"rank", lost, candidates} (report_id + description only)
    PY-->>C: {"type":"rank_result", matches}
    C->>C: validate JSON (IDs, ranks, ranges, completeness)
    PS->>PS: combine text with category / colour / location (scored only in C++)
    PS-->>F: MatchResults
    alt Python unavailable or invalid reply
        PS-->>F: PythonMatcherError
        F->>F: switch to KeywordBaselineStrategy (once, with a warning)
    end
    F-->>E: MatchResults
    E->>E: threshold, sort (score desc, ID asc), top 3, ranks
    E-->>S: results
    S-->>CLI: results
    CLI-->>U: top 3 with scores and reasons
```

## Sequence: confirming a match (and restoring it after a restart)

```mermaid
sequenceDiagram
    actor U as Desk staff
    participant CLI as findit CLI
    participant S as ReportService
    participant R as ReportRepository
    participant P as persistence
    U->>CLI: 10. Confirm a match (lost ID, found ID)
    CLI->>S: getReport(lost), getReport(found)
    alt unknown ID, wrong types, or a report not OPEN
        CLI-->>U: "Error: … already MATCHED / not found", nothing changes
    end
    CLI->>S: explainMatch(lost, found)
    CLI-->>U: score + reasons, "Confirm? [y/N]"
    U->>CLI: y
    CLI->>S: confirmMatch(lost, found)
    S->>S: both exist, LOST + FOUND, both OPEN (checked before any change)
    S->>R: updateStatus(lost, MATCHED), updateStatus(found, MATCHED)
    CLI->>P: appendConfirmedMatch(data/store/matches.csv, lost, found, today)
    Note over CLI,P: On the next start: load reports (all OPEN), then<br/>loadConfirmedMatches replays each row through S.confirmMatch
```

Status rules: `OPEN → MATCHED` (confirmation, both reports together), `MATCHED → CLOSED` and
`OPEN → CLOSED` (allowed by the model, not in the menu). Nothing moves back to OPEN; enforced by
`ItemReport::setStatus`.
