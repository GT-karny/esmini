import type { ButtonHTMLAttributes } from 'react';

const variants = {
  primary:
    'bg-blue-600 hover:bg-blue-500 text-white',
  secondary:
    'bg-gray-800 hover:bg-gray-700 text-gray-300',
  danger:
    'bg-red-600 hover:bg-red-500 text-white',
  ghost:
    'bg-transparent hover:bg-white/5 text-gray-300',
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
      className={`font-medium rounded-lg transition-colors cursor-pointer ${variants[variant]} ${sizes[size]} ${
        disabled ? 'opacity-50 cursor-not-allowed' : ''
      } ${className}`}
      disabled={disabled}
      {...rest}
    >
      {children}
    </button>
  );
}
