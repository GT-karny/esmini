#pragma once
#include "IReporter.hpp"

class StubReporter : public IReporter {
public:
    void report(const std::vector<SE_ScenarioObjectState>& states) override;
};
