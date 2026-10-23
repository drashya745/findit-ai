import { screen, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it } from "vitest";
import { BrowsePage } from "../src/pages/BrowsePage";
import { makeReport, mockApi, renderAt, type MockRoute } from "./helpers";

const categories: MockRoute = { path: "/api/categories", body: { items: [{ name: "Wallet", count: 3 }, { name: "Umbrella", count: 1 }] } };
const twoReports = {
  items: [
    makeReport({ id: "R000001", type: "lost", description: "Black leather wallet with a student ID card" }),
    makeReport({ id: "R000002", type: "found", description: "Green folding umbrella left on a bench", category: "Umbrella" }),
  ],
  total: 2, limit: 12, offset: 0,
};

const reportCalls = (calls: Array<{ url: string }>) => calls.filter((c) => c.url.startsWith("/api/reports"));

describe("Browse reports", () => {
  it("renders real report data with a match link only for open lost reports", async () => {
    mockApi([categories, { path: "/api/reports?sort=recent&limit=12&offset=0", body: twoReports }]);
    renderAt(<BrowsePage />, "/browse", "/browse");

    const lost = await screen.findByRole("article", { name: "Report R000001" });
    expect(within(lost).getByText("Black leather wallet with a student ID card")).toBeInTheDocument();
    expect(within(lost).getByText("Lost")).toBeInTheDocument();
    expect(within(lost).getByText("Open")).toBeInTheDocument();
    expect(within(lost).getByRole("link", { name: /Find matches/ })).toHaveAttribute("href", "/matches?lost=R000001");
    const found = screen.getByRole("article", { name: "Report R000002" });
    expect(within(found).queryByRole("link", { name: /Find matches/ })).not.toBeInTheDocument();
    expect(screen.getByText("Showing 1–2 of 2 reports")).toBeInTheDocument();
  });

  it("searches by keywords through the API", async () => {
    const user = userEvent.setup();
    const { calls } = mockApi([categories, { path: /^\/api\/reports\?/, body: twoReports }]);
    renderAt(<BrowsePage />, "/browse", "/browse");
    await screen.findByText("Showing 1–2 of 2 reports");
    await user.type(screen.getByRole("searchbox", { name: "Search reports" }), "black wallet");
    await user.click(screen.getByRole("button", { name: "Search" }));
    await screen.findByText("Showing 1–2 of 2 reports");
    expect(reportCalls(calls).at(-1)!.url).toBe("/api/reports?q=black+wallet&sort=recent&limit=12&offset=0");
  });

  it("filters by type, category and status on the server", async () => {
    const user = userEvent.setup();
    const { calls } = mockApi([categories, { path: /^\/api\/reports\?/, body: twoReports }]);
    renderAt(<BrowsePage />, "/browse?page=2", "/browse");
    await screen.findByRole("option", { name: "Wallet (3)" });
    await user.selectOptions(screen.getByLabelText("Type"), "lost");
    await user.selectOptions(screen.getByLabelText("Category"), "Wallet");
    await user.selectOptions(screen.getByLabelText("Status"), "open");
    await screen.findByText("Showing 1–2 of 2 reports");
    // Changing a filter returns to page 1 (offset 0).
    expect(reportCalls(calls).at(-1)!.url).toBe("/api/reports?type=lost&status=open&category=Wallet&sort=recent&limit=12&offset=0");
    await user.click(screen.getByRole("button", { name: /Clear filters/ }));
    await screen.findByText("Showing 1–2 of 2 reports");
    expect(reportCalls(calls).at(-1)!.url).toBe("/api/reports?sort=recent&limit=12&offset=0");
  });

  it("paginates with the API's limit/offset", async () => {
    const user = userEvent.setup();
    const many = { ...twoReports, total: 30 };
    const { calls } = mockApi([categories, { path: /^\/api\/reports\?/, body: many }]);
    renderAt(<BrowsePage />, "/browse", "/browse");
    await screen.findByText("Page 1 of 3");
    expect(screen.getByRole("button", { name: /Previous/ })).toBeDisabled();
    await user.click(screen.getByRole("button", { name: /Next/ }));
    await screen.findByText("Page 2 of 3");
    expect(reportCalls(calls).at(-1)!.url).toBe("/api/reports?sort=recent&limit=12&offset=12");
  });

  it("shows an empty state when nothing matches", async () => {
    mockApi([categories, { path: /^\/api\/reports\?/, body: { items: [], total: 0, limit: 12, offset: 0 } }]);
    renderAt(<BrowsePage />, "/browse?q=zzz", "/browse");
    expect(await screen.findByText("No reports match")).toBeInTheDocument();
    expect(screen.getByText(/Try fewer keywords/)).toBeInTheDocument();
  });

  it("shows a connection error (not fake data) when the backend is down, with retry", async () => {
    const user = userEvent.setup();
    const { calls } = mockApi([
      { path: "/api/categories", networkError: true },
      { path: /^\/api\/reports\?/, status: 502, raw: "Bad Gateway" },
    ]);
    renderAt(<BrowsePage />, "/browse", "/browse");
    const alert = await screen.findByRole("alert");
    expect(within(alert).getByText("Backend unavailable")).toBeInTheDocument();
    expect(screen.queryByRole("article")).not.toBeInTheDocument();
    await user.click(within(alert).getByRole("button", { name: /Retry/ }));
    expect(reportCalls(calls)).toHaveLength(2);
  });
});
