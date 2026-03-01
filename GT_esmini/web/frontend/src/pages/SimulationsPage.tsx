import { useState } from 'react';
import { useQuery } from '@tanstack/react-query';
import { Link, useNavigate } from 'react-router-dom';
import { api, type SimulationStatus } from '../api/client';
import { Button } from '../components/ui/Button';
import { StatusBadge } from '../components/ui/Badge';
import { TableShell, TableSkeleton } from '../components/ui/Table';
import { EmptyState } from '../components/ui/EmptyState';
import { ErrorPanel } from '../components/ui/ErrorPanel';

const PAGE_SIZE = 20;

const statusFilters = [
  { label: 'All', value: '' },
  { label: 'Running', value: 'running' },
  { label: 'Completed', value: 'completed' },
  { label: 'Failed', value: 'failed' },
] as const;

const columns = [
  { key: 'id', label: 'Job ID' },
  { key: 'scenario', label: 'Scenario' },
  { key: 'controller', label: 'Controller' },
  { key: 'status', label: 'Status' },
  { key: 'started', label: 'Started' },
];

export function SimulationsPage() {
  const navigate = useNavigate();
  const [statusFilter, setStatusFilter] = useState('');
  const [page, setPage] = useState(0);

  const { data, isLoading, error, refetch } = useQuery({
    queryKey: ['simulations', statusFilter, page],
    queryFn: () => api.getSimulations(statusFilter || undefined, PAGE_SIZE, page * PAGE_SIZE),
    refetchInterval: 3000,
  });

  const jobs = data?.jobs ?? [];
  const total = data?.total ?? 0;
  const totalPages = Math.max(1, Math.ceil(total / PAGE_SIZE));

  return (
    <div>
      {/* Header */}
      <div className="flex items-center justify-between mb-6">
        <h1 className="text-2xl font-bold">Jobs</h1>
        <Link to="/simulations/new">
          <Button>New Simulation</Button>
        </Link>
      </div>

      {/* Status filter chips */}
      <div className="flex gap-2 mb-4">
        {statusFilters.map((f) => (
          <button
            key={f.value}
            onClick={() => { setStatusFilter(f.value); setPage(0); }}
            className={`px-3 py-1 text-xs font-medium transition-colors cursor-pointer ${
              statusFilter === f.value
                ? 'bg-primary/80 text-background glow-edge'
                : 'bg-glass-1 text-text-secondary hover:bg-glass-hover hover:text-foreground'
            }`}
          >
            {f.label}
          </button>
        ))}
      </div>

      {/* Loading */}
      {isLoading && <TableSkeleton columns={5} rows={6} />}

      {/* Error */}
      {error && <ErrorPanel error={error} onRetry={() => refetch()} />}

      {/* Empty */}
      {data && jobs.length === 0 && (
        <EmptyState
          message={statusFilter ? `No ${statusFilter} jobs.` : 'No simulation jobs yet.'}
          action={
            statusFilter ? (
              <Button variant="ghost" size="sm" onClick={() => setStatusFilter('')}>
                Show all
              </Button>
            ) : (
              <Link to="/simulations/new">
                <Button size="sm">Run your first simulation</Button>
              </Link>
            )
          }
        />
      )}

      {/* Table */}
      {data && jobs.length > 0 && (
        <>
          <TableShell columns={columns}>
            {jobs.map((job: SimulationStatus) => (
              <tr
                key={job.job_id}
                className="border-b border-glass-edge/50 hover:bg-glass-hover/30 cursor-pointer"
                onClick={() => navigate(`/simulations/${job.job_id}`)}
              >
                <td className="px-4 py-3">
                  <span className="text-primary font-mono">{job.job_id}</span>
                </td>
                <td className="px-4 py-3">{job.scenario_id}</td>
                <td className="px-4 py-3 text-text-secondary">{job.controller_type}</td>
                <td className="px-4 py-3">
                  <StatusBadge status={job.status} />
                </td>
                <td className="px-4 py-3 text-text-secondary">
                  {job.started_at ? new Date(job.started_at).toLocaleTimeString() : '-'}
                </td>
              </tr>
            ))}
          </TableShell>

          {/* Pagination */}
          {totalPages > 1 && (
            <div className="flex items-center justify-between mt-4 text-sm">
              <span className="text-text-secondary">
                {page * PAGE_SIZE + 1}–{Math.min((page + 1) * PAGE_SIZE, total)} of {total}
              </span>
              <div className="flex gap-2">
                <Button
                  variant="secondary"
                  size="sm"
                  disabled={page === 0}
                  onClick={() => setPage((p) => p - 1)}
                >
                  Prev
                </Button>
                <Button
                  variant="secondary"
                  size="sm"
                  disabled={page >= totalPages - 1}
                  onClick={() => setPage((p) => p + 1)}
                >
                  Next
                </Button>
              </div>
            </div>
          )}
        </>
      )}
    </div>
  );
}
