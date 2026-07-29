"""SV Bridge: receives scenario variable JSON and redistributes it."""

from __future__ import annotations
import logging
import socket

from GT_esmini.web.backend.config import (
    SV_LISTEN_PORT,
    SV_MULTICAST_GROUP,
    SV_MULTICAST_PORT,
)
from GT_esmini.web.backend.services.framed_udp_bridge import (
    BridgeRegistry,
    FramedUdpFanoutBridge,
)

logger = logging.getLogger(__name__)


class SvBridge(FramedUdpFanoutBridge):
    """Receives scenario variable JSON from GT_Sim and relays it to clients."""

    def __init__(
        self,
        listen_port: int = SV_LISTEN_PORT,
        bind_ip: str = "127.0.0.1",
        multicast_group: str = SV_MULTICAST_GROUP,
        multicast_port: int = SV_MULTICAST_PORT,
        max_queue_size: int = 16,
    ) -> None:
        super().__init__(
            label="SV",
            listen_port=listen_port,
            bind_ip=bind_ip,
            max_queue_size=max_queue_size,
        )
        self._multicast_group = multicast_group
        self._multicast_port = multicast_port
        self._mc_sock: socket.socket | None = None

    def _before_start(self) -> None:
        try:
            mc = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
            mc.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 2)
            self._mc_sock = mc
        except OSError as e:
            logger.warning("SV Bridge: failed to create multicast socket: %s", e)
            self._mc_sock = None

    def _after_stop(self) -> None:
        if self._mc_sock is not None:
            self._mc_sock.close()
            self._mc_sock = None

    def _relay_payload(self, payload: bytes) -> None:
        if self._mc_sock is None:
            return

        try:
            self._mc_sock.sendto(payload, (self._multicast_group, self._multicast_port))
        except OSError as e:
            logger.warning("SV multicast send error: %s", e)

    def _log_started(self) -> None:
        logger.info(
            "SV Bridge started (listen=%s:%d, multicast=%s:%d)",
            self._bind_ip,
            self._listen_port,
            self._multicast_group,
            self._multicast_port,
        )


_registry: BridgeRegistry[SvBridge] = BridgeRegistry(
    "SV",
    lambda listen_port: SvBridge(listen_port=listen_port),
)


async def start_global_sv_bridge(listen_port: int = SV_LISTEN_PORT) -> SvBridge:
    """Start the global SV bridge at server startup."""
    return await _registry.start_global(listen_port)


def get_sv_bridge(job_id: str) -> SvBridge | None:
    return _registry.get_for_job(job_id)


async def start_sv_bridge(
    job_id: str,
    listen_port: int = SV_LISTEN_PORT,
) -> SvBridge:
    return await _registry.start_for_job(job_id, listen_port)


async def stop_sv_bridge(job_id: str) -> None:
    await _registry.stop_for_job(job_id)


async def stop_all_sv_bridges() -> int:
    return await _registry.stop_all()


__all__ = [
    "SvBridge",
    "get_sv_bridge",
    "start_global_sv_bridge",
    "start_sv_bridge",
    "stop_all_sv_bridges",
    "stop_sv_bridge",
]
