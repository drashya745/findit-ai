import { screen, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it } from "vitest";
import { MatchesPage, SCORE_LABEL } from "../src/pages/MatchesPage";
import { deferred, makeMatchResponse, makeReport, mockApi, renderAt } from "./helpers";

const MATCHES_URL = "/api/reports/R000001/matches?k=3";

describe("Find matches", () => {
  it("loads the top three candidates from the matching endpoint and renders scores and explanations", async () => {
    const { calls } = mockApi([{ path: MATCHES_URL, body: makeMatchResponse() }]);
    renderAt(<MatchesPage />, "/matches?lost=R000001", "/matches");

    const cards = await screen.findAllByRole("article", { name: /^Report R00000[234]$/ });
    expect(cards).toHaveLength(3);
    expect(calls[0]!.url).toBe(MATCHES_URL);
    expect(screen.getByText(SCORE_LABEL)).toBeInTheDocument();

    const first = cards[0]!;
    expect(within(first).getByText("#1")).toBeInTheDocument();
    expect(within(first).getByTitle(SCORE_LABEL)).toHaveTextContent("0.80"); // Score 0.9 - 0.1.
    expect(within(cards[2]!).getByTitle(SCORE_LABEL)).toHaveTextContent("0.60"); // Ranked order kept.
    expect(within(first).getByText("(relative ranking, not a probability)")).toBeInTheDocument();
    expect(within(first).getByText("Description (TF-IDF)")).toBeInTheDocument();
    expect(within(first).getByText("Text (TF-IDF): Exact shared terms: wallet, leather (#1)")).toBeInTheDocument();
    expect(within(first).getByText("Found black wallet number 1")).toBeInTheDocument();
    expect(screen.getByText(/python-tfidf-v1/)).toBeInTheDocument();
  });

  it("asks for confirmation, blocks repeats while saving, and reports success only after the backend saves", async () => {
    const user = userEvent.setup();
    const reply = deferred<{ status: number; body: unknown }>();
    const { calls } = mockApi([
      { path: MATCHES_URL, body: makeMatchResponse() },
      { method: "POST", path: "/api/matches", respond: () => reply.promise },
    ]);
    renderAt(<MatchesPage />, "/matches?lost=R000001", "/matches");
    const buttons = await screen.findAllByRole("button", { name: "Confirm match" });
    await user.click(buttons[0]!);

    const dialog = screen.getByRole("dialog", { name: "Confirm this match?" });
    expect(within(dialog).getByText("R000002")).toBeInTheDocument();
    expect(calls.filter((c) => c.method === "POST")).toHaveLength(0); // Nothing sent before "yes".
    await user.click(within(dialog).getByRole("button", { name: "Confirm match" }));

    // In flight: dialog buttons and every candidate's Confirm button are disabled; no success yet.
    expect(within(dialog).getByRole("button", { name: "Saving…" })).toBeDisabled();
    for (const button of screen.getAllByRole("button", { name: "Confirm match" })) {
      expect(button).toBeDisabled();
    }
    expect(screen.queryByText("Match confirmed")).not.toBeInTheDocument();
    expect(calls.filter((c) => c.method === "POST")).toEqual([
      { method: "POST", url: "/api/matches", body: { lostId: "R000001", foundId: "R000002" } },
    ]);

    reply.resolve({
      status: 200,
      body: {
        lost: makeReport({ status: "matched" }),
        found: makeReport({ id: "R000002", type: "found", status: "matched" }),
        confirmedOn: "2026-10-23",
        persisted: true,
      },
    });
    expect(await screen.findByText("Match confirmed")).toBeInTheDocument();
    expect(screen.getByText(/R000001 and R000002 are now marked as matched \(saved 2026-10-23\)/)).toBeInTheDocument();
    expect(screen.getByText("Confirmed as matched")).toBeInTheDocument();
    expect(screen.queryByRole("dialog")).not.toBeInTheDocument();
    // The lost report is matched now, so no candidate offers an action that would fail.
    expect(screen.queryByRole("button", { name: "Confirm match" })).not.toBeInTheDocument();
  });

  it("cancelling the dialog sends nothing", async () => {
    const user = userEvent.setup();
    const { calls } = mockApi([{ path: MATCHES_URL, body: makeMatchResponse() }]);
    renderAt(<MatchesPage />, "/matches?lost=R000001", "/matches");
    await user.click((await screen.findAllByRole("button", { name: "Confirm match" }))[1]!);
    await user.keyboard("{Escape}");
    expect(screen.queryByRole("dialog")).not.toBeInTheDocument();
    expect(calls.filter((c) => c.method === "POST")).toHaveLength(0);
  });

  it("shows a clear message for HTTP 409 and no success", async () => {
    const user = userEvent.setup();
    mockApi([
      { path: MATCHES_URL, body: makeMatchResponse() },
      { method: "POST", path: "/api/matches", status: 409,
        body: { error: { code: "conflict", message: "R000002 is already MATCHED; only OPEN reports can be confirmed as a match" } } },
    ]);
    renderAt(<MatchesPage />, "/matches?lost=R000001", "/matches");
    await user.click((await screen.findAllByRole("button", { name: "Confirm match" }))[0]!);
    await user.click(within(screen.getByRole("dialog")).getByRole("button", { name: "Confirm match" }));

    const alert = await screen.findByRole("alert");
    expect(within(alert).getByText("Already matched — nothing was changed")).toBeInTheDocument();
    expect(within(alert).getByText(/R000002 is already MATCHED/)).toBeInTheDocument();
    expect(screen.queryByText("Match confirmed")).not.toBeInTheDocument();
  });

  it("explains empty results", async () => {
    mockApi([{ path: MATCHES_URL, body: makeMatchResponse({ matches: [], emptyReason: "no_open_found_reports_in_category" }) }]);
    renderAt(<MatchesPage />, "/matches?lost=R000001", "/matches");
    expect(await screen.findByText("No candidates found")).toBeInTheDocument();
    expect(screen.getByText(/no open found reports in the category “Wallet”/)).toBeInTheDocument();
  });

  it("handles an already matched lost report (409 not_open)", async () => {
    mockApi([{ path: MATCHES_URL, status: 409, body: { error: { code: "not_open", message: "R000001 is already MATCHED; it no longer needs matches" } } }]);
    renderAt(<MatchesPage />, "/matches?lost=R000001", "/matches");
    expect(await screen.findByText("R000001 is already matched")).toBeInTheDocument();
    expect(screen.queryByRole("button", { name: "Confirm match" })).not.toBeInTheDocument();
  });

  it("handles invalid IDs and found-report IDs", async () => {
    mockApi([
      { path: "/api/reports/ABC/matches?k=3", status: 400, body: { error: { code: "invalid_id", message: "'ABC' is not a report ID (expected R followed by 6 digits)" } } },
      { path: "/api/reports/R000002/matches?k=3", status: 400, body: { error: { code: "not_a_lost_report", message: "R000002 is a found report; matches are searched for lost reports" } } },
    ]);
    const { unmount } = renderAt(<MatchesPage />, "/matches?lost=abc", "/matches");
    expect(await screen.findByText("Report not found")).toBeInTheDocument();
    expect(screen.getByText(/is not a report ID/)).toBeInTheDocument();
    unmount();
    renderAt(<MatchesPage />, "/matches?lost=R000002", "/matches");
    expect(await screen.findByText("That is a found report")).toBeInTheDocument();
  });

  it("looks up the ID typed into the search field", async () => {
    const user = userEvent.setup();
    const { calls } = mockApi([{ path: MATCHES_URL, body: makeMatchResponse() }]);
    renderAt(<MatchesPage />, "/matches", "/matches");
    expect(screen.getByText("No lost report selected")).toBeInTheDocument();
    await user.type(screen.getByLabelText("Lost report ID"), "  r000001 ");
    await user.click(screen.getByRole("button", { name: /Find matches/ }));
    await screen.findAllByRole("button", { name: "Confirm match" });
    expect(calls[0]!.url).toBe(MATCHES_URL);
  });

  it("shows a connection error when the backend is unavailable", async () => {
    mockApi([{ path: MATCHES_URL, networkError: true }]);
    renderAt(<MatchesPage />, "/matches?lost=R000001", "/matches");
    const alert = await screen.findByRole("alert");
    expect(within(alert).getByText("Backend unavailable")).toBeInTheDocument();
    expect(within(alert).getByRole("button", { name: /Retry/ })).toBeInTheDocument();
  });
});
