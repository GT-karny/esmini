import { useState } from 'react';
import { useQuery } from '@tanstack/react-query';
import { useNavigate } from 'react-router-dom';
import { api, type Scenario, type ScenarioDetail } from '../api/client';
import { Button } from '../components/ui/Button';
import { TextInput } from '../components/ui/Input';
import { TableShell, TableSkeleton } from '../components/ui/Table';
import { EmptyState } from '../components/ui/EmptyState';
import { ErrorPanel } from '../components/ui/ErrorPanel';

const columns = [
  { key: 'name', label: 'Name' },
  { key: 'file', label: 'File' },
  { key: 'size', label: 'Size', align: 'right' as const },
  { key: 'actions', label: '', align: 'right' as const },
];

export function ScenariosPage() {
  const [search, setSearch] = useState('');
  const [expandedId, setExpandedId] = useState<string | null>(null);
  const navigate = useNavigate();

  const { data: scenarios, isLoading, error, refetch } = useQuery({
    queryKey: ['scenarios', search],
    queryFn: () => api.getScenarios(search || undefined),
  });

  const { data: detail } = useQuery<ScenarioDetail>({
    queryKey: ['scenario-detail', expandedId],
    queryFn: () => api.getScenario(expandedId!),
    enabled: !!expandedId,
  });

  const toggleExpand = (id: string) => {
    setExpandedId((prev) => (prev === id ? null : id));
  };

  return (
    <div>
      <div className="flex items-center justify-between mb-6">
        <h1 className="text-2xl font-bold">Scenarios</h1>
        <TextInput
          type="text"
          placeholder="Search scenarios..."
          value={search}
          onChange={(e) => setSearch(e.target.value)}
          className="w-64"
        />
      </div>

      {isLoading && <TableSkeleton columns={4} rows={8} />}
      {error && <ErrorPanel error={error} onRetry={() => refetch()} />}

      {scenarios && scenarios.length === 0 && (
        <EmptyState
          message={search ? `No scenarios matching "${search}".` : 'No scenarios found.'}
          action={
            search ? (
              <Button variant="ghost" size="sm" onClick={() => setSearch('')}>
                Clear search
              </Button>
            ) : undefined
          }
        />
      )}

      {scenarios && scenarios.length > 0 && (
        <TableShell columns={columns}>
          {scenarios.map((s: Scenario) => (
            <ScenarioRow
              key={s.id}
              scenario={s}
              isExpanded={expandedId === s.id}
              detail={expandedId === s.id ? detail ?? null : null}
              onToggle={() => toggleExpand(s.id)}
              onRun={() => navigate(`/simulations/new?scenario=${s.id}`)}
            />
          ))}
        </TableShell>
      )}

      {scenarios && scenarios.length > 0 && (
        <p className="text-gray-500 text-sm mt-4">
          {scenarios.length} scenario{scenarios.length !== 1 ? 's' : ''} found
        </p>
      )}
    </div>
  );
}

/* ---------- Row sub-component ---------- */

function ScenarioRow({
  scenario,
  isExpanded,
  detail,
  onToggle,
  onRun,
}: {
  scenario: Scenario;
  isExpanded: boolean;
  detail: ScenarioDetail | null;
  onToggle: () => void;
  onRun: () => void;
}) {
  return (
    <>
      <tr
        className="border-b border-gray-800/50 hover:bg-gray-800/50 cursor-pointer"
        onClick={onToggle}
      >
        <td className="px-4 py-3 font-medium">
          <span className="mr-2 text-gray-500 text-xs">{isExpanded ? '\u25BC' : '\u25B6'}</span>
          {scenario.id}
        </td>
        <td className="px-4 py-3 text-gray-400">{scenario.filename}</td>
        <td className="px-4 py-3 text-right text-gray-400">
          {(scenario.size / 1024).toFixed(1)} KB
        </td>
        <td className="px-4 py-3 text-right">
          <Button
            variant="ghost"
            size="sm"
            onClick={(e) => {
              e.stopPropagation();
              onRun();
            }}
          >
            Run
          </Button>
        </td>
      </tr>

      {isExpanded && (
        <tr className="border-b border-gray-800/50">
          <td colSpan={4} className="px-4 py-3 bg-gray-800/30">
            {!detail ? (
              <div className="flex gap-2 items-center text-gray-500 text-sm">
                <div className="w-3 h-3 border-2 border-gray-500 border-t-transparent rounded-full animate-spin" />
                Loading details...
              </div>
            ) : (
              <div className="grid grid-cols-2 gap-4 text-sm">
                <div>
                  <span className="text-gray-500">Road file: </span>
                  <span className="text-gray-300 font-mono">{detail.road_file ?? 'N/A'}</span>
                </div>
                <div>
                  <span className="text-gray-500">Has controller: </span>
                  <span className="text-gray-300">{detail.has_controller ? 'Yes' : 'No'}</span>
                </div>
                {detail.entities.length > 0 && (
                  <div className="col-span-2">
                    <span className="text-gray-500 block mb-1">Entities:</span>
                    <div className="flex flex-wrap gap-2">
                      {detail.entities.map((e) => (
                        <span key={e.name} className="bg-gray-800 px-2 py-0.5 rounded text-xs text-gray-300">
                          {e.name}
                          {e.vehicle && <span className="text-gray-500 ml-1">({e.vehicle})</span>}
                        </span>
                      ))}
                    </div>
                  </div>
                )}
              </div>
            )}
          </td>
        </tr>
      )}
    </>
  );
}
