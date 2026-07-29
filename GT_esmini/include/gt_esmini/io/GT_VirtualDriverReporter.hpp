/*
 * GT_esmini - Live VirtualDriver telemetry broadcasting
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2024 GT_esmini contributors
 */

#pragma once

#include "UDP.hpp"
#include <string>

namespace gt_esmini
{

/**
 * @brief Singleton that broadcasts VirtualDriver telemetry as JSON via UDP.
 *
 * Pure transport: the caller (core) builds the JSON (see ToJson() in
 * control/virtualdriver/VirtualDriverTelemetryJson.hpp) and hands it to Send(),
 * so this reporter has no dependency on the control layer. Mirrors
 * GT_ScenarioVariablesReporter so the backend SV bridge pattern can be reused.
 *
 * Packet format (same header as esmini OSI / SV):
 *   [counter: int32][size: uint32][json_bytes]
 */
class GT_VirtualDriverReporter
{
public:
    static GT_VirtualDriverReporter& Instance();

    /**
     * Initialize UDP sender.
     * @param udp_port  Target port (default 48202)
     * @param target_ip Target IP   (default "127.0.0.1")
     */
    void Init(int udp_port = 48202, const std::string& target_ip = "127.0.0.1");

    /**
     * Send a pre-serialized telemetry JSON string. No-op if not initialized
     * or the payload is empty. Call once per step (after GT_Step), only when a
     * VirtualDriver controller is present.
     */
    void Send(const std::string& json);

    /** Release UDP resources. */
    void Close();

    bool IsInitialized() const { return initialized_; }

private:
    GT_VirtualDriverReporter();
    ~GT_VirtualDriverReporter();
    GT_VirtualDriverReporter(const GT_VirtualDriverReporter&)            = delete;
    GT_VirtualDriverReporter& operator=(const GT_VirtualDriverReporter&) = delete;

    UDPClient* udp_client_ = nullptr;
    bool       initialized_ = false;

    // UDP send buffer (same layout as esmini OSI / SV packets)
    //
    // feature:F7 2026-07-27: raised 8192 -> 16384. The re-anchor instrument
    // added ~290 bytes to ffb.gates, which pushed entity-rich scenarios
    // (highway_merge: 8321 bytes) past the old cap -- every such frame was
    // dropped, and a consumer counting frames saw an unmarked gap.
    //
    // RAISED HERE ONLY, deliberately. GT_ScenarioVariablesReporter and
    // GT_HostVehicleReporter keep 8192 because their consumers hardcode an
    // 8208-byte (8192 + 8-byte header) recvfrom buffer -- verified 2026-07-27
    // in scripts/verification/{udp_osi_common,gt_sim_test,generate_baseline,
    // test_hostvehicledata}.py, scripts/udp_driver/udp_osi_common.py,
    // DriverScript/realdriver/udp_common.py and web/backend/services/
    // vd_metrics.py. Raising those without raising every one of those buffers
    // truncates datagrams silently at the receiver.
    //
    // The VD telemetry port (48202) is safe: its only consumers are
    // web/backend/services/vd_bridge.py (asyncio DatagramProtocol, 256 KiB)
    // and scripts/vd_override_observer.py (recvfrom 65535).
    static constexpr int MAX_UDP_DATA_SIZE = 16384;
    struct
    {
        int          counter  = 0;
        unsigned int datasize = 0;
        char         data[MAX_UDP_DATA_SIZE];
    } udp_buf_;

    // Overflow accounting. Raising the cap buys headroom; it does not stop
    // the next field addition from silently re-crossing it. An oversized
    // frame is now REPLACED by a marker frame (see Send()) so a consumer
    // sees an explicit "this frame was dropped" record instead of a gap.
    unsigned long long overflow_count_ = 0;
};

}  // namespace gt_esmini
