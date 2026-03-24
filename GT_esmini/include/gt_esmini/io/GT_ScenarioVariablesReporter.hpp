/*
 * GT_esmini - Extended esmini with Scenario Variables Broadcasting
 * https://github.com/esmini/esmini
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
 * @brief Singleton that reads OpenSCENARIO VariableDeclarations each step
 *        and broadcasts them as JSON via UDP.
 *
 * Packet format (same header as esmini OSI):
 *   [counter: int32][size: uint32][json_bytes]
 *
 * JSON payload:
 *   {"sim_time":1.23,"variables":{"flag":true,"speed":60.0}}
 */
class GT_ScenarioVariablesReporter
{
public:
    static GT_ScenarioVariablesReporter& Instance();

    /**
     * Initialize UDP sender.
     * @param udp_port  Target port (default 48200)
     * @param target_ip Target IP   (default "127.0.0.1")
     */
    void Init(int udp_port = 48200, const std::string& target_ip = "127.0.0.1");

    /**
     * Read all scenario variables via SE_GetVariable*() and send as JSON.
     * Call once per simulation step (after SE_Step / GT_Step).
     */
    void Update();

    /**
     * Release UDP resources.
     */
    void Close();

    bool IsInitialized() const { return initialized_; }

private:
    GT_ScenarioVariablesReporter();
    ~GT_ScenarioVariablesReporter();
    GT_ScenarioVariablesReporter(const GT_ScenarioVariablesReporter&) = delete;
    GT_ScenarioVariablesReporter& operator=(const GT_ScenarioVariablesReporter&) = delete;

    UDPClient* udp_client_ = nullptr;
    bool initialized_ = false;

    // Reusable string buffer to avoid per-frame allocation
    std::string json_buf_;

    // UDP send buffer (same layout as esmini OSI packets)
    static constexpr int MAX_UDP_DATA_SIZE = 8192;
    struct
    {
        int counter = 0;
        unsigned int datasize = 0;
        char data[MAX_UDP_DATA_SIZE];
    } udp_buf_;
};

} // namespace gt_esmini
