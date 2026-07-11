# GT_Sim Web Frontend

React SPA for the GT_esmini web UI: project/scenario browsing, simulation
launching, live 2D monitoring (OSI GroundTruth / VirtualDriver telemetry /
scenario variables over WebSocket), and the VirtualDriver verification
workflows (replay + human annotation).

## Stack

- React 19 + TypeScript, built with Vite 7
- Tailwind CSS 4 (via `@tailwindcss/vite`) + `@osce/theme-apex`
  (local design-system package at `../../packages/theme-apex`, `file:` dependency)
- TanStack Query (server state) + React Router 7
- ESLint 9 flat config (`eslint.config.js`) with `typescript-eslint`,
  `react-hooks` (v7, includes the React Compiler-era rules) and `react-refresh`

## Prerequisites

- Node.js `^20.19.0 || >=22.12.0` (Vite 7 requirement)
- The FastAPI backend for API/WS calls (see `../backend/`, started via
  `../start_server.py` — serves on `http://127.0.0.1:8000` by default)

## Development

```sh
npm install
npm run dev        # Vite dev server; proxies /api and /ws to 127.0.0.1:8000
```

Start the backend first (from the repo root, using the project venv — never
bare `python`):

```sh
DriverScript/.venv/Scripts/python.exe GT_esmini/web/start_server.py --reload
```

`predev`/`prebuild` automatically install + build `@osce/theme-apex` before
Vite runs, so a clean checkout works without extra steps.

## Build / checks

```sh
npm run lint       # ESLint, must be 0 errors
npm run build      # tsc -b (type check) + vite build -> dist/
npm run preview    # serve the production build locally
```

The production build is served by the FastAPI backend itself: it mounts
`frontend/dist/` and falls back to `index.html` for SPA routes (see
`backend/main.py`). In the packaged EXE (PyInstaller), `dist/` is bundled via
`--add-data`. The Electron desktop shell (`../electron/`) wraps the same
served UI; Electron-only features are gated on `window.electronAPI`
(`src/lib/electron.ts`).

## Source layout

```
src/
  api/            REST client + types (client.ts), SimulationRequest builder
  components/     shared components (LiveSceneView 2D scene, run form, panels)
    project/      project page panels (scenario list/detail, execution, live monitor)
    simulation/   run-form sections (controller, advanced settings, overrides)
    verification/ VirtualDriver replay/annotate panels + replay hooks
    ui/           small presentational primitives (Button, Table, Input, ...)
  hooks/          data hooks; WebSocket streams share useWebSocketStream
                  (useOsiStream / useSvStream / useVdStream)
  pages/          route components (see src/App.tsx for the route table)
  lib/            non-component utilities (Electron bridge)
```

Routes: `/` (projects), `/projects/:projectId`, `/simulations`,
`/simulations/:jobId`, `/verification` (replay), `/verification/annotate`,
and the standalone live window `/live/vd/:jobId` (opened via `window.open`,
no nav shell).
