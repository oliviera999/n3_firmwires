#pragma once

#include <stddef.h>
#include <stdint.h>
#include "app_context.h"

namespace SystemBoot {

    // Storage & Network Identifiers
    void setupHostname(char* buffer, size_t bufferSize);
    void initializeStorage(AppContext& ctx, unsigned long& lastDigestMs, uint32_t& lastDigestSeq);

    // OTA Validation
    struct OtaState {
        bool justUpdated;
        String previousVersion;
        unsigned long lastCheck;
    };
    void validatePendingOta(OtaState& state);

    // Services Initialization
    void initializeTimekeeping(AppContext& ctx);
    bool initializeDisplay(AppContext& ctx);
    void initializePeripherals(AppContext& ctx);
    void loadConfiguration(AppContext& ctx);
    void finalizeDisplay(AppContext& ctx);

    // Network & OTA Operations
    bool connectWifi(AppContext& ctx, const char* hostname);
    void checkForOtaUpdate(AppContext& ctx);
    void onWifiReady(AppContext& ctx, const char* hostname, OtaState& state);
    void postConfiguration(AppContext& ctx, const char* hostname, OtaState& state);

} // namespace SystemBoot

