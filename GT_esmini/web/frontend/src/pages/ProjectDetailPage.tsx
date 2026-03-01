import { useState, useRef, useCallback } from 'react';
import { useParams, useNavigate, Link } from 'react-router-dom';
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { GlassPanel } from '@osce/theme-apex';
import {
  api,
  type ProjectDetail,
  type ProjectFile,
  type ScenarioInfo,
  type SimulationStatus,
} from '../api/client';
import { Button } from '../components/ui/Button';
import { StatusBadge } from '../components/ui/Badge';
import { TableShell, TableSkeleton } from '../components/ui/Table';
import { EmptyState } from '../components/ui/EmptyState';
import { ErrorPanel } from '../components/ui/ErrorPanel';
import { ConfirmDialog } from '../components/ui/ConfirmDialog';

type Tab = 'scenarios' | 'files' | 'simulations';

export function ProjectDetailPage() {
  const { projectId } = useParams<{ projectId: string }>();
  const [tab, setTab] = useState<Tab>('scenarios');

  const { data: project, isLoading, error, refetch } = useQuery({
    queryKey: ['project', projectId],
    queryFn: () => api.getProject(projectId!),
    enabled: !!projectId,
  });

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

  const tabs: { key: Tab; label: string }[] = [
    { key: 'scenarios', label: 'Scenarios' },
    { key: 'files', label: 'Files' },
    { key: 'simulations', label: 'Simulations' },
  ];

  return (
    <div>
      {/* Breadcrumb + Title */}
      <div className="flex items-center gap-2 mb-1 text-sm">
        <Link to="/" className="text-text-secondary hover:text-foreground transition-colors">
          Projects
        </Link>
        <span className="text-text-tertiary">/</span>
        <span className="text-foreground">{project.name}</span>
        {project.is_builtin && (
          <span className="text-[10px] uppercase tracking-wider text-text-tertiary border border-glass-edge px-1.5 py-0.5 ml-2">
            read-only
          </span>
        )}
      </div>
      {project.description && (
        <p className="text-text-secondary text-sm mb-4">{project.description}</p>
      )}

      {/* Tabs */}
      <div className="flex gap-1 mb-6 border-b border-glass-edge">
        {tabs.map((t) => (
          <button
            key={t.key}
            onClick={() => setTab(t.key)}
            className={`apex-tab px-4 py-2.5 text-sm font-medium transition-colors cursor-pointer ${
              tab === t.key
                ? 'text-foreground border-b-2 border-primary'
                : 'text-text-secondary hover:text-foreground'
            }`}
          >
            {t.label}
          </button>
        ))}
      </div>

      {/* Tab content */}
      {tab === 'scenarios' && (
        <ScenariosTab projectId={projectId!} project={project} />
      )}
      {tab === 'files' && (
        <FilesTab projectId={projectId!} project={project} />
      )}
      {tab === 'simulations' && (
        <SimulationsTab projectId={projectId!} />
      )}
    </div>
  );
}

/* ================================================================
   Scenarios Tab
   ================================================================ */

function ScenariosTab({ projectId, project }: { projectId: string; project: ProjectDetail }) {
  const navigate = useNavigate();

  const { data: scenarios, isLoading, error, refetch } = useQuery({
    queryKey: ['project-scenarios', projectId],
    queryFn: () => api.getProjectScenarios(projectId),
  });

  if (isLoading) return <TableSkeleton columns={4} rows={5} />;
  if (error) return <ErrorPanel error={error} onRetry={() => refetch()} />;
  if (!scenarios || scenarios.length === 0) {
    return (
      <EmptyState
        message="No scenarios in this project."
        action={
          !project.is_builtin ? (
            <Button variant="secondary" size="sm" onClick={() => {}}>
              Upload .xosc files
            </Button>
          ) : undefined
        }
      />
    );
  }

  return (
    <div className="space-y-3">
      {scenarios.map((s) => (
        <ScenarioCard
          key={s.file}
          scenario={s}
          onRun={() => navigate(`/projects/${projectId}/sim/new?scenario=${encodeURIComponent(s.file)}`)}
        />
      ))}
    </div>
  );
}

