import type { ScriptInfo } from '../../api/client';
import { SelectInput, Checkbox } from '../ui/Input';

export interface ControllerSectionProps {
  controllerType: 'default' | 'python' | 'manual';
  setControllerType: (v: 'default' | 'python' | 'manual') => void;
  pythonScript: string;
  setPythonScript: (v: string) => void;
  pythonClass: string;
  setPythonClass: (v: string) => void;
  traceEnabled: boolean;
  setTraceEnabled: (v: boolean) => void;
  scripts: ScriptInfo[];
  onOpenManualSettings?: () => void;
}

const btnBase = 'px-4 py-2 text-sm font-medium transition-colors cursor-pointer';
const btnActive = 'bg-primary/80 text-background glow-edge';
const btnInactive = 'bg-glass-1 text-text-secondary hover:bg-glass-hover hover:text-foreground';

export function ControllerSection({
  controllerType,
  setControllerType,
  pythonScript,
  setPythonScript,
  pythonClass,
  setPythonClass,
  traceEnabled,
  setTraceEnabled,
  scripts,
  onOpenManualSettings,
}: ControllerSectionProps) {
  return (
    <div>
      <h3 className="text-xs text-text-tertiary mb-2">Controller</h3>
      <div className="flex gap-2 mb-4">
        <button
          onClick={() => setControllerType('default')}
          className={`${btnBase} ${controllerType === 'default' ? btnActive : btnInactive}`}
        >
          Default
        </button>
        <button
          onClick={() => setControllerType('python')}
          className={`${btnBase} ${controllerType === 'python' ? btnActive : btnInactive}`}
        >
          Python Driver
        </button>
        <button
          onClick={() => {
            setControllerType('manual');
          }}
          className={`${btnBase} ${controllerType === 'manual' ? btnActive : btnInactive} flex items-center gap-1.5`}
        >
          Manual Drive
          {controllerType === 'manual' && onOpenManualSettings && (
            <svg
              xmlns="http://www.w3.org/2000/svg"
              viewBox="0 0 20 20"
              fill="currentColor"
              className="w-3.5 h-3.5 opacity-70 hover:opacity-100"
              onClick={(e) => {
                e.stopPropagation();
                onOpenManualSettings();
              }}
            >
              <path
                fillRule="evenodd"
                d="M7.84 1.804A1 1 0 0 1 8.82 1h2.36a1 1 0 0 1 .98.804l.331 1.652a6.993 6.993 0 0 1 1.929 1.115l1.598-.54a1 1 0 0 1 1.186.447l1.18 2.044a1 1 0 0 1-.205 1.251l-1.267 1.113a7.047 7.047 0 0 1 0 2.228l1.267 1.113a1 1 0 0 1 .206 1.25l-1.18 2.045a1 1 0 0 1-1.187.447l-1.598-.54a6.993 6.993 0 0 1-1.929 1.115l-.33 1.652a1 1 0 0 1-.98.804H8.82a1 1 0 0 1-.98-.804l-.331-1.652a6.993 6.993 0 0 1-1.929-1.115l-1.598.54a1 1 0 0 1-1.186-.447l-1.18-2.044a1 1 0 0 1 .205-1.251l1.267-1.114a7.05 7.05 0 0 1 0-2.227L1.821 7.773a1 1 0 0 1-.206-1.25l1.18-2.045a1 1 0 0 1 1.187-.447l1.598.54A6.993 6.993 0 0 1 7.51 3.456l.33-1.652ZM10 13a3 3 0 1 0 0-6 3 3 0 0 0 0 6Z"
                clipRule="evenodd"
              />
            </svg>
          )}
        </button>
      </div>

      {controllerType === 'python' && (
        <div className="space-y-3">
          <SelectInput
            label="Python Script"
            value={pythonScript}
            onChange={(e) => {
              setPythonScript(e.target.value);
              const script = scripts.find((s: ScriptInfo) => s.path === e.target.value);
              if (script?.classes.length) setPythonClass(script.classes[0]);
            }}
          >
            {scripts.map((s: ScriptInfo) => (
              <option key={s.path} value={s.path}>
                {s.recommended ? '\u2605 ' : ''}{s.name} ({s.category})
              </option>
            ))}
          </SelectInput>
          <SelectInput
            label="Class Name"
            value={pythonClass}
            onChange={(e) => setPythonClass(e.target.value)}
          >
            {(scripts.find((s: ScriptInfo) => s.path === pythonScript)?.classes ?? []).map((c: string) => (
              <option key={c} value={c}>{c}</option>
            ))}
          </SelectInput>
          <Checkbox
            label="Enable trace logging"
            checked={traceEnabled}
            onChange={(e) => setTraceEnabled(e.target.checked)}
          />
        </div>
      )}

      {controllerType === 'manual' && (
        <div className="text-xs text-text-tertiary">
          Click the gear icon to configure Manual Drive settings.
        </div>
      )}
    </div>
  );
}
