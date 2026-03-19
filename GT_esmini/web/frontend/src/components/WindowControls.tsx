import { useEffect, useState } from 'react';

/** Type for the electronAPI exposed via preload */
interface ElectronAPI {
  isElectron: boolean;
  platform: string;
  minimize: () => void;
  maximize: () => void;
  close: () => void;
  isMaximized: () => Promise<boolean>;
  onMaximizeChange: (cb: (maximized: boolean) => void) => () => void;
}

declare global {
  interface Window {
    electronAPI?: ElectronAPI;
  }
}

const isElectron = !!window.electronAPI?.isElectron;

/**
 * Custom window control buttons (minimize, maximize/restore, close).
 * Only renders inside Electron — hidden in browser.
 */
export function WindowControls() {
  const [maximized, setMaximized] = useState(false);

  useEffect(() => {
    if (!isElectron) return;
    window.electronAPI!.isMaximized().then(setMaximized);
    return window.electronAPI!.onMaximizeChange(setMaximized);
  }, []);

  if (!isElectron) return null;

  const api = window.electronAPI!;

  return (
    <div className="flex items-center -mr-2 ml-2">
      {/* Minimize */}
      <button
        onClick={api.minimize}
        className="window-control w-11 h-8 inline-flex items-center justify-center text-text-secondary hover:bg-white/10 transition-colors"
        aria-label="Minimize"
      >
        <svg width="10" height="1" viewBox="0 0 10 1">
          <rect width="10" height="1" fill="currentColor" />
        </svg>
      </button>

      {/* Maximize / Restore */}
      <button
        onClick={api.maximize}
        className="window-control w-11 h-8 inline-flex items-center justify-center text-text-secondary hover:bg-white/10 transition-colors"
        aria-label={maximized ? 'Restore' : 'Maximize'}
      >
        {maximized ? (
          // Restore icon (two overlapping rectangles)
          <svg width="10" height="10" viewBox="0 0 10 10">
            <path d="M2 3v6h6V3H2zm1 1h4v4H3V4z" fill="currentColor" />
            <path d="M3 1h6v6h-1V2H3V1z" fill="currentColor" />
          </svg>
        ) : (
          // Maximize icon (single rectangle)
          <svg width="10" height="10" viewBox="0 0 10 10">
            <rect x="0.5" y="0.5" width="9" height="9" fill="none" stroke="currentColor" strokeWidth="1" />
          </svg>
        )}
      </button>

      {/* Close */}
      <button
        onClick={api.close}
        className="window-control w-11 h-8 inline-flex items-center justify-center text-text-secondary hover:bg-[#e81123] hover:text-white transition-colors"
        aria-label="Close"
      >
        <svg width="10" height="10" viewBox="0 0 10 10">
          <path d="M1 1l8 8M9 1l-8 8" stroke="currentColor" strokeWidth="1.2" />
        </svg>
      </button>
    </div>
  );
}

/** Returns true when running inside Electron */
export { isElectron };
