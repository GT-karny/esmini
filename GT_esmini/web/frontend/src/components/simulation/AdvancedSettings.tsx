import { NumberInput, TextInput, ToggleSwitch } from '../ui/Input';

export interface AdvancedSettingsProps {
  showAdvanced: boolean;
  setShowAdvanced: (v: boolean) => void;
  hz: number;
  setHz: (v: number) => void;
  timeout: number;
  setTimeout_: (v: number) => void;
  osiEnabled: boolean;
  osiIp: string;
  setOsiIp: (v: string) => void;
  headless: boolean;
  threads: boolean;
  setThreads: (v: boolean) => void;
  winX: number;
  setWinX: (v: number) => void;
  winY: number;
  setWinY: (v: number) => void;
  winW: number;
  setWinW: (v: number) => void;
  winH: number;
  setWinH: (v: number) => void;
  validationErrors: Record<string, string>;
  compact?: boolean;
}

export function AdvancedSettings({
  showAdvanced,
  setShowAdvanced,
  hz,
  setHz,
  timeout,
  setTimeout_,
  osiEnabled,
  osiIp,
  setOsiIp,
  headless,
  threads,
  setThreads,
  winX,
  setWinX,
  winY,
  setWinY,
  winW,
  setWinW,
  winH,
  setWinH,
  validationErrors,
  compact = false,
}: AdvancedSettingsProps) {
  return (
    <div>
      <button
        onClick={() => setShowAdvanced(!showAdvanced)}
        className="flex items-center gap-1.5 text-xs text-text-tertiary hover:text-text-secondary transition-colors cursor-pointer mb-1.5"
      >
        <svg
          className={`w-3 h-3 transition-transform ${showAdvanced ? 'rotate-90' : ''}`}
          viewBox="0 0 16 16"
          fill="currentColor"
        >
          <path d="M6 3l5 5-5 5V3z" />
        </svg>
        Advanced
      </button>

      {showAdvanced && (
        <div className={compact ? 'space-y-3' : 'space-y-4'}>
          <div className={`grid grid-cols-2 ${compact ? 'gap-3' : 'gap-4'}`}>
            <div>
              <NumberInput
                label="Frequency (Hz)"
                value={hz}
                onChange={(e) => setHz(Number(e.target.value))}
              />
              {validationErrors.hz && (
                <p className="text-destructive text-xs mt-1">{validationErrors.hz}</p>
              )}
            </div>
            <div>
              <NumberInput
                label="Timeout (s)"
                value={timeout}
                onChange={(e) => setTimeout_(Number(e.target.value))}
              />
              {validationErrors.timeout && (
                <p className="text-destructive text-xs mt-1">{validationErrors.timeout}</p>
              )}
            </div>
          </div>

          {osiEnabled && (
            <div>
              <TextInput
                label="OSI IP Address"
                value={osiIp}
                onChange={(e) => setOsiIp(e.target.value)}
                className="w-48"
              />
              {validationErrors.osiIp && (
                <p className="text-destructive text-xs mt-1">{validationErrors.osiIp}</p>
              )}
            </div>
          )}

          {!headless && (
            <>
              <ToggleSwitch
                label="Threaded viewer"
                checked={threads}
                onChange={setThreads}
                description="(OSG)"
              />
              <div>
                <h3 className="text-xs text-text-tertiary mb-1.5">Window Position & Size</h3>
                <div className="grid grid-cols-4 gap-3">
                  <NumberInput label="X" value={winX} onChange={(e) => setWinX(Number(e.target.value))} />
                  <NumberInput label="Y" value={winY} onChange={(e) => setWinY(Number(e.target.value))} />
                  <NumberInput label="Width" value={winW} onChange={(e) => setWinW(Number(e.target.value))} />
                  <NumberInput label="Height" value={winH} onChange={(e) => setWinH(Number(e.target.value))} />
                </div>
              </div>
            </>
          )}
        </div>
      )}
    </div>
  );
}
