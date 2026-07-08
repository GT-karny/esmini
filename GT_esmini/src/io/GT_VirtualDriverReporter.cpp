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
    Close();
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
        LOG_WARN("GT_VirtualDriverReporter: JSON too large ({} bytes), skipping", payload_size);
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
