import { CheckCheck, Info, Search } from "lucide-react";
import { useCallback, useEffect, useState, type FormEvent } from "react";
import { Link, useSearchParams } from "react-router-dom";
import { ConfirmDialog } from "../components/ConfirmDialog";
import { ReportCard } from "../components/ReportCard";
import { EmptyState, ErrorAlert, Loading, SuccessAlert } from "../components/States";
import { ApiError, api } from "../services/api";
import type { ConfirmResponse, MatchCandidate, MatchResponse } from "../types/api";

export const SCORE_LABEL = "Match score — relative ranking, not a probability.";
const TOP_K = 3;

const componentLabels: Array<[string, string]> = [
  ["text", "Description (TF-IDF)"],
  ["location", "Location"],
  ["category", "Category"],
  ["colour", "Colour"],
];

export function MatchesPage() {
  const [params, setParams] = useSearchParams();
  const lostId = (params.get("lost") ?? "").trim().toUpperCase();
  const [draft, setDraft] = useState(lostId);
  const [data, setData] = useState<MatchResponse | null>(null);
  const [loadError, setLoadError] = useState<unknown>(null);
  const [loading, setLoading] = useState(false);
  const [pending, setPending] = useState<MatchCandidate | null>(null); // Awaiting the user's "yes".
  const [saving, setSaving] = useState(false);
  const [confirmed, setConfirmed] = useState<ConfirmResponse | null>(null);
  const [confirmError, setConfirmError] = useState<unknown>(null);

  useEffect(() => setDraft(lostId), [lostId]);

  const load = useCallback(() => {
    setData(null);
    setLoadError(null);
    setConfirmed(null);
    setConfirmError(null);
    if (!lostId) return;
    let active = true;
    setLoading(true);
    api.findMatches(lostId, TOP_K).then(
      (response) => {
        if (!active) return;
        setData(response);
        setLoading(false);
      },
      (error: unknown) => {
        if (!active) return;
        setLoadError(error);
        setLoading(false);
      },
    );
    return () => {
      active = false;
    };
  }, [lostId]);
  useEffect(load, [load]);

  const onSearch = (event: FormEvent) => {
    event.preventDefault();
    const id = draft.trim().toUpperCase();
    setParams(id ? { lost: id } : {});
  };

  const confirm = async () => {
    if (!pending || !data || saving) return;
    setSaving(true);
    setConfirmError(null);
    try {
      const result = await api.confirmMatch(data.lost.id, pending.found.id);
      // Only now, after the server saved it, reflect the new statuses.
      setConfirmed(result);
      setData({
        ...data,
        lost: result.lost,
        matches: data.matches.map((m) => (m.found.id === result.found.id ? { ...m, found: result.found } : m)),
      });
    } catch (error) {
      setConfirmError(error);
    } finally {
      setSaving(false);
      setPending(null);
    }
  };

  const lostOpen = data?.lost.status === "open";

  return (
    <div className="space-y-6">
      <header>
        <h1 className="page-title">Find matches</h1>
        <p className="page-subtitle">Enter a lost report ID to see the top {TOP_K} open found reports ranked by the matching engine.</p>
      </header>

      <form role="search" onSubmit={onSearch} className="card flex flex-col gap-2 p-4 sm:flex-row sm:items-end">
        <div className="flex-1">
          <label htmlFor="lost-id" className="label">Lost report ID</label>
          <input
            id="lost-id"
            className="input font-mono"
            placeholder="R000001"
            value={draft}
            maxLength={20}
            autoComplete="off"
            onChange={(e) => setDraft(e.target.value)}
          />
        </div>
        <button type="submit" className="btn-primary" disabled={!draft.trim()}>
          <Search className="h-4 w-4" aria-hidden="true" />
          Find matches
        </button>
      </form>

      {!lostId && (
        <EmptyState title="No lost report selected">
          Enter an ID above, or use “Find matches” on a lost report in <Link className="underline" to="/browse?type=lost&status=open">Browse</Link>.
        </EmptyState>
      )}
      {loading && <Loading label="Ranking candidates… (the first request can take a few seconds while the matcher starts)" />}
      {loadError != null && <LoadErrorView error={loadError} lostId={lostId} onRetry={load} />}

      {data && (
        <div className="space-y-5">
          <section aria-labelledby="lost-heading" className="space-y-2">
            <h2 id="lost-heading" className="text-sm font-semibold uppercase tracking-wide text-slate-500">Lost report</h2>
            <ReportCard report={data.lost} hideMatchLink />
          </section>

          {confirmed && (
            <SuccessAlert title="Match confirmed">
              {confirmed.lost.id} and {confirmed.found.id} are now marked as matched
              {confirmed.persisted ? ` (saved ${confirmed.confirmedOn}).` : " (sample mode: not saved)."}
            </SuccessAlert>
          )}
          {confirmError != null && (
            <ErrorAlert
              error={confirmError}
              onRetry={load}
              title={confirmError instanceof ApiError && confirmError.status === 409 ? "Already matched — nothing was changed" : "Match not confirmed"}
            />
          )}

          <section aria-labelledby="candidates-heading" className="space-y-3">
            <div className="flex flex-wrap items-baseline justify-between gap-2">
              <h2 id="candidates-heading" className="text-lg font-semibold text-slate-900">Top candidates</h2>
              <p className="flex items-center gap-1 text-xs text-slate-600">
                <Info className="h-3.5 w-3.5" aria-hidden="true" />
                {SCORE_LABEL}
              </p>
            </div>
            <p className="text-xs text-slate-500">
              Matcher: {data.matcher.strategy}
              {data.matcher.fallbackActive && " — Python matcher unavailable, keyword fallback in use"}
              {data.matcher.categoryBlocking && " · only reports in the same category are compared"}
            </p>

            {data.matches.length === 0 ? (
              <EmptyState title="No candidates found">
                {data.emptyReason === "no_open_found_reports_in_category"
                  ? `There are no open found reports in the category “${data.lost.category}” yet.`
                  : "There are no open found reports to compare yet."}
              </EmptyState>
            ) : (
              <ol className="space-y-3">
                {data.matches.map((candidate) => (
                  <li key={candidate.found.id}>
                    <CandidateCard
                      candidate={candidate}
                      canConfirm={lostOpen && candidate.found.status === "open"}
                      busy={saving}
                      onConfirm={() => setPending(candidate)}
                    />
                  </li>
                ))}
              </ol>
            )}
          </section>
        </div>
      )}

      <ConfirmDialog
        open={pending !== null}
        title="Confirm this match?"
        confirmLabel="Confirm match"
        busy={saving}
        onCancel={() => !saving && setPending(null)}
        onConfirm={confirm}
      >
        {pending && data && (
          <p>
            Mark lost report <strong className="font-mono">{data.lost.id}</strong> and found report{" "}
            <strong className="font-mono">{pending.found.id}</strong> as the same item? Both will be set to
            <em> matched</em>; this cannot be undone in the app.
          </p>
        )}
      </ConfirmDialog>
    </div>
  );
}

