/*
 * GT_esmini - Live VirtualDriver telemetry broadcasting
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2024 GT_esmini contributors
 */

#include "gt_esmini/io/GT_VirtualDriverReporter.hpp"
#include "logger.hpp"

#include <cstring>

namespace gt_esmini
{

GT_VirtualDriverReporter& GT_VirtualDriverReporter::Instance()
{
    static GT_VirtualDriverReporter instance;
    return instance;
}

GT_VirtualDriverReporter::GT_VirtualDriverReporter() = default;

GT_VirtualDriverReporter::~GT_VirtualDriverReporter()
{
    // Static destruction runs during process exit, where Winsock may already be
    // de-initialized (closesocket would fail with WSANOTINITIALISED, 10093).
    // Leave the socket to the OS; runtime cleanup goes through Close()/Init().
    udp_client_ = nullptr;
}

void GT_VirtualDriverReporter::Init(int udp_port, const std::string& target_ip)
{
    if (udp_client_)
    {
        delete udp_client_;
        udp_client_ = nullptr;
    }

    std::string ip = target_ip.empty() ? "127.0.0.1" : target_ip;
    udp_client_    = new UDPClient(static_cast<unsigned short>(udp_port), ip.c_str());

    if (udp_client_->GetStatus() != 0)
    {
        LOG_ERROR("GT_VirtualDriverReporter: Failed to init UDP on {}:{}", ip, udp_port);
        delete udp_client_;
        udp_client_  = nullptr;
        initialized_ = false;
    }
    else
    {
        LOG_INFO("GT_VirtualDriverReporter: UDP initialized on {}:{}", ip, udp_port);
        initialized_ = true;
    }
}

void GT_VirtualDriverReporter::Close()
{
    if (udp_client_)
    {
        delete udp_client_;
        udp_client_ = nullptr;
    }
    initialized_ = false;
}

void GT_VirtualDriverReporter::Send(const std::string& json)
{
    if (!initialized_ || !udp_client_ || json.empty())
    {
        return;
    }

    unsigned int payload_size = static_cast<unsigned int>(json.size());
    if (payload_size > MAX_UDP_DATA_SIZE)
    {
        // feature:F7 2026-07-27: this used to LOG_WARN and return. The warning
        // goes to the log, but the CONSUMER just saw a missing frame with no
        // in-band marker -- indistinguishable from "nothing happened". That is
        // the same failure shape as the instrumentation bugs found the same
        // day, so emit an explicit marker frame instead of a silent gap.
        // A consumer that counts frames still gets a frame; one that reads
        // fields finds telemetry_overflow=true and knows the run is degraded.
        ++overflow_count_;
        LOG_WARN(
            "GT_VirtualDriverReporter: telemetry frame {} bytes exceeds MAX_UDP_DATA_SIZE {} "
            "-- payload REPLACED by an overflow marker (total dropped this run: {}). "
            "Measurements from this run are incomplete.",
            payload_size,
            MAX_UDP_DATA_SIZE,
            overflow_count_);

        const std::string marker = "{\"telemetry_overflow\":true,\"dropped_bytes\":" +
                                   std::to_string(payload_size) + ",\"dropped_total\":" +
                                   std::to_string(overflow_count_) + "}";
        // The marker is a fixed, tiny object; it cannot itself overflow.
        udp_buf_.counter  = 0;
        udp_buf_.datasize = static_cast<unsigned int>(marker.size());
        std::memcpy(udp_buf_.data, marker.data(), marker.size());
        udp_client_->Send(reinterpret_cast<char*>(&udp_buf_),
                          sizeof(udp_buf_.counter) + sizeof(udp_buf_.datasize) +
                              static_cast<int>(marker.size()));
        return;
    }

    udp_buf_.counter  = 0;  // single-packet message
    udp_buf_.datasize = payload_size;
    std::memcpy(udp_buf_.data, json.data(), payload_size);

    udp_client_->Send(
        reinterpret_cast<char*>(&udp_buf_),
        sizeof(udp_buf_.counter) + sizeof(udp_buf_.datasize) + payload_size);
}

}  // namespace gt_esmini
