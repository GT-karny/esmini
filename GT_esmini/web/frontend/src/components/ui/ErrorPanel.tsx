import { Button } from './Button';

interface ErrorPanelProps {
  error: unknown;
  onRetry?: () => void;
}

export function ErrorPanel({ error, onRetry }: ErrorPanelProps) {
  const message = error instanceof Error ? error.message : String(error);
  return (
    <div className="bg-red-500/10 border border-red-500/20 rounded-lg p-4">
      <p className="text-red-400 text-sm mb-3">
        Failed to load data: {message}
      </p>
      {onRetry && (
        <Button variant="secondary" size="sm" onClick={onRetry}>
          Retry
        </Button>
      )}
    </div>
  );
}
