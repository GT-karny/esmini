import { useState } from 'react';
import { useQuery } from '@tanstack/react-query';
import { useNavigate } from 'react-router-dom';
import { api, type Scenario } from '../api/client';

export function ScenariosPage() {
  const [search, setSearch] = useState('');
  const navigate = useNavigate();

  const { data: scenarios, isLoading, error } = useQuery({
    queryKey: ['scenarios', search],
    queryFn: () => api.getScenarios(search || undefined),
  });

  return (
    <div>
      <div className="flex items-center justify-between mb-6">
        <h1 className="text-2xl font-bold">Scenarios</h1>
        <input
          type="text"
          placeholder="Search scenarios..."
          value={search}
          onChange={(e) => setSearch(e.target.value)}
          className="bg-gray-800 border border-gray-700 rounded px-3 py-2 text-sm w-64 focus:outline-none focus:border-blue-500"
        />
      </div>

      {isLoading && <p className="text-gray-400">Loading...</p>}
      {error && <p className="text-red-400">Error: {String(error)}</p>}

      {scenarios && (
        <div className="bg-gray-900 rounded-lg border border-gray-800 overflow-hidden">
          <table className="w-full text-sm">
            <thead>
              <tr className="border-b border-gray-800 text-gray-400">
                <th className="text-left px-4 py-3 font-medium">Name</th>
                <th className="text-left px-4 py-3 font-medium">File</th>
                <th className="text-right px-4 py-3 font-medium">Size</th>
                <th className="text-right px-4 py-3 font-medium">Actions</th>
              </tr>
            </thead>
            <tbody>
              {scenarios.map((s: Scenario) => (
                <tr
                  key={s.id}
                  className="border-b border-gray-800/50 hover:bg-gray-800/50 cursor-pointer"
                  onClick={() => navigate(`/simulations/new?scenario=${s.id}`)}
                >
                  <td className="px-4 py-3 font-medium">{s.id}</td>
                  <td className="px-4 py-3 text-gray-400">{s.filename}</td>
                  <td className="px-4 py-3 text-right text-gray-400">
                    {(s.size / 1024).toFixed(1)} KB
                  </td>
                  <td className="px-4 py-3 text-right">
                    <button
                      onClick={(e) => {
                        e.stopPropagation();
                        navigate(`/simulations/new?scenario=${s.id}`);
                      }}
                      className="text-blue-400 hover:text-blue-300 text-sm"
                    >
                      Run
                    </button>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
          {scenarios.length === 0 && (
            <p className="text-gray-400 text-center py-8">No scenarios found.</p>
          )}
        </div>
      )}

      <p className="text-gray-500 text-sm mt-4">
        {scenarios?.length ?? 0} scenarios found
      </p>
    </div>
  );
}
