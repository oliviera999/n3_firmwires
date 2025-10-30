#pragma once

#include "app_context.h"

namespace BootstrapServices {

void initializeTimekeeping(AppContext& ctx);

bool initializeDisplay(AppContext& ctx);

void initializePeripherals(AppContext& ctx);

void loadConfiguration(AppContext& ctx);

void finalizeDisplay(AppContext& ctx);

}  // namespace BootstrapServices


