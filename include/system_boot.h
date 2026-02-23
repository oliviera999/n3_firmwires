#pragma once

#include <stddef.h>
#include <stdint.h>
#include "app_context.h"

namespace SystemBoot {

    // Storage & Network Identifiers
    void setupHostname(char* buffer, size_t bufferSize);
    void initializeStorage(AppContext& ctx);

    // OTA Validation
    struct OtaState {
        bool justUpdated;
        char previousVersion[32];
        unsigned long lastCheck;
    };
    /// Appelé au tout début du boot (avant LittleFS/NVS) pour annuler le rollback si image PENDING_VERIFY (évite rollback si crash avant validatePendingOta).
    void cancelRollbackIfPending();
    void validatePendingOta(OtaState& state);

    // Services Initialization
    void initializeTimekeeping(AppContext& ctx);
    bool initializeDisplay(AppContext& ctx);
    void initializePeripherals(AppContext& ctx);
    /// Scan du bus I2C (0x08..0x77), log série et remplissage de outBuf pour mail de démarrage.
    void runI2cScanAndLog(char* outBuf, size_t outSize);
    void loadConfiguration(AppContext& ctx);
    void finalizeDisplay(AppContext& ctx);

    // Network & OTA Operations
    bool connectWifi(AppContext& ctx, const char* hostname);
    void checkForOtaUpdate(AppContext& ctx);
    void onWifiReady(AppContext& ctx, const char* hostname, OtaState& state);
    void postConfiguration(AppContext& ctx, const char* hostname, OtaState& state);

} // namespace SystemBoot

