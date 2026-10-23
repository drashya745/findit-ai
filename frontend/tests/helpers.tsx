// Test-only helpers: a fetch mock that routes by method + path, report fixtures, and
// render-with-router. Nothing here is used by production code.
import { render } from "@testing-library/react";
import type { ReactElement } from "react";
import { MemoryRouter, Route, Routes } from "react-router-dom";
import { vi } from "vitest";
import type { MatchCandidate, MatchResponse, Report } from "../src/types/api";

export interface MockRoute {
  method?: "GET" | "POST";
  path: string | RegExp; // Matched against path + query, e.g. "/api/reports?limit=12".
  status?: number;
  body?: unknown;
  raw?: string; // Raw response text (for non-JSON responses).
  networkError?: boolean;
  respond?: () => Promise<{ status: number; body: unknown }>; // For delayed/controlled responses.
}

export interface RecordedCall {
  method: string;
  url: string;
  body: unknown;
}

/** Installs a fetch mock. Unexpected requests fail the test loudly. */
export function mockApi(routes: MockRoute[]) {
  const calls: RecordedCall[] = [];
  const fetchMock = vi.fn(async (input: RequestInfo | URL, init?: RequestInit) => {
    const url = String(input);
    const method = init?.method ?? "GET";
    calls.push({ method, url, body: init?.body ? JSON.parse(String(init.body)) : undefined });
    const route = routes.find(
      (r) => (r.method ?? "GET") === method && (typeof r.path === "string" ? r.path === url : r.path.test(url)),
    );
    if (!route) {
      throw new Error(`Unexpected request in test: ${method} ${url}`);
    }
    if (route.networkError) {
      throw new TypeError("Failed to fetch");
    }
    const resolved = route.respond ? await route.respond() : { status: route.status ?? 200, body: route.body };
    const text = route.raw ?? (resolved.body === undefined ? "" : JSON.stringify(resolved.body));
    return {
      ok: resolved.status >= 200 && resolved.status < 300,
      status: resolved.status,
      text: async () => text,
    } as Response;
  });
  vi.stubGlobal("fetch", fetchMock);
  return { calls, fetchMock };
}

/** A promise you resolve from the test, to observe in-flight UI states. */
export function deferred<T>() {
  let resolve!: (value: T) => void;
  const promise = new Promise<T>((r) => {
    resolve = r;
  });
  return { promise, resolve };
}

export function makeReport(overrides: Partial<Report> = {}): Report {
  return {
    id: "R000001",
    type: "lost",
    status: "open",
    category: "Wallet",
    description: "Black leather wallet with a student ID card",
    color: "black",
    brand: null,
    location: "Library",
    date: "2026-03-20",
    reporterId: "U001",
    heldAt: null,
    origin: "dataset",
    sourceRef: "1",
    ...overrides,
  };
}

export function makeCandidate(rank: number, overrides: Partial<Report> = {}, score = 0.9 - rank * 0.1): MatchCandidate {
  return {
    rank,
    score,
    components: { text: 0.8, location: 1, category: 1, colour: 0.5 },
    explanations: [`Text (TF-IDF): Exact shared terms: wallet, leather (#${rank})`, "Same category: Wallet"],
    found: makeReport({ id: `R00000${rank + 1}`, type: "found", description: `Found black wallet number ${rank}`, ...overrides }),
  };
}

export function makeMatchResponse(overrides: Partial<MatchResponse> = {}): MatchResponse {
  return {
    lost: makeReport(),
    matcher: { strategy: "python-tfidf-v1", fallbackActive: false, categoryBlocking: true },
    scoreNote: "Scores are relative ranking values between 0 and 1 for comparing candidates; they are not probabilities.",
    matches: [makeCandidate(1), makeCandidate(2), makeCandidate(3)],
    emptyReason: null,
    ...overrides,
  };
}

/** Renders a page at `url`, with `path` as its route pattern. */
export function renderAt(element: ReactElement, url: string, path: string) {
  return render(
    <MemoryRouter initialEntries={[url]}>
      <Routes>
        <Route path={path} element={element} />
        <Route path="*" element={<p>other page</p>} />
      </Routes>
    </MemoryRouter>,
  );
}
