#pragma once
#include <vector>
#include "esminiLib.hpp"

class IReporter {
public:
    virtual ~IReporter() = default;
    virtual void report(const std::vector<SE_ScenarioObjectState>& states) = 0;
};
