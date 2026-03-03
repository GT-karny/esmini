import { Button } from './Button';

interface ErrorPanelProps {
  error: unknown;
  onRetry?: () => void;
}

export function ErrorPanel({ error, onRetry }: ErrorPanelProps) {
  const message = error instanceof Error ? error.message : String(error);
  return (
    <div className="bg-destructive/10 border border-destructive/20 p-4">
      <p className="text-destructive text-sm mb-3">
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
