#pragma once

#include "desktop_manager.h"
#include "config_store.h"

// Keeps ATL/WTL implementation details out of the command-line entry point.
int RunTrayApplication(HINSTANCE instance, const ConfigStore& store, const AppConfig& config);
