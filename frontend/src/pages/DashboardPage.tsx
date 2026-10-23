import { ArrowRight, CheckCircle2, FilePlus2, Files, PackageOpen, Search, SearchX } from "lucide-react";
import { useCallback, useEffect, useState, type ComponentType } from "react";
import { Link } from "react-router-dom";
import { ReportCard } from "../components/ReportCard";
import { EmptyState, ErrorAlert, Loading } from "../components/States";
import { api } from "../services/api";
import type { Report, Stats } from "../types/api";

const RECENT_COUNT = 6;

export function DashboardPage() {
  const [stats, setStats] = useState<Stats | null>(null);
  const [recent, setRecent] = useState<Report[] | null>(null);
  const [error, setError] = useState<unknown>(null);

  const load = useCallback(() => {
    setError(null);
    setStats(null);
    setRecent(null);
    Promise.all([api.stats(), api.listReports({ sort: "recent", limit: RECENT_COUNT })]).then(
      ([s, list]) => {
        setStats(s);
        setRecent(list.items);
      },
      setError,
    );
  }, []);

  useEffect(load, [load]);

  return (
    <div className="space-y-8">
      <header>
        <h1 className="page-title">FindIt AI</h1>
        <p className="page-subtitle max-w-2xl">
          Intelligent campus lost &amp; found. Report lost or found items, browse reports, and get ranked match
          suggestions with plain-language explanations. A person confirms every match.
        </p>
      </header>

      {error ? (
        <ErrorAlert error={error} onRetry={load} />
      ) : (
        <section aria-label="Summary" className="grid grid-cols-2 gap-3 lg:grid-cols-4">
          <StatCard label="Total reports" value={stats?.totalReports} icon={Files} />
          <StatCard label="Lost reports" value={stats?.lostReports} icon={SearchX} />
          <StatCard label="Found reports" value={stats?.foundReports} icon={PackageOpen} />
          <StatCard label="Confirmed matches" value={stats?.confirmedMatches} icon={CheckCircle2} />
        </section>
      )}

      <section aria-label="Quick actions" className="grid gap-3 sm:grid-cols-3">
        <QuickLink to="/report" icon={FilePlus2} title="Report an item" text="Log a lost or found item." />
        <QuickLink to="/browse" icon={Files} title="Browse reports" text="Search and filter all reports." />
        <QuickLink to="/matches" icon={Search} title="Find matches" text="Rank candidates for a lost report." />
      </section>

      <section aria-labelledby="recent-heading">
        <div className="mb-3 flex items-baseline justify-between">
          <h2 id="recent-heading" className="text-lg font-semibold text-slate-900">
            Recent reports
          </h2>
          <span className="text-xs text-slate-500">Latest report dates first</span>
        </div>
        {error ? null : recent === null ? (
          <Loading label="Loading reports…" />
        ) : recent.length === 0 ? (
          <EmptyState title="No reports yet">Create one on the Report an item page.</EmptyState>
        ) : (
          <div className="grid gap-3 md:grid-cols-2">
            {recent.map((report) => (
              <ReportCard key={report.id} report={report} />
            ))}
          </div>
        )}
      </section>
    </div>
  );
}

function StatCard({ label, value, icon: Icon }: { label: string; value?: number; icon: ComponentType<{ className?: string }> }) {
  return (
    <div className="card p-4">
      <div className="flex items-center justify-between text-slate-500">
        <span className="text-sm">{label}</span>
        <Icon className="h-4 w-4" aria-hidden="true" />
      </div>
      <p className="mt-2 text-2xl font-semibold tabular-nums text-slate-900">
        {value === undefined ? <span className="text-slate-300">—</span> : value.toLocaleString("en-US")}
      </p>
    </div>
  );
}

function QuickLink({ to, icon: Icon, title, text }: { to: string; icon: ComponentType<{ className?: string }>; title: string; text: string }) {
  return (
    <Link
      to={to}
      className="card group flex items-center gap-3 p-4 transition-colors hover:border-blue-300 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-blue-600"
    >
      <Icon className="h-5 w-5 flex-none text-blue-600" aria-hidden="true" />
      <div className="flex-1">
        <p className="text-sm font-medium text-slate-900">{title}</p>
        <p className="text-xs text-slate-600">{text}</p>
      </div>
      <ArrowRight className="h-4 w-4 text-slate-400 group-hover:text-blue-600" aria-hidden="true" />
    </Link>
  );
}
