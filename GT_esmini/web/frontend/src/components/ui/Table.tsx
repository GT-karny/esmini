import type { ReactNode } from 'react';

interface Column {
  key: string;
  label: string;
  align?: 'left' | 'right';
  className?: string;
}

interface TableShellProps {
  columns: Column[];
  children: ReactNode;
}

export function TableShell({ columns, children }: TableShellProps) {
  return (
    <div className="bg-gray-900 rounded-lg border border-gray-800 overflow-hidden">
      <table className="w-full text-sm">
        <thead>
          <tr className="border-b border-gray-800 text-gray-400">
            {columns.map((col) => (
              <th
                key={col.key}
                className={`${col.align === 'right' ? 'text-right' : 'text-left'} px-4 py-3 font-medium ${col.className ?? ''}`}
              >
                {col.label}
              </th>
            ))}
          </tr>
        </thead>
        <tbody>{children}</tbody>
      </table>
    </div>
  );
}

/* Skeleton rows for loading state */
export function TableSkeleton({ columns, rows = 5 }: { columns: number; rows?: number }) {
  return (
    <div className="bg-gray-900 rounded-lg border border-gray-800 overflow-hidden">
      <table className="w-full text-sm">
        <thead>
          <tr className="border-b border-gray-800">
            {Array.from({ length: columns }).map((_, i) => (
              <th key={i} className="px-4 py-3">
                <div className="h-3 bg-gray-800 rounded animate-pulse w-16" />
              </th>
            ))}
          </tr>
        </thead>
        <tbody>
          {Array.from({ length: rows }).map((_, row) => (
            <tr key={row} className="border-b border-gray-800/50">
              {Array.from({ length: columns }).map((_, col) => (
                <td key={col} className="px-4 py-3">
                  <div
                    className="h-3 bg-gray-800 rounded animate-pulse"
                    style={{ width: `${50 + Math.random() * 40}%` }}
                  />
                </td>
              ))}
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}
