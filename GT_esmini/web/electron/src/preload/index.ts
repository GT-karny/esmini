/**
 * Preload script — exposes a minimal API to the renderer via contextBridge.
 *
 * Kept minimal for Phase 1.  Will expand for IPC (file dialogs, etc.)
 * when the OpenSCENARIO Editor integration is planned.
 */

import { contextBridge, ipcRenderer } from 'electron';

contextBridge.exposeInMainWorld('electronAPI', {
  /** Platform identifier */
  platform: process.platform,

  /** Flag for renderer-side feature detection */
  isElectron: true,

  /** Set the window title */
  setTitle: (title: string) => ipcRenderer.send('window:setTitle', title),

  /** Window controls for custom titlebar */
  minimize: () => ipcRenderer.send('window:minimize'),
  maximize: () => ipcRenderer.send('window:maximize'),
  close: () => ipcRenderer.send('window:close'),
  isMaximized: () => ipcRenderer.invoke('window:isMaximized') as Promise<boolean>,

  /** Open a path in the OS file explorer */
  openPath: (dirPath: string) => ipcRenderer.invoke('shell:openPath', dirPath) as Promise<string>,

  /** Show native directory picker dialog */
  selectDirectory: () => ipcRenderer.invoke('dialog:openDirectory') as Promise<string | null>,

  /** Listen for maximize/unmaximize events */
  onMaximizeChange: (callback: (maximized: boolean) => void) => {
    const onMax = () => callback(true);
    const onUnmax = () => callback(false);
    ipcRenderer.on('window:maximized', onMax);
    ipcRenderer.on('window:unmaximized', onUnmax);
    return () => {
      ipcRenderer.removeListener('window:maximized', onMax);
      ipcRenderer.removeListener('window:unmaximized', onUnmax);
    };
  },
});
