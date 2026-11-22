import { ErrorBoundary } from "./components/ErrorBoundary";
import { Shell } from "./components/Shell";
import { AnomaliesPage } from "./components/anomaly/AnomaliesPage";
import { InsightsPage } from "./components/insights/InsightsPage";
import { ReplayPage } from "./components/replay/ReplayPage";
import { ResultsPage } from "./components/results/ResultsPage";
import { SetupPage } from "./components/setup/SetupPage";
import { TopologyPage } from "./components/topology/TopologyPage";
import { WorkbenchProvider, useWorkbench } from "./state/workbench";

function CurrentPage() {
  const { section } = useWorkbench();
  switch (section) {
    case "topology":
      return <TopologyPage />;
    case "setup":
      return <SetupPage />;
    case "results":
      return <ResultsPage />;
    case "anomalies":
      return <AnomaliesPage />;
    case "replay":
      return <ReplayPage />;
    case "insights":
      return <InsightsPage />;
  }
}

function SectionBoundary() {
  const { section } = useWorkbench();
  return (
    <ErrorBoundary key={section}>
      <CurrentPage />
    </ErrorBoundary>
  );
}

export default function App() {
  return (
    <WorkbenchProvider>
      <Shell>
        <SectionBoundary />
      </Shell>
    </WorkbenchProvider>
  );
}
