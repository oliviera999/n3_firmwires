#pragma once

#include <Arduino.h>

#include "app_context.h"

namespace BootstrapNetwork {

struct OtaState {
  bool& justUpdated;
  String& previousVersion;
  unsigned long& lastCheck;
};

void setupHostname(char* buffer, size_t bufferSize);

void validatePendingOta(OtaState& state);

bool connect(AppContext& ctx, const char* hostname);

void onWifiReady(AppContext& ctx,
                 const char* hostname,
                 OtaState& state);

void postConfiguration(AppContext& ctx,
                       const char* hostname,
                       OtaState& state);

void checkForOtaUpdate(AppContext& ctx);

}  // namespace BootstrapNetwork


