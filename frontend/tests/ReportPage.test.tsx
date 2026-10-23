import { screen, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it } from "vitest";
import { ReportPage, fieldForServerMessage } from "../src/pages/ReportPage";
import { deferred, makeReport, mockApi, renderAt, type MockRoute } from "./helpers";

const optionRoutes: MockRoute[] = [
  { path: "/api/users", body: { items: [{ id: "U001", name: "Suma" }, { id: "U002", name: "Yash" }] } },
  { path: "/api/categories", body: { items: [{ name: "Wallet", count: 3 }] } },
];

async function fillValidLostReport(user: ReturnType<typeof userEvent.setup>) {
  await screen.findByRole("option", { name: "Suma (U001)" });
  await user.selectOptions(screen.getByLabelText(/Reporter/), "U001");
  await user.type(screen.getByLabelText(/Description/), "Black leather wallet with a student ID card");
  await user.type(screen.getByLabelText(/Category/), "Wallet");
  await user.type(screen.getByLabelText(/Colour/), "black");
  await user.type(screen.getByLabelText(/Location/), "Library");
  const date = screen.getByLabelText(/^Date/);
  await user.clear(date);
  await user.type(date, "2026-03-20");
}

describe("Report an item", () => {
  it("shows field-level errors for missing required fields and sends nothing", async () => {
    const user = userEvent.setup();
    const { calls } = mockApi(optionRoutes);
    renderAt(<ReportPage />, "/report", "/report");
    await screen.findByRole("option", { name: "Suma (U001)" });
    await user.click(screen.getByRole("button", { name: /Submit lost report/ }));

    expect(screen.getByText("Choose who is reporting this item.")).toBeInTheDocument();
    for (const message of ["Description is required.", "Category is required.", "Colour is required.", "Location is required."]) {
      expect(screen.getByText(message)).toBeInTheDocument();
    }
    expect(screen.getByLabelText(/Description/)).toHaveAttribute("aria-invalid", "true");
    expect(calls.filter((c) => c.method === "POST")).toHaveLength(0);
  });

  it("submits a lost report and shows success only after the API accepts it", async () => {
    const user = userEvent.setup();
    const reply = deferred<{ status: number; body: unknown }>();
    const { calls } = mockApi([...optionRoutes, { method: "POST", path: "/api/reports", respond: () => reply.promise }]);
    renderAt(<ReportPage />, "/report", "/report");
    await fillValidLostReport(user);
    await user.click(screen.getByRole("button", { name: /Submit lost report/ }));

    // In flight: button disabled, no success yet.
    expect(await screen.findByRole("button", { name: /Saving/ })).toBeDisabled();
    expect(screen.queryByText(/Report R030001 saved/)).not.toBeInTheDocument();
    expect(calls.find((c) => c.method === "POST")!.body).toEqual({
      type: "lost", reporterId: "U001", category: "Wallet", description: "Black leather wallet with a student ID card",
      color: "black", location: "Library", date: "2026-03-20",
    });

    reply.resolve({ status: 201, body: { ...makeReport({ id: "R030001", origin: "app", sourceRef: null }), persisted: true } });
    expect(await screen.findByText("Report R030001 saved")).toBeInTheDocument();
    expect(screen.getByRole("link", { name: "Find matches" })).toHaveAttribute("href", "/matches?lost=R030001");
    expect(screen.getByLabelText(/Description/)).toHaveValue(""); // Form reset for the next report.
  });

  it("shows server validation errors next to the field they concern", async () => {
    const user = userEvent.setup();
    mockApi([...optionRoutes, {
      method: "POST", path: "/api/reports", status: 400,
      body: { error: { code: "validation_failed", message: "Description must be at least 10 characters" } },
    }]);
    renderAt(<ReportPage />, "/report", "/report");
    await fillValidLostReport(user);
    await user.click(screen.getByRole("button", { name: /Submit lost report/ }));

    expect(await screen.findByText("Description must be at least 10 characters")).toBeInTheDocument();
    expect(screen.getByLabelText(/Description/)).toHaveAttribute("aria-invalid", "true");
    expect(screen.queryByText(/saved/)).not.toBeInTheDocument();
  });

  it("maps an unknown reporter error to the reporter field", async () => {
    const user = userEvent.setup();
    mockApi([...optionRoutes, {
      method: "POST", path: "/api/reports", status: 400,
      body: { error: { code: "validation_failed", message: "Unknown reporter ID: 'U001'" } },
    }]);
    renderAt(<ReportPage />, "/report", "/report");
    await fillValidLostReport(user);
    await user.click(screen.getByRole("button", { name: /Submit lost report/ }));
    expect(await screen.findByText("Unknown reporter ID: 'U001'")).toBeInTheDocument();
    expect(screen.getByLabelText(/Reporter/)).toHaveAttribute("aria-invalid", "true");
  });

  it("sends heldAt for found reports and registers a new person first", async () => {
    const user = userEvent.setup();
    const { calls } = mockApi([
      ...optionRoutes,
      { method: "POST", path: "/api/users", status: 201, body: { id: "U021", name: "New Student", persisted: true } },
      { method: "POST", path: "/api/reports", status: 201, body: { ...makeReport({ id: "R030002", type: "found" }), persisted: true } },
    ]);
    renderAt(<ReportPage />, "/report", "/report");
    await fillValidLostReport(user);
    await user.click(screen.getByText("I found something"));
    await user.selectOptions(screen.getByLabelText(/Reporter/), "__new__");
    await user.type(screen.getByLabelText(/New person's name/), "New Student");
    await user.type(screen.getByLabelText(/Where is it held now/), "Security office");
    await user.click(screen.getByRole("button", { name: /Submit found report/ }));

    expect(await screen.findByText("Report R030002 saved")).toBeInTheDocument();
    const posts = calls.filter((c) => c.method === "POST");
    expect(posts.map((c) => c.url)).toEqual(["/api/users", "/api/reports"]);
    expect(posts[0]!.body).toEqual({ name: "New Student" });
    expect(posts[1]!.body).toMatchObject({ type: "found", reporterId: "U021", heldAt: "Security office" });
  });

  it("explains when the backend is unavailable", async () => {
    mockApi([{ path: "/api/users", networkError: true }, { path: "/api/categories", networkError: true }]);
    renderAt(<ReportPage />, "/report", "/report");
    const alert = await screen.findByRole("alert");
    expect(within(alert).getByText(/Cannot reach the FindIt API/)).toBeInTheDocument();
    expect(within(alert).getByRole("button", { name: /Retry/ })).toBeInTheDocument();
  });

  it("maps server messages to fields only by their leading field name", () => {
    expect(fieldForServerMessage("Colour is required", false)).toBe("color");
    expect(fieldForServerMessage("Date must use the format YYYY-MM-DD: 'x'", false)).toBe("date");
    expect(fieldForServerMessage("User name must be at least 2 characters", true)).toBe("newName");
    expect(fieldForServerMessage("Something unexpected", false)).toBeNull();
  });
});
