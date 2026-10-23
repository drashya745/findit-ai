// The only module that talks HTTP. Components call these functions and never use fetch().
import type {
  Category,
  ConfirmResponse,
  CreatedReport,
  CreatedUser,
  Health,
  MatchResponse,
  NewReport,
  NewUser,
  Report,
  ReportList,
  ReportQuery,
  Stats,
  User,
} from "../types/api";

// Same-origin by default (the Vite dev/preview server proxies /api to 127.0.0.1:8080).
const BASE_URL = (import.meta.env.VITE_API_BASE_URL ?? "").replace(/\/+$/, "");

export type ApiErrorCode =
  | "network_error" // The request never reached a server.
  | "backend_unavailable" // The proxy answered, but findit_server did not.
  | "invalid_response" // Not the JSON we expected.
  | (string & {}); // Any code sent by the server, e.g. "validation_failed", "conflict".

export class ApiError extends Error {
  readonly status: number; // 0 when no HTTP response was received.
  readonly code: ApiErrorCode;

  constructor(message: string, status: number, code: ApiErrorCode) {
    super(message);
    this.name = "ApiError";
    this.status = status;
    this.code = code;
  }

  /** True when the backend could not be reached at all. */
  get isUnavailable(): boolean {
    return this.code === "network_error" || this.code === "backend_unavailable";
  }
}

const UNAVAILABLE_MESSAGE =
  "Cannot reach the FindIt API. Start it with ./build/findit_server (from the project root) and try again.";

function isErrorBody(value: unknown): value is { error: { code?: unknown; message?: unknown } } {
  return typeof value === "object" && value !== null && "error" in value &&
    typeof (value as { error: unknown }).error === "object" && (value as { error: unknown }).error !== null;
}

async function request<T>(path: string, init?: { method?: "GET" | "POST"; body?: unknown }): Promise<T> {
  let response: Response;
  try {
    response = await fetch(`${BASE_URL}${path}`, {
      method: init?.method ?? "GET",
      headers: init?.body === undefined
        ? { Accept: "application/json" }
        : { Accept: "application/json", "Content-Type": "application/json" },
      body: init?.body === undefined ? undefined : JSON.stringify(init.body),
    });
  } catch {
    throw new ApiError(UNAVAILABLE_MESSAGE, 0, "network_error");
  }

  const text = await response.text();
  let body: unknown;
  let isJson = false;
  if (text.length > 0) {
    try {
      body = JSON.parse(text);
      isJson = true;
    } catch {
      isJson = false;
    }
  }

  if (!response.ok) {
    if (isJson && isErrorBody(body) && typeof body.error.message === "string") {
      const code = typeof body.error.code === "string" ? body.error.code : "error";
      throw new ApiError(body.error.message, response.status, code);
    }
    // Without a structured error, a 5xx means the proxy could not reach findit_server.
    if (response.status >= 500) {
      throw new ApiError(UNAVAILABLE_MESSAGE, response.status, "backend_unavailable");
    }
    throw new ApiError(`Unexpected response from the API (HTTP ${response.status}).`, response.status, "invalid_response");
  }
  if (!isJson) {
    throw new ApiError("The API returned a response that is not JSON.", response.status, "invalid_response");
  }
  return body as T;
}

function queryString(params: Record<string, string | number | undefined>): string {
  const search = new URLSearchParams();
  for (const [key, value] of Object.entries(params)) {
    if (value !== undefined && value !== "") {
      search.set(key, String(value));
    }
  }
  const text = search.toString();
  return text ? `?${text}` : "";
}

const segment = (id: string) => encodeURIComponent(id.trim());

export const api = {
  health: () => request<Health>("/api/health"),
  stats: () => request<Stats>("/api/stats"),
  listReports: (query: ReportQuery = {}) =>
    request<ReportList>(
      `/api/reports${queryString({
        q: query.q?.trim(),
        type: query.type,
        status: query.status,
        category: query.category,
        origin: query.origin,
        sort: query.sort,
        limit: query.limit,
        offset: query.offset,
      })}`,
    ),
  getReport: (id: string) => request<Report>(`/api/reports/${segment(id)}`),
  categories: () => request<{ items: Category[] }>("/api/categories").then((r) => r.items),
  users: () => request<{ items: User[] }>("/api/users").then((r) => r.items),
  createUser: (user: NewUser) => request<CreatedUser>("/api/users", { method: "POST", body: user }),
  createReport: (report: NewReport) => request<CreatedReport>("/api/reports", { method: "POST", body: report }),
  findMatches: (lostId: string, k = 3) =>
    request<MatchResponse>(`/api/reports/${segment(lostId)}/matches${queryString({ k })}`),
  confirmMatch: (lostId: string, foundId: string) =>
    request<ConfirmResponse>("/api/matches", { method: "POST", body: { lostId: lostId.trim(), foundId: foundId.trim() } }),
};
