#pragma once

namespace gt_esmini
{
class IGroundTruthPublisher
{
public:
    virtual ~IGroundTruthPublisher() = default;
    virtual void PublishGroundTruth() = 0;
};
} // namespace gt_esmini
