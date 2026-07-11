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

#include "gt_esmini/io/GT_ScenarioVariablesReporter.hpp"
#include "CommonMini.hpp"
#include "logger.hpp"
#include "esminiLib.hpp"

#include <cstring>
#include <cstdio>

namespace gt_esmini
{

GT_ScenarioVariablesReporter& GT_ScenarioVariablesReporter::Instance()
{
    static GT_ScenarioVariablesReporter instance;
    return instance;
}

GT_ScenarioVariablesReporter::GT_ScenarioVariablesReporter() = default;

GT_ScenarioVariablesReporter::~GT_ScenarioVariablesReporter()
{
    // Static destruction runs during process exit, where Winsock may already be
    // de-initialized (closesocket would fail with WSANOTINITIALISED, 10093).
    // Leave the socket to the OS; runtime cleanup goes through Close()/Init().
    udp_client_ = nullptr;
}

void GT_ScenarioVariablesReporter::Init(int udp_port, const std::string& target_ip)
{
    if (udp_client_)
    {
        delete udp_client_;
        udp_client_ = nullptr;
    }

    std::string ip = target_ip.empty() ? "127.0.0.1" : target_ip;
    udp_client_ = new UDPClient(static_cast<unsigned short>(udp_port), ip.c_str());

    if (udp_client_->GetStatus() != 0)
    {
        LOG_ERROR("GT_ScenarioVariablesReporter: Failed to init UDP on {}:{}", ip, udp_port);
        delete udp_client_;
        udp_client_ = nullptr;
        initialized_ = false;
    }
    else
    {
        LOG_INFO("GT_ScenarioVariablesReporter: UDP initialized on {}:{}", ip, udp_port);
        initialized_ = true;
    }
}

void GT_ScenarioVariablesReporter::Close()
{
    if (udp_client_)
    {
        delete udp_client_;
        udp_client_ = nullptr;
    }
    initialized_ = false;
}

// ---------------------------------------------------------------------------
// JSON helpers — hand-rolled to avoid nlohmann/json dependency
// ---------------------------------------------------------------------------

static void AppendEscapedString(std::string& buf, const char* s)
{
    buf += '"';
    for (; *s; ++s)
    {
        switch (*s)
        {
            case '"':  buf += "\\\""; break;
            case '\\': buf += "\\\\"; break;
            case '\n': buf += "\\n";  break;
            case '\r': buf += "\\r";  break;
            case '\t': buf += "\\t";  break;
            default:   buf += *s;     break;
        }
    }
    buf += '"';
}

void GT_ScenarioVariablesReporter::Update()
{
    if (!initialized_ || !udp_client_)
    {
        return;
    }

    int numVars = SE_GetNumberOfVariables();
    if (numVars <= 0)
    {
        return;  // No variables declared in this scenario
    }

    // Build JSON: {"sim_time":1.23,"variables":{"name":value,...}}
    json_buf_.clear();
    json_buf_ += "{\"sim_time\":";

    // Get simulation time from first object (index 0)
    SE_ScenarioObjectState state;
    double sim_time = 0.0;
    if (SE_GetObjectState(0, &state) == 0)
    {
        sim_time = state.timestamp;
    }

    char numbuf[64];
    std::snprintf(numbuf, sizeof(numbuf), "%.3f", sim_time);
    json_buf_ += numbuf;

    json_buf_ += ",\"variables\":{";

    bool first = true;
    for (int i = 0; i < numVars; ++i)
    {
        int type = 0;
        const char* name = SE_GetVariableName(i, &type);
        if (!name)
        {
            continue;
        }

        if (!first)
        {
            json_buf_ += ',';
        }
        first = false;

        // Key
        AppendEscapedString(json_buf_, name);
        json_buf_ += ':';

        // Value (type: 1=int, 2=double, 3=string, 4=bool)
        switch (type)
        {
            case 1:  // int
            {
                int val = 0;
                SE_GetVariableInt(name, &val);
                std::snprintf(numbuf, sizeof(numbuf), "%d", val);
                json_buf_ += numbuf;
                break;
            }
            case 2:  // double
            {
                double val = 0.0;
                SE_GetVariableDouble(name, &val);
                std::snprintf(numbuf, sizeof(numbuf), "%.6g", val);
                json_buf_ += numbuf;
                break;
            }
            case 3:  // string
            {
                const char* val = nullptr;
                SE_GetVariableString(name, &val);
                AppendEscapedString(json_buf_, val ? val : "");
                break;
            }
            case 4:  // bool
            {
                bool val = false;
                SE_GetVariableBool(name, &val);
                json_buf_ += val ? "true" : "false";
                break;
            }
            default:
            {
                json_buf_ += "null";
                break;
            }
        }
    }

    json_buf_ += "}}";

    // Send via UDP with esmini header
    unsigned int payload_size = static_cast<unsigned int>(json_buf_.size());
    if (payload_size > MAX_UDP_DATA_SIZE)
    {
        LOG_WARN("GT_ScenarioVariablesReporter: JSON too large ({} bytes), skipping", payload_size);
        return;
    }

    udp_buf_.counter = 0;  // single-packet message
    udp_buf_.datasize = payload_size;
    std::memcpy(udp_buf_.data, json_buf_.data(), payload_size);

    udp_client_->Send(
        reinterpret_cast<char*>(&udp_buf_),
        sizeof(udp_buf_.counter) + sizeof(udp_buf_.datasize) + payload_size);
}

} // namespace gt_esmini