function ScenarioCard({ scenario, onRun }: { scenario: ScenarioInfo; onRun: () => void }) {
  const [expanded, setExpanded] = useState(false);

  return (
    <GlassPanel className="p-4">
      <div className="flex items-start justify-between">
        <div className="flex-1 min-w-0">
          <button
            className="text-left cursor-pointer"
            onClick={() => setExpanded(!expanded)}
          >
            <h4 className="font-mono text-sm font-medium">
              <span className="text-text-tertiary mr-1.5 text-xs">
                {expanded ? '\u25BC' : '\u25B6'}
              </span>
              {scenario.filename}
            </h4>
          </button>
          <div className="flex gap-4 mt-1 text-xs text-text-secondary">
            {scenario.road_file && (
              <span>Road: <span className="font-mono">{scenario.road_file}</span></span>
            )}
            {scenario.entities.length > 0 && (
              <span>{scenario.entities.length} entit{scenario.entities.length !== 1 ? 'ies' : 'y'}</span>
            )}
            {scenario.params.length > 0 && (
              <span>{scenario.params.length} param{scenario.params.length !== 1 ? 's' : ''}</span>
            )}
          </div>
        </div>
        <Button variant="primary" size="sm" onClick={onRun}>Run</Button>
      </div>

      {expanded && (
        <div className="mt-3 pt-3 border-t border-glass-edge space-y-3 text-sm">
          {/* Parameters */}
          {scenario.params.length > 0 && (
            <div>
              <h5 className="text-text-secondary text-xs mb-1.5">Parameters</h5>
              <div className="flex flex-wrap gap-2">
                {scenario.params.map((p) => (
                  <span
                    key={p.name}
                    className="bg-glass-1 border border-glass-edge px-2 py-0.5 text-xs font-mono"
                  >
                    {p.name}
                    <span className="text-text-tertiary ml-1">={p.value}</span>
                    <span className="text-text-tertiary ml-1 text-[10px]">({p.type})</span>
                  </span>
                ))}
              </div>
            </div>
          )}

          {/* Entities */}
          {scenario.entities.length > 0 && (
            <div>
              <h5 className="text-text-secondary text-xs mb-1.5">Entities</h5>
              <div className="flex flex-wrap gap-2">
                {scenario.entities.map((e) => (
                  <span
                    key={e.name}
                    className="bg-glass-1 border border-glass-edge px-2 py-0.5 text-xs"
                  >
                    {e.name}
                    {e.model && <span className="text-text-tertiary ml-1">({e.model})</span>}
                  </span>
                ))}
              </div>
            </div>
          )}
        </div>
      )}
    </GlassPanel>
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
   Simulations Tab
   ================================================================ */

function SimulationsTab({ projectId }: { projectId: string }) {
  const navigate = useNavigate();
  const [page, setPage] = useState(0);
  const PAGE_SIZE = 20;

  const { data, isLoading, error, refetch } = useQuery({
    queryKey: ['project-simulations', projectId, page],
    queryFn: () => api.getSimulations(undefined, PAGE_SIZE, page * PAGE_SIZE, projectId),
    refetchInterval: 3000,
  });

  const jobs = data?.jobs ?? [];
  const total = data?.total ?? 0;
  const totalPages = Math.max(1, Math.ceil(total / PAGE_SIZE));

  const columns = [
    { key: 'id', label: 'Job ID' },
    { key: 'scenario', label: 'Scenario' },
    { key: 'status', label: 'Status' },
    { key: 'started', label: 'Started' },
  ];

  if (isLoading) return <TableSkeleton columns={4} rows={5} />;
  if (error) return <ErrorPanel error={error} onRetry={() => refetch()} />;

  if (jobs.length === 0) {
    return (
      <EmptyState
        message="No simulation jobs for this project yet."
        action={
          <Link to={`/projects/${projectId}/sim/new`}>
            <Button size="sm">Run a simulation</Button>
          </Link>
        }
      />
    );
  }

  return (
    <>
      <TableShell columns={columns}>
        {jobs.map((job: SimulationStatus) => (
          <tr
            key={job.job_id}
            className="border-b border-glass-edge/50 hover:bg-glass-hover/30 cursor-pointer"
            onClick={() => navigate(`/simulations/${job.job_id}`)}
          >
            <td className="px-4 py-3">
              <span className="text-primary font-mono text-sm">{job.job_id}</span>
            </td>
            <td className="px-4 py-3 text-sm">{job.scenario_id}</td>
            <td className="px-4 py-3">
              <StatusBadge status={job.status} />
            </td>
            <td className="px-4 py-3 text-text-secondary text-sm">
              {job.started_at ? new Date(job.started_at).toLocaleTimeString() : '-'}
            </td>
          </tr>
        ))}
      </TableShell>

      {totalPages > 1 && (
        <div className="flex items-center justify-between mt-4 text-sm">
          <span className="text-text-secondary">
            {page * PAGE_SIZE + 1}–{Math.min((page + 1) * PAGE_SIZE, total)} of {total}
          </span>
          <div className="flex gap-2">
            <Button variant="secondary" size="sm" disabled={page === 0} onClick={() => setPage((p) => p - 1)}>
              Prev
            </Button>
            <Button variant="secondary" size="sm" disabled={page >= totalPages - 1} onClick={() => setPage((p) => p + 1)}>
              Next
            </Button>
          </div>
        </div>
      )}
    </>
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
