import { ChevronLeft, ChevronRight, Search, X } from "lucide-react";
import { useCallback, useEffect, useState, type FormEvent } from "react";
import { useSearchParams } from "react-router-dom";
import { ReportCard } from "../components/ReportCard";
import { EmptyState, ErrorAlert, Loading } from "../components/States";
import { api } from "../services/api";
import type { Category, ReportList, ReportStatus, ReportType } from "../types/api";

export const PAGE_SIZE = 12;

export function BrowsePage() {
  const [params, setParams] = useSearchParams();
  const q = params.get("q") ?? "";
  const type = (params.get("type") ?? "") as ReportType | "";
  const status = (params.get("status") ?? "") as ReportStatus | "";
  const category = params.get("category") ?? "";
  const page = Math.max(1, Number.parseInt(params.get("page") ?? "1", 10) || 1);

  const [draft, setDraft] = useState(q);
  const [categories, setCategories] = useState<Category[]>([]);
  const [result, setResult] = useState<ReportList | null>(null);
  const [error, setError] = useState<unknown>(null);
  const [loading, setLoading] = useState(true);

  useEffect(() => setDraft(q), [q]);
  useEffect(() => {
    api.categories().then(setCategories, () => setCategories([])); // The list itself reports errors.
  }, []);

  const load = useCallback(() => {
    let active = true;
    setLoading(true);
    setError(null);
    api.listReports({ q, type, status, category, sort: "recent", limit: PAGE_SIZE, offset: (page - 1) * PAGE_SIZE }).then(
      (list) => {
        if (!active) return;
        setResult(list);
        setLoading(false);
      },
      (e: unknown) => {
        if (!active) return;
        setError(e);
        setResult(null);
        setLoading(false);
      },
    );
    return () => {
      active = false;
    };
  }, [q, type, status, category, page]);
  useEffect(load, [load]);

  /** Updates one or more filters and returns to page 1. */
  const update = (changes: Record<string, string>) => {
    const next = new URLSearchParams(params);
    for (const [key, value] of Object.entries(changes)) {
      if (value) next.set(key, value);
      else next.delete(key);
    }
    if (!("page" in changes)) next.delete("page");
    setParams(next);
  };

  const onSearch = (event: FormEvent) => {
    event.preventDefault();
    update({ q: draft.trim() });
  };

  const total = result?.total ?? 0;
  const pages = Math.max(1, Math.ceil(total / PAGE_SIZE));
  const first = total === 0 ? 0 : (page - 1) * PAGE_SIZE + 1;
  const last = Math.min(page * PAGE_SIZE, total);
  const filtered = Boolean(q || type || status || category);

  return (
    <div className="space-y-5">
      <header>
        <h1 className="page-title">Browse reports</h1>
        <p className="page-subtitle">Search by keywords (every word must appear) and filter by type, category and status.</p>
      </header>

      <div className="card space-y-3 p-4">
        <form role="search" onSubmit={onSearch} className="flex gap-2">
          <label htmlFor="browse-search" className="sr-only">Search reports</label>
          <div className="relative flex-1">
            <Search className="pointer-events-none absolute left-3 top-2.5 h-4 w-4 text-slate-400" aria-hidden="true" />
            <input
              id="browse-search"
              type="search"
              className="input pl-9"
              placeholder="e.g. black leather wallet, or R000123"
              value={draft}
              maxLength={100}
              onChange={(e) => setDraft(e.target.value)}
            />
          </div>
          <button type="submit" className="btn-primary">Search</button>
        </form>
        <div className="grid gap-3 sm:grid-cols-4">
          <Select id="filter-type" label="Type" value={type} onChange={(v) => update({ type: v })}
            options={[["", "All types"], ["lost", "Lost"], ["found", "Found"]]} />
          <Select id="filter-category" label="Category" value={category} onChange={(v) => update({ category: v })}
            options={[["", "All categories"], ...categories.map((c): [string, string] => [c.name, `${c.name} (${c.count})`])]} />
          <Select id="filter-status" label="Status" value={status} onChange={(v) => update({ status: v })}
            options={[["", "All statuses"], ["open", "Open"], ["matched", "Matched"], ["closed", "Closed"]]} />
          <div className="flex items-end">
            <button type="button" className="btn-secondary w-full" disabled={!filtered} onClick={() => setParams(new URLSearchParams())}>
              <X className="h-4 w-4" aria-hidden="true" />
              Clear filters
            </button>
          </div>
        </div>
      </div>

      {error != null ? (
        <ErrorAlert error={error} onRetry={load} />
      ) : loading && !result ? (
        <Loading label="Loading reports…" />
      ) : result && result.items.length === 0 ? (
        <EmptyState title="No reports match">
          {filtered ? "Try fewer keywords or clear the filters." : "There are no reports yet."}
        </EmptyState>
      ) : result ? (
        <>
          <p className="text-sm text-slate-600" aria-live="polite">
            Showing {first.toLocaleString("en-US")}–{last.toLocaleString("en-US")} of {total.toLocaleString("en-US")} reports
            {loading && " · updating…"}
          </p>
          <div className="grid gap-3 md:grid-cols-2 xl:grid-cols-3">
            {result.items.map((report) => (
              <ReportCard key={report.id} report={report} />
            ))}
          </div>
          <nav aria-label="Pagination" className="flex items-center justify-between">
            <button type="button" className="btn-secondary" disabled={page <= 1 || loading} onClick={() => update({ page: String(page - 1) })}>
              <ChevronLeft className="h-4 w-4" aria-hidden="true" />
              Previous
            </button>
            <span className="text-sm text-slate-600">Page {page} of {pages.toLocaleString("en-US")}</span>
            <button type="button" className="btn-secondary" disabled={page >= pages || loading} onClick={() => update({ page: String(page + 1) })}>
              Next
              <ChevronRight className="h-4 w-4" aria-hidden="true" />
            </button>
          </nav>
        </>
      ) : null}
    </div>
  );
}

function Select({ id, label, value, options, onChange }: {
  id: string;
  label: string;
  value: string;
  options: Array<[string, string]>;
  onChange: (value: string) => void;
}) {
  return (
    <div>
      <label htmlFor={id} className="label">{label}</label>
      <select id={id} className="input" value={value} onChange={(e) => onChange(e.target.value)}>
        {options.map(([v, text]) => (
          <option key={v} value={v}>{text}</option>
        ))}
      </select>
    </div>
  );
}
