export type ControllerType = 'default' | 'manual' | 'virtual_driver';
export type DriveMode = 'comfort' | 'sport';
export type LaneChangeTiming = 'late' | 'normal' | 'early';
export type LaneChangeGap = 'wide' | 'normal' | 'tight';

export interface ControllerSectionProps {
  controllerType: ControllerType;
  setControllerType: (v: ControllerType) => void;
  onOpenManualSettings?: () => void;
  onOpenVirtualDriverSettings?: () => void;
  driveMode: DriveMode;
  setDriveMode: (v: DriveMode) => void;
  // RouteDrive lane-change timing knobs — only shown when Route Drive is enabled.
  routeDriveMode?: boolean;
  laneChangeTiming: LaneChangeTiming;
  setLaneChangeTiming: (v: LaneChangeTiming) => void;
  laneChangeGap: LaneChangeGap;
  setLaneChangeGap: (v: LaneChangeGap) => void;
}

const btnBase = 'px-4 py-2 text-sm font-medium transition-colors cursor-pointer';
const btnActive = 'bg-primary/80 text-background glow-edge';
const btnInactive = 'bg-glass-1 text-text-secondary hover:bg-glass-hover hover:text-foreground';

const modeBtnBase = 'px-2.5 py-1 text-xs font-medium rounded transition-colors cursor-pointer';
const modeBtnActive = 'bg-blue-500 text-white';
const modeBtnInactive = 'bg-glass-1 text-text-secondary hover:bg-glass-hover hover:text-foreground';

const driveModeTooltip =
  'シフトポイントとシフト時挙動を切替えます。Sport は高回転側、ダウンシフト時 rev-match ブリップ、トルクインタラプト強め。Default controller / NPC のみに効果。Manual Drive 走行中の自車挙動には影響しません。';

export function ControllerSection({
  controllerType,
  setControllerType,
  onOpenManualSettings,
  onOpenVirtualDriverSettings,
  driveMode,
  setDriveMode,
  routeDriveMode,
  laneChangeTiming,
  setLaneChangeTiming,
  laneChangeGap,
  setLaneChangeGap,
}: ControllerSectionProps) {
  return (
    <div>
      <div className="flex items-end justify-between mb-2">
        <h3 className="text-xs text-text-tertiary">Controller</h3>
        <div className="flex items-center gap-1.5" title={driveModeTooltip}>
          <span className="text-xs text-text-tertiary">Drive Mode</span>
          <div className="inline-flex items-center gap-0.5 rounded border border-glass-edge p-0.5">
            {(['comfort', 'sport'] as DriveMode[]).map((m) => (
              <button
                key={m}
                type="button"
                onClick={() => setDriveMode(m)}
                className={`${modeBtnBase} ${driveMode === m ? modeBtnActive : modeBtnInactive}`}
              >
                {m === 'comfort' ? 'Comfort' : 'Sport'}
              </button>
            ))}
          </div>
        </div>
      </div>
      <div className="flex gap-2 mb-4">
        <button
          onClick={() => setControllerType('default')}
          className={`${btnBase} ${controllerType === 'default' ? btnActive : btnInactive}`}
        >
          Default
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
        <button
          onClick={() => setControllerType('virtual_driver')}
          title="Full vehicle physics driven by an internal virtual driver (route follow + SpeedAction + lane change). Phase 1 MVP."
          className={`${btnBase} ${controllerType === 'virtual_driver' ? btnActive : btnInactive} flex items-center gap-1.5`}
        >
          Virtual Driver
          {controllerType === 'virtual_driver' && onOpenVirtualDriverSettings && (
            <svg
              xmlns="http://www.w3.org/2000/svg"
              viewBox="0 0 20 20"
              fill="currentColor"
              className="w-3.5 h-3.5 opacity-70 hover:opacity-100"
              onClick={(e) => {
                e.stopPropagation();
                onOpenVirtualDriverSettings();
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

      {controllerType === 'manual' && (
        <div className="text-xs text-text-tertiary">
          Click the gear icon to configure Manual Drive settings.
        </div>
      )}

      {controllerType === 'virtual_driver' && (
        <div className="text-xs text-text-tertiary">
          Click the gear icon to configure Virtual Driver settings.
        </div>
      )}

      {routeDriveMode && (
        <div
          className="flex flex-col gap-2 mt-1"
          title="Route Drive のレーンチェンジ挙動。Timing=いつ切るか(早め/遅め)、Gap=どれだけ狭い車間で踏み切るか。"
        >
          <div className="flex items-center justify-between">
            <span className="text-xs text-text-tertiary">Lane Change · Timing</span>
            <div className="inline-flex items-center gap-0.5 rounded border border-glass-edge p-0.5">
              {(['late', 'normal', 'early'] as LaneChangeTiming[]).map((t) => (
                <button
                  key={t}
                  type="button"
                  onClick={() => setLaneChangeTiming(t)}
                  className={`${modeBtnBase} ${laneChangeTiming === t ? modeBtnActive : modeBtnInactive}`}
                >
                  {t === 'late' ? 'Late' : t === 'normal' ? 'Normal' : 'Early'}
                </button>
              ))}
            </div>
          </div>
          <div className="flex items-center justify-between">
            <span className="text-xs text-text-tertiary">Lane Change · Gap</span>
            <div className="inline-flex items-center gap-0.5 rounded border border-glass-edge p-0.5">
              {(['wide', 'normal', 'tight'] as LaneChangeGap[]).map((g) => (
                <button
                  key={g}
                  type="button"
                  onClick={() => setLaneChangeGap(g)}
                  className={`${modeBtnBase} ${laneChangeGap === g ? modeBtnActive : modeBtnInactive}`}
                >
                  {g === 'wide' ? 'Wide' : g === 'normal' ? 'Normal' : 'Tight'}
                </button>
              ))}
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
