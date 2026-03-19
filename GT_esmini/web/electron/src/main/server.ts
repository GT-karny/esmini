/**
 * FastAPI server lifecycle manager.
 *
 * Dev mode:   spawns `python start_server.py`
 * Packaged:   spawns `server/gt_sim_web.exe`
 */

import { ChildProcess, spawn } from 'node:child_process';
import { existsSync } from 'node:fs';
import http from 'node:http';
import path from 'node:path';

const DEFAULT_HOST = '127.0.0.1';
const DEFAULT_PORT = 8000;
const HEALTH_ENDPOINT = '/api/health';
const HEALTH_POLL_INTERVAL_MS = 300;
const HEALTH_TIMEOUT_MS = 30_000;

let serverProcess: ChildProcess | null = null;

interface ServerInfo {
  host: string;
  port: number;
  url: string;
}

/** Resolve how to launch the FastAPI server depending on environment. */
function resolveCommand(): { command: string; args: string[]; cwd: string } {
  const isDev = !process.env.GT_SIM_PACKAGED;

  if (isDev) {
    // Dev mode: use venv Python to run start_server.py
    // __dirname is dist/main/ inside electron dir.
    // Repo root: dist/main -> electron -> web -> GT_esmini -> esmini (4 levels)
    const repoRoot =
      process.env.GT_SIM_REPO_ROOT ??
      path.resolve(__dirname, '..', '..', '..', '..', '..');

    const venvPython = path.join(
      repoRoot,
      'DriverScript',
      '.venv',
      'Scripts',
      'python.exe',
    );
    const startScript = path.join(
      repoRoot,
      'GT_esmini',
      'web',
      'start_server.py',
    );

    if (!existsSync(venvPython)) {
      throw new Error(`Python venv not found: ${venvPython}`);
    }
    if (!existsSync(startScript)) {
      throw new Error(`Start script not found: ${startScript}`);
    }

    return {
      command: venvPython,
      args: [startScript, '--host', DEFAULT_HOST, '--port', String(DEFAULT_PORT)],
      cwd: repoRoot,
    };
  }

  // Packaged mode: exe is at <package_root>/server/gt_sim_web.exe
  const exeDir = path.dirname(process.execPath);
  const packageRoot = process.env.GT_SIM_PACKAGE_ROOT ?? exeDir;
  const serverExe = path.join(packageRoot, 'server', 'gt_sim_web.exe');

  if (!existsSync(serverExe)) {
    throw new Error(`Server executable not found: ${serverExe}`);
  }

  return {
    command: serverExe,
    args: ['--host', DEFAULT_HOST, '--port', String(DEFAULT_PORT), '--no-browser'],
    cwd: packageRoot,
  };
}

/** Poll the health endpoint until the server responds 200. */
function waitForHealth(host: string, port: number, timeoutMs: number): Promise<void> {
  return new Promise((resolve, reject) => {
    const deadline = Date.now() + timeoutMs;

    const poll = () => {
      if (Date.now() > deadline) {
        reject(new Error(`Server did not become ready within ${timeoutMs}ms`));
        return;
      }

      const req = http.get(
        { hostname: host, port, path: HEALTH_ENDPOINT, timeout: 2000 },
        (res) => {
          if (res.statusCode === 200) {
            resolve();
          } else {
            setTimeout(poll, HEALTH_POLL_INTERVAL_MS);
          }
          res.resume(); // drain
        },
      );

      req.on('error', () => {
        setTimeout(poll, HEALTH_POLL_INTERVAL_MS);
      });

      req.on('timeout', () => {
        req.destroy();
        setTimeout(poll, HEALTH_POLL_INTERVAL_MS);
      });
    };

    poll();
  });
}

/** Start the FastAPI server and wait until it is ready. */
export async function startServer(): Promise<ServerInfo> {
  const { command, args, cwd } = resolveCommand();

  console.log(`[server] Starting: ${command} ${args.join(' ')}`);
  console.log(`[server] CWD: ${cwd}`);

  serverProcess = spawn(command, args, {
    cwd,
    stdio: ['ignore', 'pipe', 'pipe'],
    windowsHide: true,
  });

  // Forward server stdout/stderr to Electron console
  serverProcess.stdout?.on('data', (data: Buffer) => {
    process.stdout.write(`[server] ${data.toString()}`);
  });
  serverProcess.stderr?.on('data', (data: Buffer) => {
    process.stderr.write(`[server] ${data.toString()}`);
  });

  serverProcess.on('error', (err) => {
    console.error('[server] Failed to start:', err.message);
  });

  serverProcess.on('exit', (code, signal) => {
    console.log(`[server] Exited (code=${code}, signal=${signal})`);
    serverProcess = null;
  });

  // Wait for server to respond
  console.log('[server] Waiting for health check...');
  await waitForHealth(DEFAULT_HOST, DEFAULT_PORT, HEALTH_TIMEOUT_MS);
  console.log('[server] Ready.');

  return {
    host: DEFAULT_HOST,
    port: DEFAULT_PORT,
    url: `http://${DEFAULT_HOST}:${DEFAULT_PORT}`,
  };
}

/** Kill the server process tree. */
export function stopServer(): void {
  if (!serverProcess) return;

  console.log(`[server] Stopping (pid=${serverProcess.pid})...`);

  try {
    // On Windows, use taskkill /T to kill the process tree
    // (uvicorn may spawn worker processes)
    if (process.platform === 'win32' && serverProcess.pid) {
      spawn('taskkill', ['/pid', String(serverProcess.pid), '/T', '/F'], {
        stdio: 'ignore',
        windowsHide: true,
      });
    } else {
      serverProcess.kill('SIGTERM');
    }
  } catch {
    // Process may have already exited
  }

  serverProcess = null;
}

/** Check if the server process is still running. */
export function isServerRunning(): boolean {
  return serverProcess !== null && serverProcess.exitCode === null;
}
