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
});
