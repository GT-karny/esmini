import type { ReactNode } from 'react';

interface CardProps {
  title?: string;
  children: ReactNode;
  className?: string;
}

export function Card({ title, children, className = '' }: CardProps) {
  return (
    <section className={`bg-gray-900 rounded-lg border border-gray-800 p-4 ${className}`}>
      {title && <h2 className="text-sm font-medium text-gray-400 mb-3">{title}</h2>}
      {children}
    </section>
  );
}
