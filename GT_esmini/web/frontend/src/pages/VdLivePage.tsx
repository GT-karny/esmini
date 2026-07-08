import { useParams, useSearchParams } from 'react-router-dom';
import { LiveVdPanel } from '../components/verification/LiveVdPanel';

/**
 * Standalone (layout-less) live VirtualDriver telemetry view, intended to be
 * opened in a separate window via window.open('/live/vd/:jobId'). Renders only
 * the LiveVdPanel full-screen so it can sit side-by-side with the main app.
 * Pass ?override=1 to include the manual-override controls.
 */
export function VdLivePage() {
  const { jobId } = useParams<{ jobId: string }>();
  const [params] = useSearchParams();
  const showOverride = params.get('override') === '1';
  const projectId = params.get('project') ?? undefined;
  const scenarioFile = params.get('scenario') ?? undefined;

  return (
    <div className="h-screen w-screen p-3 bg-background">
      <LiveVdPanel
        jobId={jobId ?? null}
        projectId={projectId}
        scenarioFile={scenarioFile}
        showOverride={showOverride}
      />
    </div>
  );
}
