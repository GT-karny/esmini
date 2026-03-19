import { useState, useRef, useCallback, useEffect, type PointerEvent as ReactPointerEvent } from 'react';
import { useParams, useSearchParams } from 'react-router-dom';
import { useQuery } from '@tanstack/react-query';
import {
  api,
  type ScenarioInfo,
} from '../api/client';
import { TableSkeleton } from '../components/ui/Table';
import { EmptyState } from '../components/ui/EmptyState';
import { ErrorPanel } from '../components/ui/ErrorPanel';
import { ScenarioListPanel } from '../components/project/ScenarioListPanel';
import { ScenarioDetailPanel } from '../components/project/ScenarioDetailPanel';
import { LiveMonitorPanel } from '../components/project/LiveMonitorPanel';
import { ParameterPanel } from '../components/project/ParameterPanel';
import { ExecutionPanel } from '../components/project/ExecutionPanel';
import { FilesTab } from '../components/project/FilesTab';
import { useJobPolling } from '../hooks/useJobPolling';

type Tab = 'scenarios' | 'files';

export function ProjectDetailPage() {
  const { projectId } = useParams<{ projectId: string }>();
  const [searchParams, setSearchParams] = useSearchParams();
  const tab = (searchParams.get('tab') ?? 'scenarios') as Tab;
  const [paramOverrides, setParamOverrides] = useState<Record<string, string>>({});

  const {
    runningJobId,
    setRunningJobId,
    latestJobId,
    isJobActive,
    handleRunning,
  } = useJobPolling();

  // Selected scenario from URL
  const selectedScenarioFile = searchParams.get('scenario');

  const { data: project, isLoading, error, refetch } = useQuery({
    queryKey: ['project', projectId],
    queryFn: () => api.getProject(projectId!),
    enabled: !!projectId,
  });

  const { data: scenarios } = useQuery({
    queryKey: ['project-scenarios', projectId],
    queryFn: () => api.getProjectScenarios(projectId!),
    enabled: !!projectId,
  });

  const selectedScenario = scenarios?.find((s) => s.file === selectedScenarioFile) ?? null;

  // Auto-select first scenario when none selected
  useEffect(() => {
    if (scenarios && scenarios.length > 0 && !selectedScenarioFile) {
      setSearchParams((prev) => {
        const next = new URLSearchParams(prev);
        next.set('scenario', scenarios[0].file);
        return next;
      }, { replace: true });
    }
  }, [scenarios, selectedScenarioFile, setSearchParams]);

  const handleSelectScenario = (file: string) => {
    setSearchParams((prev) => {
      const next = new URLSearchParams(prev);
      next.set('scenario', file);
      return next;
    });
    setRunningJobId(null);
  };

  if (isLoading) {
    return (
      <div>
        <div className="h-6 bg-glass-1 animate-pulse w-48 mb-6" />
        <TableSkeleton columns={4} rows={5} />
      </div>
    );
  }

  if (error) return <ErrorPanel error={error} onRetry={() => refetch()} />;
  if (!project) return null;

  return (
    <div className="h-full flex flex-col overflow-hidden px-6 pt-1">
      {project.description && (
        <p className="text-text-secondary text-xs mb-1 shrink-0 truncate" title={project.description}>
          {project.description}
        </p>
      )}

      {/* Tab content fills remaining space */}
      <div className="flex-1 min-h-0">
        {tab === 'scenarios' && scenarios ? (
          <ScenarioDashboard
            projectId={projectId!}
            scenarios={scenarios}
            selectedScenario={selectedScenario}
            selectedFile={selectedScenarioFile}
            onSelectScenario={handleSelectScenario}
            runningJobId={runningJobId}
            latestJobId={latestJobId}
            paramOverrides={paramOverrides}
            onParamOverridesChange={setParamOverrides}
            onRunning={handleRunning}
            activeJobId={isJobActive ? runningJobId : null}
          />
        ) : tab === 'scenarios' ? (
          <TableSkeleton columns={4} rows={5} />
        ) : (
          <div className="h-full overflow-y-auto">
            <FilesTab projectId={projectId!} project={project} />
          </div>
        )}
      </div>
    </div>
  );
}

/* ================================================================
   Scenario Dashboard (4-panel grid)
   ================================================================ */

interface ScenarioDashboardProps {
  projectId: string;
  scenarios: ScenarioInfo[];
  selectedScenario: ScenarioInfo | null;
  selectedFile: string | null;
  onSelectScenario: (file: string) => void;
  runningJobId: string | null;
  latestJobId: string | null;
  paramOverrides: Record<string, string>;
  onParamOverridesChange: (overrides: Record<string, string>) => void;
  onRunning: (jobId: string) => void;
  activeJobId: string | null;
}

