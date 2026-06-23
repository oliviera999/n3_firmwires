#pragma once

#include <stddef.h>
#include <stdint.h>
#include "app_context.h"

namespace SystemBoot {

    // v13.53: Init Task Watchdog (TWDT) factorisé - cible WROOM/S3 sans PSRAM (S3+PSRAM
    // garde son init précoce dans app.cpp à cause de earlyInitVariant et IWDT/MWDT1).
    // Choisit le timeout selon le profil : 30s prod/test, 60s wroom-beta (USE_TEST_ENDPOINTS),
    // 300s S3 (sans PSRAM).
    void initWatchdog();

    // Storage & Network Identifiers
    void setupHostname(char* buffer, size_t bufferSize);
    void initializeStorage(AppContext& ctx);

    // OTA Validation
    struct OtaState {
        bool justUpdated;
        char previousVersion[32];
        unsigned long lastCheck;
    };
    /// Au boot, détecte une image OTA en attente de validation (ESP_OTA_IMG_PENDING_VERIFY).
    /// v14.17 : ne marque PLUS l'image valide inconditionnellement — arme une probation
    /// anti-rollback (cf. confirmOtaValidation / tickOtaValidation). Renseigne
    /// state.justUpdated et state.previousVersion pour la notification post-OTA.
    void validatePendingOta(OtaState& state);

    /// true tant qu'une image OTA est en probation (flashée mais pas encore confirmée saine).
    bool isOtaPendingValidation();

    /// Confirme la validité de l'image OTA en probation (annule le rollback) si une image
    /// est effectivement en attente. Idempotent. `reason` documente le déclencheur (log).
    /// À appeler sur un critère de santé OU avant un reboot délibéré (évite de rollbacker
    /// une bonne image fraîchement flashée sur un reboot manuel/distant).
    void confirmOtaValidation(const char* reason);

    /// À appeler périodiquement depuis loop() : confirme l'image en probation une fois
    /// l'uptime stable atteint (TimingConfig::OTA_VALIDATION_GRACE_MS). Filet anti-rollback
    /// indépendant du réseau (toujours exécuté, même build sans FEATURE_OTA).
    void tickOtaValidation();

    // Services Initialization
    void initializeTimekeeping(AppContext& ctx);
    bool initializeDisplay(AppContext& ctx);
    /// Carte SD (SPI) optionnelle, ESP32-S3 uniquement : détection au boot, log présent/absent.
    void initializeSdCard();
    void initializePeripherals(AppContext& ctx);
    /// Scan du bus I2C (0x08..0x77), log série et remplissage de outBuf pour mail de démarrage.
    void runI2cScanAndLog(char* outBuf, size_t outSize);
    /// Scan I2C puis init OLED dans le même mutex si 0x3C trouvé (évite NACK après relâche mutex). Retourne true si écran initialisé.
    bool runI2cScanAndInitDisplay(AppContext& ctx);
    void loadConfiguration(AppContext& ctx);
    void finalizeDisplay(AppContext& ctx);

    // Network & OTA Operations
    bool connectWifi(AppContext& ctx, const char* hostname);
    void checkForOtaUpdate(AppContext& ctx);
    void onWifiReady(AppContext& ctx, const char* hostname, OtaState& state);
    void postConfiguration(AppContext& ctx, const char* hostname, OtaState& state);

    /// Mail de démarrage (diagnostic) — extrait de setup() (cibles non-S3+PSRAM).
    void sendStartupTestMail(AppContext& ctx);

} // namespace SystemBoot

