import { AlertCircle, CheckCircle2, Inbox, Loader2, RefreshCw, WifiOff } from "lucide-react";
import type { ReactNode } from "react";
import { ApiError } from "../services/api";

export function Loading({ label = "Loading…" }: { label?: string }) {
  return (
    <div role="status" className="flex items-center gap-2 py-8 text-sm text-slate-600">
      <Loader2 className="h-4 w-4 animate-spin" aria-hidden="true" />
      {label}
    </div>
  );
}

export function EmptyState({ title, children }: { title: string; children?: ReactNode }) {
  return (
    <div className="card flex flex-col items-center px-6 py-10 text-center">
      <Inbox className="h-8 w-8 text-slate-400" aria-hidden="true" />
      <p className="mt-3 font-medium text-slate-800">{title}</p>
      {children && <div className="mt-1 max-w-md text-sm text-slate-600">{children}</div>}
    </div>
  );
}

/** Shows an API (or other) error. Connection problems get their own wording and icon. */
export function ErrorAlert({ error, onRetry, title }: { error: unknown; onRetry?: () => void; title?: string }) {
  const unavailable = error instanceof ApiError && error.isUnavailable;
  const message = error instanceof Error ? error.message : "Something went wrong.";
  const Icon = unavailable ? WifiOff : AlertCircle;
  return (
    <div role="alert" className="flex items-start gap-3 rounded-md border border-red-200 bg-red-50 p-4 text-sm text-red-800">
      <Icon className="mt-0.5 h-4 w-4 flex-none" aria-hidden="true" />
      <div className="flex-1">
        <p className="font-medium">{title ?? (unavailable ? "Backend unavailable" : "Request failed")}</p>
        <p className="mt-0.5">{message}</p>
      </div>
      {onRetry && (
        <button type="button" className="btn-secondary px-2.5 py-1.5" onClick={onRetry}>
          <RefreshCw className="h-3.5 w-3.5" aria-hidden="true" />
          Retry
        </button>
      )}
    </div>
  );
}

export function SuccessAlert({ title, children }: { title: string; children?: ReactNode }) {
  return (
    <div role="status" className="flex items-start gap-3 rounded-md border border-emerald-200 bg-emerald-50 p-4 text-sm text-emerald-900">
      <CheckCircle2 className="mt-0.5 h-4 w-4 flex-none" aria-hidden="true" />
      <div>
        <p className="font-medium">{title}</p>
        {children && <div className="mt-0.5">{children}</div>}
      </div>
    </div>
  );
}
