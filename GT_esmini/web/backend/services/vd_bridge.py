"""VD Bridge: receives VirtualDriver telemetry JSON and fans it out."""

from __future__ import annotations

from GT_esmini.web.backend.config import VD_LISTEN_PORT
from GT_esmini.web.backend.services.framed_udp_bridge import (
    BridgeRegistry,
    FramedUdpFanoutBridge,
)


class VdBridge(FramedUdpFanoutBridge):
    """Receives VirtualDriver telemetry JSON via UDP unicast from GT_Sim."""

    def __init__(
        self,
        listen_port: int = VD_LISTEN_PORT,
        bind_ip: str = "127.0.0.1",
        max_queue_size: int = 16,
    ) -> None:
        super().__init__(
            label="VD",
            listen_port=listen_port,
            bind_ip=bind_ip,
            max_queue_size=max_queue_size,
        )


_registry: BridgeRegistry[VdBridge] = BridgeRegistry(
    "VD",
    lambda listen_port: VdBridge(listen_port=listen_port),
)


def get_global_vd_bridge() -> VdBridge | None:
    """Return the always-on VD bridge started at server startup."""
    return _registry.get_global()


async def start_global_vd_bridge(listen_port: int = VD_LISTEN_PORT) -> VdBridge:
    """Start the global VD bridge at server startup."""
    return await _registry.start_global(listen_port)


async def stop_global_vd_bridge() -> None:
    """Stop the global VD bridge at server shutdown."""
    await _registry.stop_global()


def get_vd_bridge(job_id: str) -> VdBridge | None:
    return _registry.get_for_job(job_id)


async def start_vd_bridge(job_id: str, listen_port: int = VD_LISTEN_PORT) -> VdBridge:
    return await _registry.start_for_job(job_id, listen_port)


async def stop_vd_bridge(job_id: str) -> None:
    await _registry.stop_for_job(job_id)


async def stop_all_vd_bridges() -> int:
    return await _registry.stop_all()


__all__ = [
    "VdBridge",
    "get_global_vd_bridge",
    "get_vd_bridge",
    "start_global_vd_bridge",
    "start_vd_bridge",
    "stop_all_vd_bridges",
    "stop_global_vd_bridge",
    "stop_vd_bridge",
]
