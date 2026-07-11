/**
 * Electron shell integration (preload bridge).
 *
 * Lives outside the component tree so component files only export components
 * (react-refresh constraint) and any module can test for the Electron shell.
 */

/** Shape of the electronAPI exposed via the Electron preload script. */
export interface ElectronAPI {
  isElectron: boolean;
  platform: string;
  minimize: () => void;
  maximize: () => void;
  close: () => void;
  isMaximized: () => Promise<boolean>;
  onMaximizeChange: (cb: (maximized: boolean) => void) => () => void;
  openPath: (dirPath: string) => Promise<string>;
  selectDirectory: () => Promise<string | null>;
}

declare global {
  interface Window {
    electronAPI?: ElectronAPI;
  }
}

/** True when running inside the Electron shell (browser otherwise). */
export const isElectron = !!window.electronAPI?.isElectron;
