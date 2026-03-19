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
    frame: false,
    titleBarStyle: 'hidden',
    show: false,
    webPreferences: {
      preload: path.join(__dirname, '..', 'preload', 'index.js'),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: false,
    },
  });

  mainWindow.loadURL(serverUrl);

  // Forward maximize/unmaximize events to renderer for titlebar button state
  mainWindow.on('maximize', () => {
    mainWindow?.webContents.send('window:maximized');
  });
  mainWindow.on('unmaximize', () => {
    mainWindow?.webContents.send('window:unmaximized');
  });

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

  ipcMain.on('window:minimize', () => {
    mainWindow?.minimize();
  });

  ipcMain.on('window:maximize', () => {
    if (mainWindow?.isMaximized()) {
      mainWindow.unmaximize();
    } else {
      mainWindow?.maximize();
    }
  });

  ipcMain.on('window:close', () => {
    mainWindow?.close();
  });

  ipcMain.handle('window:isMaximized', () => {
    return mainWindow?.isMaximized() ?? false;
  });
}

function unregisterIpcHandlers(): void {
  ipcMain.removeAllListeners('window:setTitle');
  ipcMain.removeAllListeners('window:minimize');
  ipcMain.removeAllListeners('window:maximize');
  ipcMain.removeAllListeners('window:close');
  ipcMain.removeHandler('window:isMaximized');
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
