// Types for the findit_server JSON API (docs/API.md), checked against real responses.

export type ReportType = "lost" | "found";
export type ReportStatus = "open" | "matched" | "closed";
export type ReportOrigin = "dataset" | "app";

export interface Report {
  id: string;
  type: ReportType;
  status: ReportStatus;
  category: string;
  description: string;
  color: string;
  brand: string | null;
  location: string;
  date: string; // YYYY-MM-DD
  reporterId: string;
  heldAt: string | null; // Found reports only.
  origin: ReportOrigin; // "dataset" = imported synthetic data, "app" = created in the CLI/UI.
  sourceRef: string | null;
}

export interface Health {
  status: "ok";
  dataMode: "dataset" | "sample";
  persistent: boolean;
  sampleData: boolean;
  matcher: {
    strategy: string;
    textEngine: "python" | "keyword";
    fallbackActive: boolean;
    categoryBlocking: boolean;
  };
}

export interface Stats {
  totalReports: number;
  lostReports: number;
  foundReports: number;
  openLostReports: number;
  openFoundReports: number;
  confirmedMatches: number;
  reportsCreatedInApp: number;
}

export interface ReportList {
  items: Report[];
  total: number;
  limit: number;
  offset: number;
}

export interface ReportQuery {
  q?: string;
  type?: ReportType | "";
  status?: ReportStatus | "";
  category?: string;
  origin?: ReportOrigin | "";
  sort?: "recent" | "id";
  limit?: number; // 1-100 (server default 20)
  offset?: number;
}

export interface Category {
  name: string;
  count: number;
}

export interface User {
  id: string;
  name: string;
}

export interface CreatedUser extends User {
  persisted: boolean;
}

export interface NewUser {
  name: string;
  contact?: string;
}

export interface NewReport {
  type: ReportType;
  reporterId: string;
  category: string;
  description: string;
  color: string;
  location: string;
  date: string; // YYYY-MM-DD
  brand?: string;
  heldAt?: string; // Found reports only.
}

export type CreatedReport = Report & { persisted: boolean };

export interface MatchCandidate {
  rank: number;
  score: number; // Relative ranking value in [0, 1]; not a probability.
  components: Record<string, number>; // text, location, category, colour
  explanations: string[];
  found: Report;
}

export interface MatchResponse {
  lost: Report;
  matcher: { strategy: string; fallbackActive: boolean; categoryBlocking: boolean };
  scoreNote: string;
  matches: MatchCandidate[];
  emptyReason: null | "no_open_found_reports_in_category" | "no_open_found_reports";
}

export interface ConfirmResponse {
  lost: Report;
  found: Report;
  confirmedOn: string;
  persisted: boolean;
}

// Error body returned by the server: {"error": {"code": ..., "message": ...}}.
export interface ApiErrorBody {
  error: { code: string; message: string };
}
