const statusStyles: Record<string, string> = {
  queued: 'bg-warning/20 text-warning border-warning/30',
  running: 'bg-info/20 text-info border-info/30',
  completed: 'bg-success/20 text-success border-success/30',
  failed: 'bg-destructive/20 text-destructive border-destructive/30',
  cancelled: 'bg-glass-1 text-text-secondary border-glass-edge',
  timeout: 'bg-warning/20 text-warning border-warning/30',
};

interface BadgeProps {
  status: string;
  bordered?: boolean;
  className?: string;
}

export function StatusBadge({ status, bordered = false, className = '' }: BadgeProps) {
  const style = statusStyles[status] ?? '';
  return (
    <span
      className={`inline-block px-2 py-0.5 text-xs font-medium ${style} ${
        bordered ? 'border' : ''
      } ${className}`}
    >
      {status}
    </span>
  );
}
