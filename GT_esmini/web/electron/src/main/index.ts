/**
 * GT_Sim Desktop — Electron main process.
 *
 * Lifecycle:
 *   1. app.whenReady()
 *   2. Start FastAPI server (child process)
 *   3. Create BrowserWindow → load server URL
 *   4. On window close → stop server → quit
 */

import { app, BrowserWindow, ipcMain } from 'electron';
import path from 'node:path';
import { startServer, stopServer } from './server.js';

let mainWindow: BrowserWindow | null = null;

// ---------------------------------------------------------------------------
// Window
// ---------------------------------------------------------------------------

function createWindow(serverUrl: string): void {
  mainWindow = new BrowserWindow({
    width: 1400,
    height: 900,
    title: 'GT_Sim',
    backgroundColor: '#0a0a0f',
    show: false,
    webPreferences: {
      preload: path.join(__dirname, '..', 'preload', 'index.js'),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: false,
    },
  });

  mainWindow.loadURL(serverUrl);

  // Show window once content is ready (avoid white flash)
  mainWindow.once('ready-to-show', () => {
    mainWindow?.show();
  });

  mainWindow.on('closed', () => {
    mainWindow = null;
  });
}

// ---------------------------------------------------------------------------
// IPC handlers
// ---------------------------------------------------------------------------

function registerIpcHandlers(): void {
  ipcMain.on('window:setTitle', (_event, title: string) => {
    mainWindow?.setTitle(title);
  });
}

function unregisterIpcHandlers(): void {
  ipcMain.removeAllListeners('window:setTitle');
}

// ---------------------------------------------------------------------------
// App lifecycle
// ---------------------------------------------------------------------------

app.whenReady().then(async () => {
  registerIpcHandlers();

  try {
    const serverInfo = await startServer();
    createWindow(serverInfo.url);
  } catch (err) {
    console.error('[app] Failed to start server:', err);
    app.quit();
  }
});

app.on('window-all-closed', () => {
  stopServer();
  unregisterIpcHandlers();
  app.quit();
});

app.on('before-quit', () => {
  stopServer();
});
