import { Link, Route, Routes } from "react-router-dom";
import { Layout } from "./components/Layout";
import { EmptyState } from "./components/States";
import { BrowsePage } from "./pages/BrowsePage";
import { DashboardPage } from "./pages/DashboardPage";
import { MatchesPage } from "./pages/MatchesPage";
import { ReportPage } from "./pages/ReportPage";

export function App() {
  return (
    <Routes>
      <Route element={<Layout />}>
        <Route index element={<DashboardPage />} />
        <Route path="report" element={<ReportPage />} />
        <Route path="browse" element={<BrowsePage />} />
        <Route path="matches" element={<MatchesPage />} />
        <Route
          path="*"
          element={
            <EmptyState title="Page not found">
              <Link className="underline" to="/">Go to the dashboard</Link>
            </EmptyState>
          }
        />
      </Route>
    </Routes>
  );
}
