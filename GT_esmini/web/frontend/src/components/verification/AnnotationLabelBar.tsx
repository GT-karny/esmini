/**
 * Right-rail labeling control: three label buttons (Pass / Fail / Needs-discussion)
 * with keyboard hints + a comment box. The page owns the keyboard shortcuts and
 * auto-advance; this component is the visual + click surface and exposes a ref so
 * the page can focus the comment box (C key).
 */
import { forwardRef } from 'react';
import type { AnnotationLabel } from '../../api/client';

const BTN: Record<AnnotationLabel, { label: string; key: string; active: string }> = {
  pass: { label: 'Pass', key: 'P', active: 'bg-success/80 text-white border-success' },
  fail: { label: 'Fail', key: 'F', active: 'bg-destructive/80 text-white border-destructive' },
  'needs-discussion': { label: 'Needs-disc.', key: 'D', active: 'bg-warning/80 text-white border-warning' },
};

export const AnnotationLabelBar = forwardRef<HTMLTextAreaElement, {
  current: AnnotationLabel | null;
  comment: string;
  onLabel: (label: AnnotationLabel) => void;
  onComment: (text: string) => void;
  onCommentBlur: () => void;
  saved: boolean;
  saving: boolean;
  error?: string | null;
  autoAdvance: boolean;
  onToggleAutoAdvance: () => void;
}>(function AnnotationLabelBar(
  { current, comment, onLabel, onComment, onCommentBlur, saved, saving, error,
    autoAdvance, onToggleAutoAdvance }, ref,
) {
  return (
    <div className="rounded border border-glass-edge p-3 flex flex-col gap-2">
      <div className="flex items-center justify-between">
        <span className="text-xs font-medium text-foreground">Label</span>
        <span className="text-[11px] h-4">
          {saving ? <span className="text-text-tertiary">saving…</span>
            : saved ? <span className="text-success">✓ saved</span>
            : error ? <span className="text-destructive">{error}</span> : null}
        </span>
      </div>

      <div className="grid grid-cols-3 gap-1">
        {(Object.keys(BTN) as AnnotationLabel[]).map((lbl) => {
          const cfg = BTN[lbl];
          const isActive = current === lbl;
          return (
            <button
              key={lbl}
              onClick={() => onLabel(lbl)}
              className={`px-1.5 py-1.5 rounded border text-xs font-medium transition-colors ${
                isActive ? cfg.active
                  : 'border-glass-edge text-text-secondary hover:bg-glass-2'
              }`}
              title={`${cfg.label} (${cfg.key})`}
            >
              {cfg.label}
              <span className="ml-1 opacity-60 text-[10px]">{cfg.key}</span>
            </button>
          );
        })}
      </div>

      <textarea
        ref={ref}
        value={comment}
        onChange={(e) => onComment(e.target.value)}
        onBlur={onCommentBlur}
        placeholder="Comment (C to focus, Esc to blur)…"
        rows={3}
        className="bg-glass-1 border border-glass-edge rounded px-2 py-1 text-xs text-foreground resize-none"
      />

      <label className="flex items-center gap-1.5 text-[11px] text-text-secondary select-none cursor-pointer">
        <input type="checkbox" checked={autoAdvance} onChange={onToggleAutoAdvance}
          className="accent-primary" />
        Auto-advance to next unlabeled
      </label>
    </div>
  );
});
