import type { InputHTMLAttributes, SelectHTMLAttributes, ReactNode } from 'react';

const baseInput =
  'w-full bg-glass-1 border border-glass-edge px-3 py-2 text-sm text-foreground focus:outline-none focus:border-glass-edge-active transition-colors placeholder:text-text-tertiary';

interface FieldWrapperProps {
  label?: string;
  children: ReactNode;
  className?: string;
}

function FieldWrapper({ label, children, className = '' }: FieldWrapperProps) {
  return (
    <div className={className}>
      {label && <label className="block text-xs text-text-secondary mb-1">{label}</label>}
      {children}
    </div>
  );
}

interface TextInputProps extends InputHTMLAttributes<HTMLInputElement> {
  label?: string;
  wrapperClassName?: string;
}

export function TextInput({ label, wrapperClassName, className = '', ...rest }: TextInputProps) {
  return (
    <FieldWrapper label={label} className={wrapperClassName}>
      <input className={`${baseInput} ${className}`} {...rest} />
    </FieldWrapper>
  );
}

interface NumberInputProps extends InputHTMLAttributes<HTMLInputElement> {
  label?: string;
  wrapperClassName?: string;
}

export function NumberInput({ label, wrapperClassName, className = '', ...rest }: NumberInputProps) {
  return (
    <FieldWrapper label={label} className={wrapperClassName}>
      <input type="number" className={`${baseInput} ${className}`} {...rest} />
    </FieldWrapper>
  );
}

interface SelectInputProps extends SelectHTMLAttributes<HTMLSelectElement> {
  label?: string;
  wrapperClassName?: string;
  children: ReactNode;
}

export function SelectInput({ label, wrapperClassName, className = '', children, ...rest }: SelectInputProps) {
  return (
    <FieldWrapper label={label} className={wrapperClassName}>
      <select className={`${baseInput} ${className}`} {...rest}>
        {children}
      </select>
    </FieldWrapper>
  );
}

interface CheckboxProps extends InputHTMLAttributes<HTMLInputElement> {
  label: string;
  description?: string;
}

export function Checkbox({ label, description, className = '', ...rest }: CheckboxProps) {
  return (
    <label className={`flex items-center gap-2 text-sm cursor-pointer ${className}`}>
      <input type="checkbox" {...rest} />
      <span>{label}</span>
      {description && <span className="text-xs text-text-tertiary">{description}</span>}
    </label>
  );
}
