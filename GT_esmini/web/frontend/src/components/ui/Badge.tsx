const statusStyles: Record<string, string> = {
  queued: 'bg-yellow-500/20 text-yellow-400 border-yellow-500/30',
  running: 'bg-blue-500/20 text-blue-400 border-blue-500/30',
  completed: 'bg-green-500/20 text-green-400 border-green-500/30',
  failed: 'bg-red-500/20 text-red-400 border-red-500/30',
  cancelled: 'bg-gray-500/20 text-gray-400 border-gray-500/30',
  timeout: 'bg-orange-500/20 text-orange-400 border-orange-500/30',
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
      className={`inline-block px-2 py-0.5 rounded text-xs font-medium ${style} ${
        bordered ? 'border' : ''
      } ${className}`}
    >
      {status}
    </span>
  );
}
