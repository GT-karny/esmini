/**
 * Dev mode launcher for GT_Sim Desktop.
 *
 * 1. Bundle main process + preload with esbuild
 * 2. Launch Electron (which spawns FastAPI server internally)
 *
 * Usage: node scripts/dev.mjs
 */

import { spawn } from 'node:child_process';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = dirname(fileURLToPath(import.meta.url));
const electronDir = resolve(__dirname, '..');
const repoRoot = resolve(electronDir, '..', '..', '..');

// Step 1: Build
console.log('[dev] Building main process...');
const buildProc = spawn('node', ['scripts/bundle.mjs'], {
  cwd: electronDir,
  stdio: 'inherit',
  shell: true,
});

buildProc.on('exit', (code) => {
  if (code !== 0) {
    console.error(`[dev] Build failed (exit ${code})`);
    process.exit(code ?? 1);
  }

  // Step 2: Launch Electron
  console.log('[dev] Starting Electron...');
  const electronBin = resolve(electronDir, 'node_modules', '.bin', 'electron');
  const electronProc = spawn(electronBin, ['.'], {
    cwd: electronDir,
    stdio: 'inherit',
    shell: true,
    env: (() => {
      const env = { ...process.env, GT_SIM_REPO_ROOT: repoRoot };
      // VS Code sets ELECTRON_RUN_AS_NODE=1, which prevents Electron from
      // initializing its browser environment. Must be deleted entirely.
      delete env.ELECTRON_RUN_AS_NODE;
      return env;
    })(),
  });

  electronProc.on('exit', (exitCode) => {
    console.log(`[dev] Electron exited (code=${exitCode})`);
    process.exit(exitCode ?? 0);
  });
});
