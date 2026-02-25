"""gRPC server for OSI GroundTruth and HostVehicleData streaming."""

from __future__ import annotations

import asyncio
import logging

import grpc
from google.protobuf import empty_pb2

from osi3.osi_groundtruth_pb2 import GroundTruth
from osi3.osi_hostvehicledata_pb2 import HostVehicleData

from GT_esmini.web.backend.grpc_gen.service_groundtruth_pb2_grpc import (
    GroundTruthServiceServicer,
    add_GroundTruthServiceServicer_to_server,
)
from GT_esmini.web.backend.grpc_gen.service_hostvehicledata_pb2_grpc import (
    HostVehicleDataServiceServicer,
    add_HostVehicleDataServiceServicer_to_server,
)
from GT_esmini.web.backend.services.osi_bridge import _bridges

logger = logging.getLogger(__name__)


class GroundTruthServiceImpl(GroundTruthServiceServicer):
    """Streams GroundTruth from any active OSI bridge to gRPC clients."""

    async def StreamGroundTruth(
        self,
        request: empty_pb2.Empty,
        context: grpc.aio.ServicerContext,
    ):
        bridge = _get_active_bridge()
        if bridge is None:
            context.set_code(grpc.StatusCode.UNAVAILABLE)
            context.set_details("No active simulation with OSI output")
            return

        sub_id, queue = bridge.subscribe_gt()
        logger.info("gRPC GroundTruth subscriber connected: %s", sub_id)
        try:
            while not context.cancelled():
                try:
                    raw = await asyncio.wait_for(queue.get(), timeout=1.0)
                except asyncio.TimeoutError:
                    # Check if bridge is still running
                    if not bridge.running:
                        return
                    continue

                gt = GroundTruth()
                gt.ParseFromString(raw)
                yield gt
        finally:
            bridge.unsubscribe_gt(sub_id)
            logger.info("gRPC GroundTruth subscriber disconnected: %s", sub_id)


class HostVehicleDataServiceImpl(HostVehicleDataServiceServicer):
    """Streams HostVehicleData from any active OSI bridge to gRPC clients."""

    async def StreamHostVehicleData(
        self,
        request: empty_pb2.Empty,
        context: grpc.aio.ServicerContext,
    ):
        bridge = _get_active_bridge()
        if bridge is None:
            context.set_code(grpc.StatusCode.UNAVAILABLE)
            context.set_details("No active simulation with OSI output")
            return

        sub_id, queue = bridge.subscribe_hvd()
        logger.info("gRPC HostVehicleData subscriber connected: %s", sub_id)
        try:
            while not context.cancelled():
                try:
                    raw = await asyncio.wait_for(queue.get(), timeout=1.0)
                except asyncio.TimeoutError:
                    if not bridge.running:
                        return
                    continue

                hvd = HostVehicleData()
                hvd.ParseFromString(raw)
                yield hvd
        finally:
            bridge.unsubscribe_hvd(sub_id)
            logger.info("gRPC HostVehicleData subscriber disconnected: %s", sub_id)


def _get_active_bridge():
    """Return the first active (running) bridge, or None."""
    for bridge in _bridges.values():
        if bridge.running:
            return bridge
    return None


async def start_grpc_server(port: int = 50051) -> grpc.aio.Server:
    """Start the gRPC server with both OSI services."""
    server = grpc.aio.server()
    add_GroundTruthServiceServicer_to_server(GroundTruthServiceImpl(), server)
    add_HostVehicleDataServiceServicer_to_server(HostVehicleDataServiceImpl(), server)
    listen_addr = f"[::]:{port}"
    server.add_insecure_port(listen_addr)
    await server.start()
    logger.info("gRPC OSI server started on %s", listen_addr)
    return server
