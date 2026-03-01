import { useQuery } from '@tanstack/react-query';
import { api, type ScenarioInfo } from '../../api/client';
import { MarkdownRenderer } from '../ui/MarkdownRenderer';

interface ScenarioDetailPanelProps {
  projectId: string;
  scenario: ScenarioInfo;
}

export function ScenarioDetailPanel({ projectId, scenario }: ScenarioDetailPanelProps) {
  const { data: docsContent } = useQuery({
    queryKey: ['scenario-docs', projectId, scenario.file],
    queryFn: () => api.getScenarioDocs(projectId, scenario.file),
  });

  const imageBaseUrl = `/api/projects/${projectId}/files/docs/`;

  return (
    <div className="h-full overflow-y-auto p-4">
      <h3 className="flex items-center gap-2 text-sm font-display font-medium text-text-secondary tracking-wider mb-3">
        <svg viewBox="0 0 16 16" fill="none" className="w-3.5 h-3.5">
          <path d="M3 1h7l3 3v11H3V1z" fill="currentColor" opacity="0.3" />
          <path d="M3 1h7l3 3v11H3V1zm7 0v3h3" stroke="currentColor" strokeWidth="1.2" />
          <path d="M5 8h6M5 10.5h6M5 5.5h3" stroke="currentColor" strokeWidth="0.8" />
        </svg>
        {scenario.filename}
      </h3>

      {/* Markdown documentation */}
      {docsContent ? (
        <MarkdownRenderer
          content={docsContent}
          imageBaseUrl={imageBaseUrl}
          className="mb-4"
        />
      ) : (
        <p className="text-text-tertiary text-xs mb-4 bg-glass-1 border border-glass-edge p-3">
          Place <code className="font-mono text-text-secondary">docs/{scenario.filename.replace(/\.xosc$/, '')}.md</code> in your project to add documentation.
        </p>
      )}

      {/* Road file */}
      {scenario.road_file && (
        <div className="mb-3">
          <h4 className="text-xs text-text-secondary mb-1">Road File</h4>
          <span className="text-sm font-mono text-foreground">{scenario.road_file}</span>
        </div>
      )}

      {/* Entities */}
      {scenario.entities.length > 0 && (
        <div className="mb-3">
          <h4 className="text-xs text-text-secondary mb-1.5">Entities</h4>
          <div className="flex flex-wrap gap-1.5">
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

      {/* Parameters (read-only table) */}
      {scenario.params.length > 0 && (
        <div>
          <h4 className="text-xs text-text-secondary mb-1.5">Parameter Declarations</h4>
          <table className="w-full text-xs border border-glass-edge">
            <thead>
              <tr className="bg-glass-1 border-b border-glass-edge">
                <th className="text-left px-2 py-1.5 text-text-secondary font-medium">Name</th>
                <th className="text-left px-2 py-1.5 text-text-secondary font-medium">Type</th>
                <th className="text-left px-2 py-1.5 text-text-secondary font-medium">Default</th>
              </tr>
            </thead>
            <tbody className="font-mono">
              {scenario.params.map((p) => (
                <tr key={p.name} className="border-b border-glass-edge/50">
                  <td className="px-2 py-1.5 text-foreground">{p.name}</td>
                  <td className="px-2 py-1.5 text-text-tertiary">{p.type}</td>
                  <td className="px-2 py-1.5 text-text-secondary">{p.value}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}
    </div>
  );
}
