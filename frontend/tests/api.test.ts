import { describe, expect, it } from "vitest";
import { ApiError, api } from "../src/services/api";
import { makeReport, mockApi } from "./helpers";

async function caught(promise: Promise<unknown>): Promise<ApiError> {
  try {
    await promise;
  } catch (error) {
    expect(error).toBeInstanceOf(ApiError);
    return error as ApiError;
  }
  throw new Error("expected the call to fail");
}

describe("api service", () => {
  it("returns parsed JSON on success", async () => {
    const report = makeReport();
    mockApi([{ path: "/api/reports/R000001", body: report }]);
    await expect(api.getReport("R000001")).resolves.toEqual(report);
  });

  it("builds report queries from non-empty filters only", async () => {
    const { calls } = mockApi([{ path: /^\/api\/reports\?/, body: { items: [], total: 0, limit: 12, offset: 12 } }]);
    await api.listReports({ q: "  black wallet ", type: "lost", status: "", category: "", limit: 12, offset: 12 });
    expect(calls[0]!.url).toBe("/api/reports?q=black+wallet&type=lost&limit=12&offset=12");
  });

  it("sends JSON bodies for POST requests and encodes IDs in paths", async () => {
    const { calls, fetchMock } = mockApi([
      { method: "POST", path: "/api/matches", body: { ok: true } },
      { path: "/api/reports/R%2F1/matches?k=3", body: {} },
    ]);
    await api.confirmMatch(" R000001 ", "R000002");
    expect(calls[0]!.body).toEqual({ lostId: "R000001", foundId: "R000002" });
    const init = fetchMock.mock.calls[0]![1] as RequestInit;
    expect((init.headers as Record<string, string>)["Content-Type"]).toBe("application/json");
    await api.findMatches("R/1");
    expect(calls[1]!.url).toBe("/api/reports/R%2F1/matches?k=3");
  });

  it("turns structured error bodies into ApiError with status and code", async () => {
    mockApi([{
      method: "POST", path: "/api/reports", status: 400,
      body: { error: { code: "validation_failed", message: "Description must be at least 10 characters" } },
    }]);
    const error = await caught(api.createReport({
      type: "lost", reporterId: "U001", category: "Wallet", description: "short", color: "black", location: "Library", date: "2026-03-20",
    }));
    expect(error.status).toBe(400);
    expect(error.code).toBe("validation_failed");
    expect(error.message).toBe("Description must be at least 10 characters");
    expect(error.isUnavailable).toBe(false);
  });

  it("keeps the server's message for a structured 409 conflict", async () => {
    mockApi([{ method: "POST", path: "/api/matches", status: 409, body: { error: { code: "conflict", message: "R000001 is already MATCHED" } } }]);
    const error = await caught(api.confirmMatch("R000001", "R000002"));
    expect([error.status, error.code, error.message]).toEqual([409, "conflict", "R000001 is already MATCHED"]);
  });

  it("reports a network failure as backend unavailable", async () => {
    mockApi([{ path: "/api/stats", networkError: true }]);
    const error = await caught(api.stats());
    expect(error.code).toBe("network_error");
    expect(error.status).toBe(0);
    expect(error.isUnavailable).toBe(true);
    expect(error.message).toMatch(/Cannot reach the FindIt API/);
  });

  it("treats a non-JSON 5xx (proxy cannot reach the server) as backend unavailable", async () => {
    mockApi([{ path: "/api/stats", status: 500, raw: "" }, { path: "/api/health", status: 502, raw: "Bad Gateway" }]);
    expect((await caught(api.stats())).code).toBe("backend_unavailable");
    expect((await caught(api.health())).isUnavailable).toBe(true);
  });

  it("uses the server's message for a structured 503", async () => {
    mockApi([{ path: "/api/reports/R000001/matches?k=3", status: 503, body: { error: { code: "matcher_unavailable", message: "The matching engine is unavailable" } } }]);
    const error = await caught(api.findMatches("R000001"));
    expect([error.code, error.message]).toEqual(["matcher_unavailable", "The matching engine is unavailable"]);
  });

  it("rejects non-JSON success bodies and unstructured 4xx errors", async () => {
    mockApi([
      { path: "/api/stats", status: 200, raw: "<html>not json</html>" },
      { path: "/api/health", status: 404, raw: "Not Found" },
    ]);
    expect((await caught(api.stats())).code).toBe("invalid_response");
    const error = await caught(api.health());
    expect([error.code, error.status]).toEqual(["invalid_response", 404]);
  });

  it("unwraps list endpoints for users and categories", async () => {
    mockApi([
      { path: "/api/users", body: { items: [{ id: "U001", name: "Asha" }] } },
      { path: "/api/categories", body: { items: [{ name: "Wallet", count: 3 }] } },
    ]);
    await expect(api.users()).resolves.toEqual([{ id: "U001", name: "Asha" }]);
    await expect(api.categories()).resolves.toEqual([{ name: "Wallet", count: 3 }]);
  });
});
