interface TabItem {
  key: string;
  label: string;
  badge?: number;
}

interface TabsProps {
  items: TabItem[];
  activeKey: string;
  onChange: (key: string) => void;
  size?: 'sm' | 'md';
  className?: string;
}

const sizes = {
  md: 'px-3 py-2 text-[10px]',
  sm: 'px-2 py-1.5 text-[9px]',
} as const;

export function Tabs({ items, activeKey, onChange, size = 'md', className = '' }: TabsProps) {
  return (
    <div className={`flex gap-1 ${className}`} role="tablist">
      {items.map((item) => {
        const active = item.key === activeKey;
        return (
          <button
            key={item.key}
            role="tab"
            aria-selected={active}
            data-state={active ? 'active' : 'inactive'}
            onClick={() => onChange(item.key)}
            className={`apex-tab ${sizes[size]} font-medium transition-colors cursor-pointer ${
              active
                ? 'bg-glass-active text-foreground'
                : 'text-text-secondary hover:text-foreground hover:bg-glass-hover'
            }`}
          >
            {item.label}
            {typeof item.badge === 'number' && (
              <span
                className={`ml-1.5 inline-flex items-center justify-center min-w-[18px] h-[18px] px-1 rounded-full text-[10px] font-semibold ${
                  active
                    ? 'bg-primary/20 text-primary'
                    : 'bg-glass-1 text-text-secondary'
                }`}
              >
                {item.badge}
              </span>
            )}
          </button>
        );
      })}
    </div>
  );
}
