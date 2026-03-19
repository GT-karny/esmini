/**
 * Bundle the Electron main process and preload script with esbuild.
 *
 * Usage: node scripts/bundle.mjs
 */

import { build } from 'esbuild';
import { rmSync } from 'node:fs';

// Clean previous build
rmSync('dist', { recursive: true, force: true });

/** Packages that must stay external (resolved at runtime from node_modules). */
const external = [
  'electron',
];

// Main process — CJS bundle (Electron's Node.js has CJS/ESM interop issues with 'electron' package)
await build({
  entryPoints: ['src/main/index.ts'],
  bundle: true,
  platform: 'node',
  target: 'node20',
  format: 'cjs',
  outfile: 'dist/main/index.js',
  sourcemap: true,
  external,
});

// Preload script — CommonJS bundle (required by Electron)
await build({
  entryPoints: ['src/preload/index.ts'],
  bundle: true,
  platform: 'node',
  target: 'node20',
  format: 'cjs',
  outfile: 'dist/preload/index.js',
  sourcemap: true,
  external: ['electron'],
});

console.log('Build complete: dist/main/index.js, dist/preload/index.js');
