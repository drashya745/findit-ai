import { CalendarDays, MapPin, Palette, Search, Tag } from "lucide-react";
import type { ReactNode } from "react";
import { Link } from "react-router-dom";
import type { Report } from "../types/api";
import { StatusBadge, TypeBadge } from "./Badges";

interface Props {
  report: Report;
  /** Extra content under the details (e.g. match scores). */
  children?: ReactNode;
  /** Hide the "Find matches" link (e.g. on the matches page itself). */
  hideMatchLink?: boolean;
}

export function ReportCard({ report, children, hideMatchLink }: Props) {
  const canMatch = report.type === "lost" && report.status === "open" && !hideMatchLink;
  return (
    <article className="card flex flex-col p-4" aria-label={`Report ${report.id}`}>
      <div className="flex flex-wrap items-center gap-2">
        <span className="font-mono text-sm font-semibold text-slate-900">{report.id}</span>
        <TypeBadge type={report.type} />
        <StatusBadge status={report.status} />
        {report.origin === "app" && (
          <span className="text-xs text-slate-500" title="Created in the FindIt app (not part of the imported dataset)">
            · created in app
          </span>
        )}
      </div>
      <p className="mt-2 text-sm text-slate-800">{report.description}</p>
      <dl className="mt-3 grid grid-cols-1 gap-x-4 gap-y-1 text-xs text-slate-600 sm:grid-cols-2">
        <Detail icon={<Tag className="h-3.5 w-3.5" />} label="Category" value={report.category} />
        <Detail icon={<Palette className="h-3.5 w-3.5" />} label="Colour" value={report.color} />
        <Detail icon={<MapPin className="h-3.5 w-3.5" />} label="Location" value={report.location} />
        <Detail
          icon={<CalendarDays className="h-3.5 w-3.5" />}
          label={report.type === "lost" ? "Lost on" : "Found on"}
          value={report.date}
        />
        {report.brand && <Detail label="Brand" value={report.brand} />}
        {report.heldAt && <Detail label="Held at" value={report.heldAt} />}
      </dl>
      {children}
      {canMatch && (
        <div className="mt-3">
          <Link to={`/matches?lost=${encodeURIComponent(report.id)}`} className="btn-secondary px-2.5 py-1.5 text-xs">
            <Search className="h-3.5 w-3.5" aria-hidden="true" />
            Find matches
          </Link>
        </div>
      )}
    </article>
  );
}

function Detail({ icon, label, value }: { icon?: ReactNode; label: string; value: string }) {
  return (
    <div className="flex items-center gap-1.5">
      {icon && <span className="text-slate-400" aria-hidden="true">{icon}</span>}
      <dt className="sr-only">{label}</dt>
      <dd>
        <span className="text-slate-500">{label}: </span>
        {value}
      </dd>
    </div>
  );
}
