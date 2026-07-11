/* ---------- Layer toggle ---------- */

export function LayerToggle({
  label,
  active,
  onClick,
}: {
  label: string;
  active: boolean;
  onClick: () => void;
}) {
  return (
    <button
      type="button"
      onClick={onClick}
      className={`px-2 py-0.5 rounded text-[10px] font-medium transition-colors backdrop-blur ${
        active
          ? 'bg-primary/80 text-white'
          : 'bg-glass-2/70 text-text-tertiary hover:bg-glass-2'
      }`}
    >
      {label}
    </button>
  );
}
