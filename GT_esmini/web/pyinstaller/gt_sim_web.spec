# -*- mode: python ; coding: utf-8 -*-
"""PyInstaller spec for GT_Sim Web server.

Usage:
    cd <repo_root>
    DriverScript\\.venv\\Scripts\\python.exe -m PyInstaller GT_esmini/web/pyinstaller/gt_sim_web.spec

The spec builds in --onedir mode, producing:
    GT_esmini/web/pyinstaller/dist/gt_sim_web/
        gt_sim_web.exe
        _internal/
"""

from pathlib import Path

# Resolve paths (spec file is at GT_esmini/web/pyinstaller/)
SPEC_DIR = Path(SPECPATH)
REPO_ROOT = SPEC_DIR.parents[2]  # -> GT_esmini/web -> GT_esmini -> repo root
FRONTEND_DIST = REPO_ROOT / "GT_esmini" / "web" / "frontend" / "dist"

a = Analysis(
    [str(SPEC_DIR / "gt_sim_web_entry.py")],
    pathex=[
        str(REPO_ROOT),
        str(REPO_ROOT / "scripts"),
        str(REPO_ROOT / "DriverScript"),
    ],
    binaries=[],
    datas=[
        # Bundle React SPA
        (str(FRONTEND_DIST), "frontend/dist"),
    ],
    hiddenimports=[
        # --- uvicorn (dynamic protocol/loop loading) ---
        "uvicorn.logging",
        "uvicorn.loops",
        "uvicorn.loops.auto",
        "uvicorn.loops.asyncio",
        "uvicorn.protocols",
        "uvicorn.protocols.http",
        "uvicorn.protocols.http.auto",
        "uvicorn.protocols.http.h11_impl",
        "uvicorn.protocols.http.httptools_impl",
        "uvicorn.protocols.websockets",
        "uvicorn.protocols.websockets.auto",
        "uvicorn.protocols.websockets.wsproto_impl",
        "uvicorn.protocols.websockets.websockets_impl",
        "uvicorn.lifespan",
        "uvicorn.lifespan.on",
        "uvicorn.lifespan.off",
        # --- FastAPI / Starlette ---
        "fastapi",
        "fastapi.middleware",
        "fastapi.middleware.cors",
        "fastapi.staticfiles",
        "fastapi.responses",
        "starlette.responses",
        "starlette.routing",
        "starlette.middleware",
        "starlette.middleware.cors",
        # --- Pydantic ---
        "pydantic",
        "pydantic.deprecated",
        "pydantic.deprecated.decorator",
        "pydantic._internal",
        "pydantic._internal._core_utils",
        "pydantic._internal._generate_schema",
        "pydantic._internal._validators",
        "pydantic._internal._fields",
        "pydantic._internal._model_construction",
        "pydantic._internal._decorators",
        "pydantic._internal._config",
        "annotated_types",
        "typing_extensions",
        # --- Database ---
        "aiosqlite",
        "sqlite3",
        # --- YAML ---
        "yaml",
        # --- Multipart (FastAPI form handling) ---
        "multipart",
        # --- Async I/O ---
        "anyio._backends._asyncio",
        # --- Backend package (loaded by uvicorn via string import) ---
        "GT_esmini",
        "GT_esmini.web",
        "GT_esmini.web.backend",
        "GT_esmini.web.backend.config",
        "GT_esmini.web.backend.main",
        "GT_esmini.web.backend.db",
        "GT_esmini.web.backend.db.database",
        "GT_esmini.web.backend.api",
        "GT_esmini.web.backend.api.scenarios",
        "GT_esmini.web.backend.api.scripts",
        "GT_esmini.web.backend.api.simulations",
        "GT_esmini.web.backend.api.results",
        "GT_esmini.web.backend.api.config_api",
        "GT_esmini.web.backend.api.controller_config",
        "GT_esmini.web.backend.models",
        "GT_esmini.web.backend.models.scenario",
        "GT_esmini.web.backend.models.simulation",
        "GT_esmini.web.backend.models.result",
        "GT_esmini.web.backend.models.script",
        "GT_esmini.web.backend.services",
        "GT_esmini.web.backend.services.scenario_service",
        "GT_esmini.web.backend.services.script_service",
        "GT_esmini.web.backend.services.simulation_runner",
        "GT_esmini.web.backend.services.result_service",
        "GT_esmini.web.backend.services.osi_bridge",
        "GT_esmini.web.backend.services.grpc_server",
        "GT_esmini.web.backend.api.osi_stream",
        "GT_esmini.web.backend.grpc_gen",
        "GT_esmini.web.backend.grpc_gen.service_groundtruth_pb2",
        "GT_esmini.web.backend.grpc_gen.service_groundtruth_pb2_grpc",
        "GT_esmini.web.backend.grpc_gen.service_hostvehicledata_pb2",
        "GT_esmini.web.backend.grpc_gen.service_hostvehicledata_pb2_grpc",
        # --- gRPC ---
        "grpc",
        "grpc._cython",
        "grpc._cython.cygrpc",
        "grpc.aio",
        "grpc.aio._server",
        # --- OSI protobuf messages ---
        "osi3",
        "osi3.osi_groundtruth_pb2",
        "osi3.osi_hostvehicledata_pb2",
        "osi3.osi_common_pb2",
        "google.protobuf",
        "google.protobuf.empty_pb2",
        # --- External scripts (resolved via sys.path at runtime) ---
        "scenario_generator",
        "dat",
        "runtime_api",
    ],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[
        "tkinter",
        "matplotlib",
        "numpy",
        "PySide6",
        "PIL",
        "logidrivepy",
        "pytest",
        "IPython",
        "notebook",
        "sphinx",
    ],
    noarchive=False,
)

pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,  # --onedir mode
    name="gt_sim_web",
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=False,
    console=True,
)

coll = COLLECT(
    exe,
    a.binaries,
    a.datas,
    strip=False,
    upx=False,
    upx_exclude=[],
    name="gt_sim_web",
)
