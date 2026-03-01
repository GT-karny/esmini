import type { ScenarioInfo } from '../../api/client';

interface ScenarioListPanelProps {
  scenarios: ScenarioInfo[];
  selectedFile: string | null;
  onSelect: (file: string) => void;
}

export function ScenarioListPanel({ scenarios, selectedFile, onSelect }: ScenarioListPanelProps) {
  return (
    <div className="h-full overflow-y-auto">
      <h3 className="flex items-center gap-2 text-sm font-display font-medium text-text-secondary uppercase tracking-wider px-3 py-2 border-b border-glass-edge">
        <svg viewBox="0 0 16 16" fill="currentColor" className="w-3.5 h-3.5">
          <path d="M2 3h12v1.5H2zm0 4h12v1.5H2zm0 4h8v1.5H2z" />
        </svg>
        Scenarios
      </h3>
      {scenarios.length === 0 ? (
        <p className="text-text-tertiary text-sm px-3 py-4">No scenarios found.</p>
      ) : (
        <ul>
          {scenarios.map((s) => {
            const isSelected = s.file === selectedFile;
            return (
              <li key={s.file}>
                <button
                  onClick={() => onSelect(s.file)}
                  className={`w-full text-left px-3 py-2.5 transition-colors cursor-pointer ${
                    isSelected
                      ? 'bg-glass-active border-l-2 border-primary'
                      : 'hover:bg-glass-hover/30 border-l-2 border-transparent'
                  }`}
                >
                  <div className="font-mono text-sm text-foreground truncate">
                    {s.filename}
                  </div>
                  <div className="flex gap-3 mt-0.5 text-[11px] text-text-tertiary">
                    {s.entities.length > 0 && (
                      <span>{s.entities.length} entit{s.entities.length !== 1 ? 'ies' : 'y'}</span>
                    )}
                    {s.params.length > 0 && (
                      <span>{s.params.length} param{s.params.length !== 1 ? 's' : ''}</span>
                    )}
                  </div>
                </button>
              </li>
            );
          })}
        </ul>
      )}
    </div>
  );
}
