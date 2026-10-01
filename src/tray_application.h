#pragma once

#include "desktop_manager.h"

// Keeps ATL/WTL implementation details out of the command-line entry point.
int RunTrayApplication(HINSTANCE instance, const MonitorTarget& target);
