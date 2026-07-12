/**
 * FastAPI server lifecycle manager.
 *
 * Dev mode:   spawns `python start_server.py`
 * Packaged:   spawns `server/gt_sim_web.exe`
 */

import { ChildProcess, spawn } from 'node:child_process';
import { existsSync } from 'node:fs';
import http from 'node:http';
import net from 'node:net';
import path from 'node:path';

const DEFAULT_HOST = '127.0.0.1';
// Preferred HTTP port. 8000 is a common port, so it may be taken by another app
// (or a previous GT_Sim instance). We scan upward from here for a free port
// rather than failing to launch. Overridable via GT_SIM_HTTP_PORT for parity
// with the backend config.
const PREFERRED_PORT = Number(process.env.GT_SIM_HTTP_PORT) || 8000;
const PORT_SCAN_RANGE = 20; // probe PREFERRED_PORT .. PREFERRED_PORT + 19
const MAX_START_ATTEMPTS = 3; // retry on the rare probe→bind race (TOCTOU)
const HEALTH_ENDPOINT = '/api/health';
const HEALTH_POLL_INTERVAL_MS = 300;
const HEALTH_TIMEOUT_MS = 30_000;

let serverProcess: ChildProcess | null = null;

interface ServerInfo {
  host: string;
  port: number;
  url: string;
}

/** Probe whether a TCP port is free to bind on `host`. */
function probePort(host: string, port: number): Promise<boolean> {
  return new Promise((resolve) => {
    const srv = net.createServer();
    srv.once('error', () => resolve(false));
    srv.once('listening', () => srv.close(() => resolve(true)));
    srv.listen(port, host);
  });
}

/**
 * Find a free port, scanning upward from PREFERRED_PORT.
 * `skip` holds ports already attempted this run (so retries pick a new one).
 */
async function findFreePort(host: string, skip: Set<number>): Promise<number> {
  for (let p = PREFERRED_PORT; p < PREFERRED_PORT + PORT_SCAN_RANGE; p++) {
    if (skip.has(p)) continue;
    if (await probePort(host, p)) return p;
  }
  throw new Error(
    `No free port in ${PREFERRED_PORT}..${PREFERRED_PORT + PORT_SCAN_RANGE - 1}`,
  );
}

/** Resolve how to launch the FastAPI server depending on environment. */
function resolveCommand(
  host: string,
  port: number,
): { command: string; args: string[]; cwd: string } {
  // Auto-detect packaged mode: if server/gt_sim_web.exe exists next to the
  // Electron executable, we're running from a distribution package.
  const exeDir = path.dirname(process.execPath);
  const packageRoot = process.env.GT_SIM_PACKAGE_ROOT ?? exeDir;
  const serverExe = path.join(packageRoot, 'server', 'gt_sim_web.exe');

  const isPackaged = existsSync(serverExe);

  if (!isPackaged) {
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
      args: [startScript, '--host', host, '--port', String(port)],
      cwd: repoRoot,
    };
  }

  // Packaged mode: exe is at <package_root>/server/gt_sim_web.exe
  return {
    command: serverExe,
    args: ['--host', host, '--port', String(port), '--no-browser'],
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

/** Spawn the server on a specific port and wait until its health check passes. */
async function launch(host: string, port: number): Promise<void> {
  const { command, args, cwd } = resolveCommand(host, port);

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
  await waitForHealth(host, port, HEALTH_TIMEOUT_MS);
  console.log('[server] Ready.');
}

/**
 * Start the FastAPI server on a free port and wait until it is ready.
 *
 * Scans upward from PREFERRED_PORT (8000) so a busy port no longer blocks
 * launch. Retries on the rare race where another process grabs the port
 * between our free-port probe and the server's bind.
 */
export async function startServer(): Promise<ServerInfo> {
  const host = DEFAULT_HOST;
  const tried = new Set<number>();
  let lastErr: unknown;

  for (let attempt = 1; attempt <= MAX_START_ATTEMPTS; attempt++) {
    const port = await findFreePort(host, tried);
    tried.add(port);
    try {
      await launch(host, port);
      return { host, port, url: `http://${host}:${port}` };
    } catch (err) {
      lastErr = err;
      console.error(
        `[server] Start attempt ${attempt}/${MAX_START_ATTEMPTS} on port ${port} failed:`,
        err instanceof Error ? err.message : err,
      );
      stopServer(); // tear down the failed process before retrying
    }
  }

  throw lastErr ?? new Error('Server failed to start');
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
