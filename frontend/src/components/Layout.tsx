import { FilePlus2, LayoutDashboard, ListFilter, PackageSearch, Search } from "lucide-react";
import { useEffect, useState } from "react";
import { NavLink, Outlet } from "react-router-dom";
import { api } from "../services/api";
import type { Health } from "../types/api";

const navItems = [
  { to: "/", label: "Dashboard", icon: LayoutDashboard, end: true },
  { to: "/report", label: "Report an item", icon: FilePlus2, end: false },
  { to: "/browse", label: "Browse reports", icon: ListFilter, end: false },
  { to: "/matches", label: "Find matches", icon: Search, end: false },
];

const linkClass = ({ isActive }: { isActive: boolean }) =>
  `flex items-center gap-2.5 rounded-md px-3 py-2 text-sm font-medium transition-colors focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-blue-600 ${
    isActive ? "bg-blue-50 text-blue-700" : "text-slate-700 hover:bg-slate-100"
  }`;

export function SyntheticBanner() {
  return (
    <div className="border-b border-amber-200 bg-amber-50 px-4 py-1.5 text-center text-xs text-amber-900" role="note">
      <strong>Dataset: synthetic sample data.</strong> Imported reports are generated examples, not real campus
      incidents.
    </div>
  );
}

/** Small status line about the backend; failures are shown, never hidden. */
function BackendStatus() {
  const [health, setHealth] = useState<Health | null>(null);
  const [failed, setFailed] = useState(false);
  useEffect(() => {
    let active = true;
    api.health().then(
      (h) => active && setHealth(h),
      () => active && setFailed(true),
    );
    return () => {
      active = false;
    };
  }, []);
  if (failed) return <p className="text-xs text-red-700">API not reachable</p>;
  if (!health) return <p className="text-xs text-slate-500">Checking API…</p>;
  return (
    <p className="text-xs text-slate-500">
      Matcher: {health.matcher.strategy}
      {health.matcher.fallbackActive && " (fallback)"}
      {health.sampleData && " · sample mode, not saved"}
    </p>
  );
}

export function Layout() {
  return (
    <div className="min-h-screen">
      <a href="#main" className="sr-only focus:not-sr-only focus:absolute focus:left-2 focus:top-2 focus:z-50 focus:rounded focus:bg-white focus:px-3 focus:py-2">
        Skip to content
      </a>
      <SyntheticBanner />
      <div className="mx-auto flex max-w-7xl flex-col md:flex-row">
        <aside className="border-b border-slate-200 bg-white md:min-h-[calc(100vh-29px)] md:w-60 md:flex-none md:border-b-0 md:border-r">
          <div className="flex items-center gap-2 px-4 py-4">
            <PackageSearch className="h-6 w-6 text-blue-600" aria-hidden="true" />
            <div>
              <p className="text-base font-semibold leading-tight text-slate-900">FindIt AI</p>
              <p className="text-xs text-slate-500">Campus lost &amp; found</p>
            </div>
          </div>
          <nav aria-label="Main" className="flex gap-1 overflow-x-auto px-2 pb-3 md:flex-col md:overflow-visible">
            {navItems.map(({ to, label, icon: Icon, end }) => (
              <NavLink key={to} to={to} end={end} className={linkClass}>
                <Icon className="h-4 w-4 flex-none" aria-hidden="true" />
                <span className="whitespace-nowrap">{label}</span>
              </NavLink>
            ))}
          </nav>
          <div className="hidden px-4 pb-4 md:block">
            <BackendStatus />
          </div>
        </aside>
        <main id="main" className="min-w-0 flex-1 px-4 py-6 md:px-8 md:py-8">
          <Outlet />
        </main>
      </div>
    </div>
  );
}
