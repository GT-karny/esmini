#include "gt_esmini/control/realdriver/DriverOutputPort.hpp"
#include "gt_esmini/control/realdriver/LonProfilePlanner.hpp"
#include "gt_esmini/control/ControllerRealDriver.hpp"

#include "logger.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

namespace gt_esmini
{
void DriverOutputPort::SendLonProfile(ControllerRealDriver& controller, const std::vector<LonProfilePoint>& profile) const
{
    if (!controller.udpClient_ || profile.empty())
    {
        return;
    }

#pragma pack(push, 1)
    struct LonProfileHeader
    {
        uint8_t type;
        uint32_t count;
    };

    struct LonProfilePacketPoint
    {
        double t_offset;
        double v_target;
        double a_max;
        double j_max;
    };
#pragma pack(pop)

    const std::size_t header_size = sizeof(LonProfileHeader);
    const std::size_t point_size = sizeof(LonProfilePacketPoint);
    const std::size_t total_size = header_size + point_size * profile.size();

    std::vector<char> buffer(total_size);
    auto* header = reinterpret_cast<LonProfileHeader*>(buffer.data());
    header->type = 3;
    header->count = static_cast<uint32_t>(profile.size());

    auto* points = reinterpret_cast<LonProfilePacketPoint*>(buffer.data() + header_size);
    for (std::size_t i = 0; i < profile.size(); ++i)
    {
        points[i] = LonProfilePacketPoint{profile[i].t_offset, profile[i].v_target, profile[i].a_max, profile[i].j_max};
    }

    const int sent = controller.udpClient_->Send(buffer.data(), static_cast<int>(buffer.size()));
    if (sent != static_cast<int>(buffer.size()))
    {
        static int error_counter = 0;
        if (error_counter++ % 100 == 0)
        {
            LOG_WARN("RealDriver: Failed to send longitudinal profile (sent {} bytes, expected {})", sent, buffer.size());
        }
    }
}
} // namespace gt_esmini
