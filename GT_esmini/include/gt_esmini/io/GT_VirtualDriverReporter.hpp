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
    static constexpr int MAX_UDP_DATA_SIZE = 8192;
    struct
    {
        int          counter  = 0;
        unsigned int datasize = 0;
        char         data[MAX_UDP_DATA_SIZE];
    } udp_buf_;
};

}  // namespace gt_esmini
