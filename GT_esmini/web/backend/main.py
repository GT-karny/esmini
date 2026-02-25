"""FastAPI application entry point for GT_Sim Web API."""

from __future__ import annotations

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
    osi_stream,
    results,
    scenarios,
    scripts,
    simulations,
)
from GT_esmini.web.backend.db.database import init_db
from GT_esmini.web.backend.services.grpc_server import start_grpc_server


@asynccontextmanager
async def lifespan(app: FastAPI):
    # Startup
    await init_db()
    grpc_srv = await start_grpc_server(port=50051)
    yield
    # Shutdown
    await grpc_srv.stop(grace=5)


app = FastAPI(
    title="GT_Sim Web API",
    version="0.1.0",
    description="REST API for GT_Sim simulation management",
    lifespan=lifespan,
)

# CORS for frontend dev server
app.add_middleware(
    CORSMiddleware,
    allow_origins=["http://localhost:5173", "http://127.0.0.1:5173"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Register API routers
app.include_router(scenarios.router)
app.include_router(scripts.router)
app.include_router(controller_config.router)
app.include_router(simulations.router)
app.include_router(results.router)
app.include_router(config_api.router)
# WebSocket must be registered before the SPA catch-all route
app.include_router(osi_stream.router)


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
