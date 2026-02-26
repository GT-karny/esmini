#pragma once

#ifdef GT_ENABLE_EMBEDDED_PYTHON

namespace gt_esmini
{
class ControllerPythonDriver;

class PythonDriverCoordinator
{
public:
    void RunFrame(ControllerPythonDriver& controller, double time_step) const;
};

} // namespace gt_esmini

#endif // GT_ENABLE_EMBEDDED_PYTHON
