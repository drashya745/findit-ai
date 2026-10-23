import type { ReportStatus, ReportType } from "../types/api";

const base = "inline-flex items-center rounded-full px-2 py-0.5 text-xs font-medium ring-1 ring-inset";

export function TypeBadge({ type }: { type: ReportType }) {
  return type === "lost" ? (
    <span className={`${base} bg-amber-50 text-amber-800 ring-amber-200`}>Lost</span>
  ) : (
    <span className={`${base} bg-sky-50 text-sky-800 ring-sky-200`}>Found</span>
  );
}

const statusStyles: Record<ReportStatus, string> = {
  open: "bg-slate-50 text-slate-700 ring-slate-200",
  matched: "bg-emerald-50 text-emerald-800 ring-emerald-200",
  closed: "bg-slate-100 text-slate-500 ring-slate-200",
};

export function StatusBadge({ status }: { status: ReportStatus }) {
  return <span className={`${base} ${statusStyles[status]}`}>{status[0]!.toUpperCase() + status.slice(1)}</span>;
}
