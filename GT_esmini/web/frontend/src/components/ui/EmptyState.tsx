import type { ReactNode } from 'react';

interface EmptyStateProps {
  message: string;
  action?: ReactNode;
}

export function EmptyState({ message, action }: EmptyStateProps) {
  return (
    <div className="text-center py-12">
      <p className="text-text-secondary mb-4">{message}</p>
      {action}
    </div>
  );
}