function ScenarioDashboard({
  projectId,
  scenarios,
  selectedScenario,
  selectedFile,
  onSelectScenario,
  runningJobId,
  latestJobId,
  paramOverrides,
  onParamOverridesChange,
  onRunning,
  activeJobId,
}: ScenarioDashboardProps) {
  const containerRef = useRef<HTMLDivElement>(null);
  const [splitX, setSplitX] = useState(0.5);
  const [splitY, setSplitY] = useState(0.5);
  const dragAxis = useRef<'x' | 'y' | null>(null);

  const onHandleDown = useCallback((axis: 'x' | 'y') => (e: ReactPointerEvent) => {
    e.preventDefault();
    dragAxis.current = axis;
    (e.target as HTMLElement).setPointerCapture(e.pointerId);
  }, []);

  const handlePointerMove = useCallback((e: ReactPointerEvent) => {
    if (!dragAxis.current || !containerRef.current) return;
    const rect = containerRef.current.getBoundingClientRect();
    if (dragAxis.current === 'x') {
      setSplitX(Math.max(0.15, Math.min(0.75, (e.clientX - rect.left) / rect.width)));
    } else {
      setSplitY(Math.max(0.2, Math.min(0.8, (e.clientY - rect.top) / rect.height)));
    }
  }, []);

  const handlePointerUp = useCallback(() => {
    dragAxis.current = null;
  }, []);

  if (scenarios.length === 0) {
    return <EmptyState message="No scenarios in this project." />;
  }

  const handleCls = 'glass border border-glass-edge backdrop-blur-sm select-none transition-colors';

  return (
    <div
      ref={containerRef}
      className="hidden md:grid h-full"
      style={{
        gridTemplateColumns: `${splitX * 100}% 5px 1fr`,
        gridTemplateRows: `${splitY * 100}% 5px 1fr`,
      }}
      onPointerMove={handlePointerMove}
      onPointerUp={handlePointerUp}
    >
      {/* Top-left: Scenario list */}
      <div className="overflow-hidden" style={{ gridArea: '1/1/2/2' }}>
        <ScenarioListPanel
          scenarios={scenarios}
          selectedFile={selectedFile}
          onSelect={onSelectScenario}
        />
      </div>

      {/* Vertical glass handle (spans all rows) */}
      <div
        className={`${handleCls} cursor-col-resize flex items-center justify-center hover:border-primary/40`}
        style={{ gridArea: '1/2/4/3' }}
        onPointerDown={onHandleDown('x')}
      >
        <div className="w-px h-10 bg-text-tertiary/30" />
      </div>

      {/* Top-right: Parameters */}
      <div className="overflow-hidden" style={{ gridArea: '1/3/2/4' }}>
        <ParameterPanel
          projectId={projectId}
          scenario={selectedScenario}
          paramOverrides={paramOverrides}
          onParamOverridesChange={onParamOverridesChange}
        />
      </div>

      {/* Horizontal glass handle — left half */}
      <div
        className={`${handleCls} cursor-row-resize flex items-center justify-center hover:border-primary/40`}
        style={{ gridArea: '2/1/3/2' }}
        onPointerDown={onHandleDown('y')}
      >
        <div className="h-px w-10 bg-text-tertiary/30" />
      </div>

      {/* Horizontal glass handle — right half */}
      <div
        className={`${handleCls} cursor-row-resize flex items-center justify-center hover:border-primary/40`}
        style={{ gridArea: '2/3/3/4' }}
        onPointerDown={onHandleDown('y')}
      >
        <div className="h-px w-10 bg-text-tertiary/30" />
      </div>

      {/* Bottom-left: Scenario detail / Live monitor */}
      <div className="overflow-hidden" style={{ gridArea: '3/1/4/2' }}>
        {runningJobId ? (
          <LiveMonitorPanel jobId={runningJobId} projectId={projectId} scenarioFile={selectedFile ?? undefined} />
        ) : selectedScenario ? (
          <ScenarioDetailPanel
            projectId={projectId}
            scenario={selectedScenario}
          />
        ) : (
          <div className="h-full flex items-center justify-center">
            <EmptyState message="Select a scenario to view details" />
          </div>
        )}
      </div>

      {/* Bottom-right: Execution panel */}
      <div className="overflow-hidden" style={{ gridArea: '3/3/4/4' }}>
        <ExecutionPanel
          projectId={projectId}
          scenario={selectedScenario}
          paramOverrides={paramOverrides}
          onRunning={onRunning}
          latestJobId={latestJobId}
          activeJobId={activeJobId}
        />
      </div>
    </div>
  );
}
