import { ErrorBoundary } from "./components/ErrorBoundary";
import { Shell } from "./components/Shell";
import { AnomaliesPage } from "./components/anomaly/AnomaliesPage";
import { InsightsPage } from "./components/insights/InsightsPage";
import { ReplayPage } from "./components/replay/ReplayPage";
import { ResultsPage } from "./components/results/ResultsPage";
import { SetupPage } from "./components/setup/SetupPage";
import { TopologyPage } from "./components/topology/TopologyPage";
import { WorkbenchProvider, useWorkbench } from "./state/workbench";

const PAGES = {
  topology: TopologyPage,
  setup: SetupPage,
  results: ResultsPage,
  anomalies: AnomaliesPage,
  replay: ReplayPage,
  insights: InsightsPage,
};

function CurrentPage() {
  const { section } = useWorkbench();
  const Page = PAGES[section];
  return (
    <ErrorBoundary key={section}>
      <Page />
    </ErrorBoundary>
  );
}

export default function App() {
  return (
    <WorkbenchProvider>
      <Shell>
        <CurrentPage />
      </Shell>
    </WorkbenchProvider>
  );
}
