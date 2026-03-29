import { useSvStream } from '../../hooks/useSvStream';

function typeOf(value: unknown): string {
  if (typeof value === 'boolean') return 'bool';
  if (typeof value === 'number') return Number.isInteger(value) ? 'int' : 'double';
  if (typeof value === 'string') return 'string';
  return 'unknown';
}

function ValueCell({ value }: { value: unknown }) {
  if (typeof value === 'boolean') {
    return (
      <span className="flex items-center gap-1.5">
        <span className={`inline-block w-2 h-2 rounded-full ${value ? 'bg-success' : 'bg-destructive'}`} />
        {value ? 'true' : 'false'}
      </span>
    );
  }
  if (typeof value === 'number') {
    return <>{Number.isInteger(value) ? value : value.toFixed(4)}</>;
  }
  return <>{String(value)}</>;
}

export function SvLivePanel({ jobId }: { jobId: string }) {
  const { status, variables, simTime } = useSvStream(jobId);

  const statusIndicator: Record<string, { color: string; label: string }> = {
    connecting: { color: 'bg-warning', label: 'Connecting' },
    connected: { color: 'bg-success', label: 'Connected' },
    disconnected: { color: 'bg-text-tertiary', label: 'Disconnected' },
    error: { color: 'bg-destructive', label: 'Error' },
  };

  const { color, label } = statusIndicator[status] ?? statusIndicator.connecting;
  const entries = Object.entries(variables);

  return (
    <div className="h-full flex flex-col p-3 overflow-hidden">
      {/* Header */}
      <div className="flex items-center justify-between mb-2 shrink-0">
        <h2 className="text-sm font-medium text-text-secondary">Scenario Variables</h2>
        <div className="flex items-center gap-2 text-xs">
          <span className={`inline-block w-2 h-2 rounded-full ${color}`} />
          <span className="text-text-secondary">{label}</span>
          {simTime > 0 && (
            <span className="text-text-tertiary ml-2">t = {simTime.toFixed(2)}s</span>
          )}
        </div>
      </div>

      {/* Table */}
      {entries.length > 0 ? (
        <div className="flex-1 min-h-0 overflow-auto">
          <table className="w-full text-sm" style={{ tableLayout: 'fixed' }}>
            <colgroup>
              <col style={{ width: '40%' }} />
              <col style={{ width: '72px' }} />
              <col />
            </colgroup>
            <thead>
              <tr className="text-left text-text-secondary text-xs border-b border-glass-edge">
                <th className="py-1.5 pr-4 truncate">Name</th>
                <th className="py-1.5 pr-4">Type</th>
                <th className="py-1.5">Value</th>
              </tr>
            </thead>
            <tbody className="font-mono">
              {entries.map(([name, value]) => (
                <tr key={name} className="border-b border-glass-edge/50">
                  <td className="py-1.5 pr-4 text-foreground truncate" title={name}>{name}</td>
                  <td className="py-1.5 pr-4 text-text-tertiary">{typeOf(value)}</td>
                  <td className="py-1.5 truncate"><ValueCell value={value} /></td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      ) : status === 'connected' ? (
        <p className="text-text-secondary text-sm">Waiting for variable data...</p>
      ) : status === 'error' ? (
        <p className="text-destructive text-sm">Failed to connect to SV stream</p>
      ) : (
        <p className="text-text-tertiary text-sm">No variables in this scenario</p>
      )}
    </div>
  );
}
