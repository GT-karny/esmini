"""FastAPI application entry point for GT_Sim Web API."""

from __future__ import annotations

import asyncio
import logging
import os
import sys
from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import FastAPI, Request
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles

from GT_esmini.web.backend.api import (
    config_api,
    controller_config,
    manual_drive_api,
    osi_stream,
    preset_stream,
    sv_stream,
    projects,
    results,
    roads,
    scenarios,
    scripts,
    simulations,
)
from GT_esmini.web.backend.config import GRPC_PORT
from GT_esmini.web.backend.db.database import init_db
from GT_esmini.web.backend.services.grpc_server import start_grpc_server

_logger = logging.getLogger(__name__)


@asynccontextmanager
async def lifespan(app: FastAPI):
    # --- Startup ---
    await init_db()  # Also cleans stale DB entries from previous crashes

    # Register built-in project (resources/)
    from GT_esmini.web.backend.services.project_service import ensure_builtin_project
    await ensure_builtin_project()

    # Clean up expired temp files from previous sessions
    from GT_esmini.web.backend.services.scenario_service import cleanup_expired_scenarios
    from GT_esmini.web.backend.services.road_service import cleanup_expired_roads
    expired_s = cleanup_expired_scenarios()
    expired_r = cleanup_expired_roads()
    if expired_s or expired_r:
        _logger.info("Cleaned up %d expired temp scenario(s) and %d road(s)", expired_s, expired_r)

    # Start always-on SV bridge (UDP listener for scenario variables)
    from GT_esmini.web.backend.services.sv_bridge import start_global_sv_bridge
    try:
        await start_global_sv_bridge()
    except Exception as e:
        _logger.warning("Global SV Bridge failed to start: %s — will use per-job bridges", e)

    grpc_srv = None
    try:
        grpc_srv = await start_grpc_server(port=GRPC_PORT)
    except Exception as e:
        _logger.warning("gRPC server failed to start (port %s): %s — continuing without gRPC", GRPC_PORT, e)

    async def _periodic_temp_cleanup():
        while True:
            await asyncio.sleep(900)  # 15 minutes
            try:
                s = cleanup_expired_scenarios()
                r = cleanup_expired_roads()
                if s or r:
                    _logger.info("Periodic cleanup: %d scenario(s), %d road(s)", s, r)
            except Exception:
                pass

    cleanup_task = asyncio.create_task(_periodic_temp_cleanup())

    _logger.info("GT_Sim Web server started")
    yield
    # --- Shutdown ---
    # Must complete within ~4s on Windows CTRL_CLOSE_EVENT
    cleanup_task.cancel()
    _logger.info("Shutting down GT_Sim Web server...")

    # Phase 1: Kill all running GT_Sim subprocesses (highest priority)
    from GT_esmini.web.backend.services.simulation_runner import kill_all_running

    kill_count = await asyncio.to_thread(kill_all_running)
    if kill_count:
        _logger.info("Killed %d running GT_Sim subprocess(es)", kill_count)

    # Phase 2: Stop all OSI bridges (releases UDP ports)
    from GT_esmini.web.backend.services.osi_bridge import stop_all_bridges
    from GT_esmini.web.backend.services.sv_bridge import stop_all_sv_bridges

    bridge_count = await stop_all_bridges()
    if bridge_count:
        _logger.info("Stopped %d OSI bridge(s)", bridge_count)

    sv_count = await stop_all_sv_bridges()
    if sv_count:
        _logger.info("Stopped %d SV bridge(s)", sv_count)

    # Phase 3: Stop gRPC server (reduced grace for Windows deadline)
    if grpc_srv is not None:
        await grpc_srv.stop(grace=2)

    # Stop filesystem watchers (preset YAML observers)
    from GT_esmini.web.backend.services.preset_watcher import get_preset_watcher_manager
    try:
        await asyncio.to_thread(get_preset_watcher_manager().shutdown)
    except Exception:
        _logger.warning("Preset watcher shutdown failed", exc_info=True)

    # Phase 4: Mark any remaining running jobs as failed in DB
    await _mark_stale_jobs()

    _logger.info("Shutdown complete")


async def _mark_stale_jobs() -> None:
    """Mark any still-running DB jobs as failed during shutdown."""
    from GT_esmini.web.backend.db.database import get_db

    db = await get_db()
    try:
        cursor = await db.execute(
            """UPDATE simulations
               SET status = 'failed',
                   error_message = 'Server shut down while job was running',
                   completed_at = datetime('now'),
                   pid = NULL
               WHERE status = 'running'"""
        )
        await db.commit()
        if cursor.rowcount and cursor.rowcount > 0:
            _logger.info("Marked %d running job(s) as failed", cursor.rowcount)
    finally:
        await db.close()


app = FastAPI(
    title="GT_Sim Web API",
    version="0.1.0",
    description="REST API for GT_Sim simulation management",
    lifespan=lifespan,
)

# CORS for frontend dev server + external editors (via GT_SIM_CORS_ORIGINS env var)
_cors_origins = ["http://localhost:5173", "http://127.0.0.1:5173"]
_extra_origins = os.environ.get("GT_SIM_CORS_ORIGINS", "")
if _extra_origins:
    _cors_origins.extend(o.strip() for o in _extra_origins.split(",") if o.strip())

app.add_middleware(
    CORSMiddleware,
    allow_origins=_cors_origins,
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Register API routers
app.include_router(projects.router)
app.include_router(scenarios.router)
app.include_router(scripts.router)
app.include_router(controller_config.router)
app.include_router(manual_drive_api.router)
app.include_router(simulations.router)
app.include_router(results.router)
app.include_router(config_api.router)
app.include_router(roads.router)
# WebSocket must be registered before the SPA catch-all route
app.include_router(osi_stream.router)
app.include_router(sv_stream.router)
app.include_router(preset_stream.router)


@app.get("/api/health")
async def health_check():
    """Health check endpoint."""
    return {"status": "ok"}


# Serve frontend static files (production build)
if getattr(sys, "frozen", False):
    # PyInstaller: frontend is bundled via --add-data
    _FRONTEND_DIST = Path(sys._MEIPASS) / "frontend" / "dist"  # type: ignore[attr-defined]
else:
    _FRONTEND_DIST = Path(__file__).resolve().parent.parent / "frontend" / "dist"
if _FRONTEND_DIST.is_dir():
    app.mount("/assets", StaticFiles(directory=str(_FRONTEND_DIST / "assets")), name="static")

    @app.get("/{full_path:path}")
    async def serve_spa(request: Request, full_path: str):
        """Serve React SPA for all non-API routes."""
        # Try static file first
        file_path = _FRONTEND_DIST / full_path
        if file_path.is_file() and file_path.resolve().is_relative_to(_FRONTEND_DIST.resolve()):
            return FileResponse(str(file_path))
        # Fall back to index.html for SPA routing
        return FileResponse(str(_FRONTEND_DIST / "index.html"))


if __name__ == "__main__":
    import uvicorn

    uvicorn.run(
        "GT_esmini.web.backend.main:app",
        host="127.0.0.1",
        port=8000,
        reload=True,
    )
