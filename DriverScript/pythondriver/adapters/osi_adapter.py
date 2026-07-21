"""Adapter for OSI GroundTruth payloads passed from embedded bridge."""

from __future__ import annotations

from typing import Optional


class OSIAdapter:
    @staticmethod
    def parse_ground_truth(ground_truth_bytes: bytes):
        """Parse serialized OSI GroundTruth protobuf bytes."""
        if not ground_truth_bytes:
            return None

        from osi3.osi_groundtruth_pb2 import GroundTruth

        gt = GroundTruth()
        gt.ParseFromString(ground_truth_bytes)
        return gt

    @staticmethod
    def extract_host_id(ground_truth) -> Optional[int]:
        if ground_truth is None:
            return None
        if not ground_truth.host_vehicle_id.value:
            return None
        return int(ground_truth.host_vehicle_id.value)
