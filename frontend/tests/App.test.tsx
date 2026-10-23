import { render, screen, within } from "@testing-library/react";
import { MemoryRouter } from "react-router-dom";
import { describe, expect, it } from "vitest";
import { App } from "../src/App";
import { makeReport, mockApi } from "./helpers";

const health = {
  status: "ok", dataMode: "dataset", persistent: true, sampleData: false,
  matcher: { strategy: "python-tfidf-v1", textEngine: "python", fallbackActive: false, categoryBlocking: true },
};
const stats = {
  totalReports: 30002, lostReports: 15001, foundReports: 15001, openLostReports: 15000, openFoundReports: 15000,
  confirmedMatches: 1, reportsCreatedInApp: 2,
};

function renderApp(url = "/") {
  return render(
    <MemoryRouter initialEntries={[url]}>
      <App />
    </MemoryRouter>,
  );
}

describe("App shell and dashboard", () => {
  it("shows the synthetic-data banner, navigation, real counts and recent reports", async () => {
    mockApi([
      { path: "/api/health", body: health },
      { path: "/api/stats", body: stats },
      { path: "/api/reports?sort=recent&limit=6", body: { items: [makeReport({ id: "R030001", origin: "app" })], total: 30002, limit: 6, offset: 0 } },
    ]);
    renderApp();
    expect(screen.getByText("Dataset: synthetic sample data.")).toBeInTheDocument();
    const nav = screen.getByRole("navigation", { name: "Main" });
    for (const name of ["Dashboard", "Report an item", "Browse reports", "Find matches"]) {
      expect(within(nav).getByRole("link", { name })).toBeInTheDocument();
    }
    const summary = screen.getByRole("region", { name: "Summary" });
    expect(await within(summary).findByText("30,002")).toBeInTheDocument();
    expect(within(summary).getAllByText("15,001")).toHaveLength(2);
    expect(within(summary).getByText("1")).toBeInTheDocument();
    expect(await screen.findByRole("article", { name: "Report R030001" })).toBeInTheDocument();
    expect(await screen.findByText(/Matcher: python-tfidf-v1/)).toBeInTheDocument();
  });

  it("shows a connection error instead of numbers when the backend is down", async () => {
    mockApi([
      { path: "/api/health", networkError: true },
      { path: "/api/stats", networkError: true },
      { path: "/api/reports?sort=recent&limit=6", networkError: true },
    ]);
    renderApp();
    expect(await screen.findByText("Backend unavailable")).toBeInTheDocument();
    expect(screen.queryByRole("region", { name: "Summary" })).not.toBeInTheDocument();
    expect(await screen.findByText("API not reachable")).toBeInTheDocument();
    expect(screen.getByText("Dataset: synthetic sample data.")).toBeInTheDocument();
  });

  it("renders a not-found page for unknown routes", async () => {
    mockApi([{ path: "/api/health", body: health }]);
    renderApp("/nope");
    expect(screen.getByText("Page not found")).toBeInTheDocument();
    expect(await screen.findByText(/Matcher: python-tfidf-v1/)).toBeInTheDocument(); // Shell still works.
  });
});
