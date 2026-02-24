import { useQuery } from '@tanstack/react-query';
import { Link } from 'react-router-dom';
import { api, type SimulationStatus } from '../api/client';

const statusColors: Record<string, string> = {
  queued: 'bg-yellow-500/20 text-yellow-400',
  running: 'bg-blue-500/20 text-blue-400',
  completed: 'bg-green-500/20 text-green-400',
  failed: 'bg-red-500/20 text-red-400',
  cancelled: 'bg-gray-500/20 text-gray-400',
  timeout: 'bg-orange-500/20 text-orange-400',
};

export function SimulationsPage() {
  const { data, isLoading, error } = useQuery({
    queryKey: ['simulations'],
    queryFn: () => api.getSimulations(),
    refetchInterval: 3000,
  });

  return (
    <div>
      <div className="flex items-center justify-between mb-6">
        <h1 className="text-2xl font-bold">Jobs</h1>
        <Link
          to="/simulations/new"
          className="bg-blue-600 hover:bg-blue-500 text-white text-sm font-medium px-4 py-2 rounded transition-colors"
        >
          New Simulation
        </Link>
      </div>

      {isLoading && <p className="text-gray-400">Loading...</p>}
      {error && <p className="text-red-400">Error: {String(error)}</p>}

      {data && (
        <div className="bg-gray-900 rounded-lg border border-gray-800 overflow-hidden">
          <table className="w-full text-sm">
            <thead>
              <tr className="border-b border-gray-800 text-gray-400">
                <th className="text-left px-4 py-3 font-medium">Job ID</th>
                <th className="text-left px-4 py-3 font-medium">Scenario</th>
                <th className="text-left px-4 py-3 font-medium">Controller</th>
                <th className="text-left px-4 py-3 font-medium">Status</th>
                <th className="text-left px-4 py-3 font-medium">Started</th>
              </tr>
            </thead>
            <tbody>
              {data.jobs.map((job: SimulationStatus) => (
                <tr key={job.job_id} className="border-b border-gray-800/50 hover:bg-gray-800/50">
                  <td className="px-4 py-3">
                    <Link to={`/simulations/${job.job_id}`} className="text-blue-400 hover:text-blue-300 font-mono">
                      {job.job_id}
                    </Link>
                  </td>
                  <td className="px-4 py-3">{job.scenario_id}</td>
                  <td className="px-4 py-3 text-gray-400">{job.controller_type}</td>
                  <td className="px-4 py-3">
                    <span className={`inline-block px-2 py-0.5 rounded text-xs font-medium ${statusColors[job.status] ?? ''}`}>
                      {job.status}
                    </span>
                  </td>
                  <td className="px-4 py-3 text-gray-400">
                    {job.started_at ? new Date(job.started_at).toLocaleTimeString() : '-'}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
          {data.jobs.length === 0 && (
            <p className="text-gray-400 text-center py-8">No simulation jobs yet.</p>
          )}
        </div>
      )}
    </div>
  );
}
