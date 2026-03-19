import type { ScriptInfo } from '../../api/client';
import { SelectInput, Checkbox } from '../ui/Input';

export interface ControllerSectionProps {
  controllerType: 'default' | 'python';
  setControllerType: (v: 'default' | 'python') => void;
  pythonScript: string;
  setPythonScript: (v: string) => void;
  pythonClass: string;
  setPythonClass: (v: string) => void;
  traceEnabled: boolean;
  setTraceEnabled: (v: boolean) => void;
  scripts: ScriptInfo[];
}

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
}: ControllerSectionProps) {
  return (
    <div>
      <h3 className="text-xs text-text-tertiary mb-2">Controller</h3>
      <div className="flex gap-2 mb-4">
        <button
          onClick={() => setControllerType('default')}
          className={`px-4 py-2 text-sm font-medium transition-colors cursor-pointer ${
            controllerType === 'default'
              ? 'bg-primary/80 text-background glow-edge'
              : 'bg-glass-1 text-text-secondary hover:bg-glass-hover hover:text-foreground'
          }`}
        >
          Default
        </button>
        <button
          onClick={() => setControllerType('python')}
          className={`px-4 py-2 text-sm font-medium transition-colors cursor-pointer ${
            controllerType === 'python'
              ? 'bg-primary/80 text-background glow-edge'
              : 'bg-glass-1 text-text-secondary hover:bg-glass-hover hover:text-foreground'
          }`}
        >
          Python Driver
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
    </div>
  );
}
