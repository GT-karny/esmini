import { useState, useRef, useCallback, useEffect, type PointerEvent as ReactPointerEvent } from 'react';
import { useParams, useSearchParams } from 'react-router-dom';
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import {
  api,
  type ProjectDetail,
  type ProjectFile,
  type ScenarioInfo,
} from '../api/client';
import { Button } from '../components/ui/Button';
import { TableShell, TableSkeleton } from '../components/ui/Table';
import { EmptyState } from '../components/ui/EmptyState';
import { ErrorPanel } from '../components/ui/ErrorPanel';
import { ConfirmDialog } from '../components/ui/ConfirmDialog';
import { ScenarioListPanel } from '../components/project/ScenarioListPanel';
import { ScenarioDetailPanel } from '../components/project/ScenarioDetailPanel';
import { LiveMonitorPanel } from '../components/project/LiveMonitorPanel';
import { ParameterPanel } from '../components/project/ParameterPanel';
import { ExecutionPanel } from '../components/project/ExecutionPanel';

type Tab = 'scenarios' | 'files';

export function ProjectDetailPage() {
  const { projectId } = useParams<{ projectId: string }>();
  const [searchParams, setSearchParams] = useSearchParams();
  const tab = (searchParams.get('tab') ?? 'scenarios') as Tab;
  const [runningJobId, setRunningJobId] = useState<string | null>(null);
  const [latestJobId, setLatestJobId] = useState<string | null>(null);
  const [paramOverrides, setParamOverrides] = useState<Record<string, string>>({});

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

  // Poll running job status for auto-switch back
  const { data: runningJobStatus } = useQuery({
    queryKey: ['simulation', runningJobId],
    queryFn: () => api.getSimulation(runningJobId!),
    enabled: !!runningJobId,
    refetchInterval: 1000,
  });

  // Auto-switch: when running job completes, clear after 2s
  useEffect(() => {
    if (!runningJobStatus) return;
    const s = runningJobStatus.status;
    if (s === 'completed' || s === 'failed' || s === 'cancelled' || s === 'timeout') {
      const timer = globalThis.setTimeout(() => {
        setRunningJobId(null);
      }, 2000);
      return () => globalThis.clearTimeout(timer);
    }
  }, [runningJobStatus]);

  // runningJobId is non-null while job is active AND for 2s after completion
  // (cleared by the timer above), so we use it directly for panel switching.
  // activeJobId is only non-null when the job is actually running/queued (for Stop button).
  const isJobActive = !!runningJobId && !!runningJobStatus &&
    (runningJobStatus.status === 'running' || runningJobStatus.status === 'queued');

  const handleSelectScenario = (file: string) => {
    setSearchParams((prev) => {
      const next = new URLSearchParams(prev);
      next.set('scenario', file);
      return next;
    });
    setRunningJobId(null);
  };

  const handleRunning = (jobId: string) => {
    setRunningJobId(jobId);
    setLatestJobId(jobId);
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
          <LiveMonitorPanel jobId={runningJobId} />
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

/* ================================================================
   Files Tab
   ================================================================ */

function FilesTab({ projectId, project }: { projectId: string; project: ProjectDetail }) {
  const queryClient = useQueryClient();
  const fileInputRef = useRef<HTMLInputElement>(null);
  const [deleteTarget, setDeleteTarget] = useState<string | null>(null);

  const { data: files, isLoading, error, refetch } = useQuery({
    queryKey: ['project-files', projectId],
    queryFn: () => api.getProjectFiles(projectId),
  });

  const uploadMut = useMutation({
    mutationFn: (file: File) => api.uploadProjectFile(projectId, file),
    onSuccess: () => queryClient.invalidateQueries({ queryKey: ['project-files', projectId] }),
  });

  const deleteMut = useMutation({
    mutationFn: (path: string) => api.deleteProjectFile(projectId, path),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['project-files', projectId] });
      setDeleteTarget(null);
    },
  });

  const handleDrop = useCallback((e: React.DragEvent) => {
    e.preventDefault();
    if (project.is_builtin) return;
    const dropped = e.dataTransfer.files;
    for (let i = 0; i < dropped.length; i++) {
      uploadMut.mutate(dropped[i]);
    }
  }, [project.is_builtin, uploadMut]);

  if (isLoading) return <TableSkeleton columns={4} rows={6} />;
  if (error) return <ErrorPanel error={error} onRetry={() => refetch()} />;

  const fileColumns = [
    { key: 'name', label: 'Name' },
    { key: 'type', label: 'Type' },
    { key: 'size', label: 'Size', align: 'right' as const },
    { key: 'actions', label: '', align: 'right' as const },
  ];

  return (
    <div
      onDragOver={(e) => { if (!project.is_builtin) e.preventDefault(); }}
      onDrop={handleDrop}
    >
      {/* Upload bar */}
      {!project.is_builtin && (
        <div className="flex items-center justify-between mb-4">
          <p className="text-text-secondary text-xs">
            Drag &amp; drop files here or use the upload button
          </p>
          <div>
            <input
              ref={fileInputRef}
              type="file"
              multiple
              className="hidden"
              onChange={(e) => {
                const list = e.target.files;
                if (list) {
                  for (let i = 0; i < list.length; i++) uploadMut.mutate(list[i]);
                }
                e.target.value = '';
              }}
            />
            <Button
              variant="secondary"
              size="sm"
              onClick={() => fileInputRef.current?.click()}
              disabled={uploadMut.isPending}
            >
              {uploadMut.isPending ? 'Uploading...' : 'Upload Files'}
            </Button>
          </div>
        </div>
      )}

      {uploadMut.error && (
        <p className="text-destructive text-xs mb-3">
          Upload failed: {uploadMut.error instanceof Error ? uploadMut.error.message : 'Unknown error'}
        </p>
      )}

      {!files || files.length === 0 ? (
        <EmptyState message="No files in this project." />
      ) : (
        <TableShell columns={fileColumns}>
          {files.map((f: ProjectFile) => (
            <tr key={f.path} className="border-b border-glass-edge/50 hover:bg-glass-hover/30">
              <td className="px-4 py-3">
                <span className="text-text-tertiary mr-2">{f.is_dir ? '\uD83D\uDCC1' : '\uD83D\uDCC4'}</span>
                <span className="font-mono text-sm">{f.name}</span>
              </td>
              <td className="px-4 py-3 text-text-secondary text-xs uppercase">{f.type}</td>
              <td className="px-4 py-3 text-right text-text-secondary font-mono text-xs">
                {f.is_dir ? '-' : formatFileSize(f.size)}
              </td>
              <td className="px-4 py-3 text-right">
                <div className="flex justify-end gap-1">
                  {!f.is_dir && (
                    <a
                      href={api.downloadProjectFile(projectId, f.path)}
                      className="text-text-secondary hover:text-foreground text-xs px-2 py-1 transition-colors"
                      download
                    >
                      &#8595;
                    </a>
                  )}
                  {!project.is_builtin && !f.is_dir && (
                    <button
                      onClick={() => setDeleteTarget(f.path)}
                      className="text-text-secondary hover:text-destructive text-xs px-2 py-1 transition-colors cursor-pointer"
                    >
                      &#10005;
                    </button>
                  )}
                </div>
              </td>
            </tr>
          ))}
        </TableShell>
      )}

      <ConfirmDialog
        open={!!deleteTarget}
        title="Delete File"
        message={`Delete "${deleteTarget}"? This cannot be undone.`}
        confirmLabel="Delete"
        variant="danger"
        onConfirm={() => deleteTarget && deleteMut.mutate(deleteTarget)}
        onCancel={() => setDeleteTarget(null)}
      />
    </div>
  );
}

/* ================================================================
   Helpers
   ================================================================ */

function formatFileSize(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
}
