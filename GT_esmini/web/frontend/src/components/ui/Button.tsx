import type { ButtonHTMLAttributes } from 'react';

const variants = {
  primary:
    'bg-primary/80 hover:bg-primary text-background glow-edge',
  secondary:
    'bg-glass-1 hover:bg-glass-hover text-text-secondary hover:text-foreground border border-glass-edge',
  danger:
    'bg-destructive/80 hover:bg-destructive text-white',
  ghost:
    'bg-transparent hover:bg-glass-hover text-text-secondary hover:text-foreground',
} as const;

const sizes = {
  sm: 'text-xs px-3 py-1.5',
  md: 'text-sm px-4 py-2',
  lg: 'text-sm px-4 py-3',
} as const;

interface ButtonProps extends ButtonHTMLAttributes<HTMLButtonElement> {
  variant?: keyof typeof variants;
  size?: keyof typeof sizes;
}

export function Button({
  variant = 'primary',
  size = 'md',
  className = '',
  disabled,
  children,
  ...rest
}: ButtonProps) {
  return (
    <button
      className={`font-medium transition-colors cursor-pointer ${variants[variant]} ${sizes[size]} ${
        disabled ? 'opacity-50 cursor-not-allowed' : ''
      } ${className}`}
      disabled={disabled}
      {...rest}
    >
      {children}
    </button>
  );
}