function LoadErrorView({ error, lostId, onRetry }: { error: unknown; lostId: string; onRetry: () => void }) {
  if (error instanceof ApiError && error.code === "not_open") {
    return (
      <div role="status" className="flex items-start gap-3 rounded-md border border-emerald-200 bg-emerald-50 p-4 text-sm text-emerald-900">
        <CheckCheck className="mt-0.5 h-4 w-4 flex-none" aria-hidden="true" />
        <div>
          <p className="font-medium">{lostId} is already matched</p>
          <p className="mt-0.5">{error.message}</p>
        </div>
      </div>
    );
  }
  const title =
    error instanceof ApiError && error.code === "not_a_lost_report" ? "That is a found report"
    : error instanceof ApiError && (error.code === "not_found" || error.code === "invalid_id") ? "Report not found"
    : undefined;
  return <ErrorAlert error={error} onRetry={error instanceof ApiError && error.isUnavailable ? onRetry : undefined} title={title} />;
}

function CandidateCard({ candidate, canConfirm, busy, onConfirm }: {
  candidate: MatchCandidate;
  canConfirm: boolean;
  busy: boolean;
  onConfirm: () => void;
}) {
  return (
    <ReportCard report={candidate.found} hideMatchLink>
      <div className="mt-4 border-t border-slate-100 pt-3">
        <div className="flex flex-wrap items-center justify-between gap-2">
          <p className="text-sm text-slate-700">
            <span className="mr-2 rounded bg-slate-100 px-1.5 py-0.5 text-xs font-semibold text-slate-700">#{candidate.rank}</span>
            Match score{" "}
            <strong className="tabular-nums text-slate-900" title={SCORE_LABEL}>{candidate.score.toFixed(2)}</strong>
            <span className="text-xs text-slate-500"> (relative ranking, not a probability)</span>
          </p>
          {/* Only offer the action when it can succeed; disabled only while a save is in flight. */}
          {canConfirm ? (
            <button type="button" className="btn-success px-3 py-1.5" disabled={busy} onClick={onConfirm}>
              <CheckCheck className="h-4 w-4" aria-hidden="true" />
              Confirm match
            </button>
          ) : candidate.found.status === "matched" ? (
            <span className="text-xs font-medium text-emerald-700">Confirmed as matched</span>
          ) : null}
        </div>
        <dl className="mt-3 grid grid-cols-2 gap-x-4 gap-y-2 sm:grid-cols-4">
          {componentLabels
            .filter(([key]) => key in candidate.components)
            .map(([key, label]) => {
              const value = candidate.components[key] ?? 0;
              return (
                <div key={key}>
                  <dt className="text-xs text-slate-500">{label}</dt>
                  <dd className="flex items-center gap-2 text-sm tabular-nums text-slate-800">
                    {value.toFixed(2)}
                    <span className="h-1.5 flex-1 rounded-full bg-slate-100" aria-hidden="true">
                      <span className="block h-1.5 rounded-full bg-blue-500" style={{ width: `${Math.round(value * 100)}%` }} />
                    </span>
                  </dd>
                </div>
              );
            })}
        </dl>
        <ul className="mt-3 list-disc space-y-0.5 pl-5 text-xs text-slate-600" aria-label="Why this candidate">
          {candidate.explanations.map((reason) => (
            <li key={reason}>{reason}</li>
          ))}
        </ul>
      </div>
    </ReportCard>
  );
}
