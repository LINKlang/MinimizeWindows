#pragma once

#include <string>

struct MonitorTarget {
    std::wstring device_name;
};

class DesktopManager {
public:
    void ToggleDesktop(const MonitorTarget& target);
};
