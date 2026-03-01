import type { ReactNode } from 'react';
import { GlassPanel } from '@osce/theme-apex';

interface CardProps {
  title?: string;
  children: ReactNode;
  className?: string;
  variant?: 'default' | 'elevated' | 'active';
}

export function Card({ title, children, className = '', variant = 'default' }: CardProps) {
  return (
    <GlassPanel variant={variant} className={`p-4 ${className}`}>
      {title && <h2 className="text-sm font-medium text-text-secondary mb-3">{title}</h2>}
      {children}
    </GlassPanel>
  );
}
