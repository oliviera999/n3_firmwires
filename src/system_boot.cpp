#include "system_boot.h"
#include "bootstrap_network.h"
#include <Arduino.h>
#include <LittleFS.h>
#include "nvs_manager.h"
#include "config.h"

// Implémentation du namespace SystemBoot qui délègue aux namespaces existants
namespace SystemBoot {

void setupHostname(char* buffer, size_t bufferSize) {
    BootstrapNetwork::setupHostname(buffer, bufferSize);
}

void initializeStorage(AppContext& ctx) {
    // Fusionné depuis BootstrapStorage::initialize()
    Serial.println("[Event] Boot start");

    const char* fsLabel = "littlefs";
    Serial.printf("[FS] Mounting LittleFS (label=%s)...\n", fsLabel);
    uint32_t fsStartTime = millis();
    if (!LittleFS.begin(false, "/littlefs", 10, fsLabel)) {
        Serial.println("[FS] ❌ LittleFS mount failed - tentative de format");
        if (LittleFS.format()) {
            Serial.println("[FS] ✅ Format réussi, tentative de remontage");
            if (LittleFS.begin(false, "/littlefs", 10, fsLabel)) {
                Serial.println("[FS] ✅ Remontage après format réussi");
            } else {
                Serial.println("[FS] ❌ CRITIQUE: Impossible de monter LittleFS même après format");
            }
        } else {
            Serial.println("[FS] ❌ CRITIQUE: Format LittleFS échoué");
        }
    } else {
        uint32_t fsDuration = millis() - fsStartTime;
        size_t total = LittleFS.totalBytes();
        size_t used = LittleFS.usedBytes();
        Serial.printf("[FS] ✅ LittleFS ok: %u/%u bytes (montage: %u ms)\n",
                      static_cast<unsigned>(used),
                      static_cast<unsigned>(total),
                      fsDuration);

        if (!LittleFS.exists("/index.html")) {
            Serial.println("[FS] ⚠️ Fichier index.html manquant");
        }
        if (!LittleFS.exists("/shared/common.js")) {
            Serial.println("[FS] ⚠️ Fichier common.js manquant");
        }
    }

    Serial.println(F("[App] 🚀 Initialisation du gestionnaire NVS centralisé"));
    if (!g_nvsManager.begin()) {
        Serial.println(F("[App] ❌ Erreur initialisation NVS Manager"));
        return;
    }

    g_nvsManager.migrateFromOldSystem();
}

void validatePendingOta(OtaState& state) {
    // Adapter l'OtaState SystemBoot vers BootstrapNetwork::OtaState
    BootstrapNetwork::OtaState netState{
        state.justUpdated,
        state.previousVersion,
        state.lastCheck
    };
    BootstrapNetwork::validatePendingOta(netState);
}

void initializeTimekeeping(AppContext& ctx) {
    // Fusionné depuis BootstrapServices::initializeTimekeeping()
    ctx.power.initTime();
    Serial.println("[Event] Time init");

    ctx.power.setNTPConfig(SystemConfig::NTP_GMT_OFFSET_SEC,
                           SystemConfig::NTP_DAYLIGHT_OFFSET_SEC,
                           SystemConfig::NTP_SERVER);
}

bool initializeDisplay(AppContext& ctx) {
    // Fusionné depuis BootstrapServices::initializeDisplay()
    if (ctx.display.isPresent()) {
        ctx.display.hideOtaProgressOverlay();
    }

    Serial.println(F("[DEBUG] Avant oled.begin()"));
    bool result = ctx.display.begin();
    Serial.printf("[DEBUG] oled.begin() retourné: %s\n", result ? "true" : "false");
    Serial.printf("[DEBUG] oled.isPresent(): %s\n", ctx.display.isPresent() ? "true" : "false");
    return result;
}

void initializePeripherals(AppContext& ctx) {
    // Fusionné depuis BootstrapServices::initializePeripherals()
    if (ctx.display.isPresent()) {
        ctx.display.showDiagnostic("Sensors");
    }
    ctx.sensors.begin();

    if (ctx.display.isPresent()) {
        ctx.display.showDiagnostic("Actuators");
    }
    ctx.actuators.begin();

    if (ctx.display.isPresent()) {
        ctx.display.showDiagnostic("WebSrv");
    }
    ctx.webServer.begin();

    if (ctx.display.isPresent()) {
        ctx.display.showDiagnostic("Diag");
    }
    ctx.diagnostics.begin();

    if (ctx.display.isPresent()) {
        ctx.display.showDiagnostic("Systems");
    }
    ctx.power.initWatchdog();
}

void loadConfiguration(AppContext& ctx) {
    // Fusionné depuis BootstrapServices::loadConfiguration()
    Serial.println(F("\n[App] Chargement configuration complète depuis NVS..."));
    ctx.config.loadConfigFromNVS();
    ctx.automatism.begin();
}

void finalizeDisplay(AppContext& ctx) {
    // Fusionné depuis BootstrapServices::finalizeDisplay()
    if (!ctx.display.isPresent()) {
        return;
    }

    ctx.display.forceEndSplash();
    ctx.display.clear();
    ctx.display.showDiagnostic("Init ok");
    delay(SystemConfig::FINAL_INIT_DELAY_MS);
}

bool connectWifi(AppContext& ctx, const char* hostname) {
    return BootstrapNetwork::connect(ctx, hostname);
}

void checkForOtaUpdate(AppContext& ctx) {
    BootstrapNetwork::checkForOtaUpdate(ctx);
}

void onWifiReady(AppContext& ctx, const char* hostname, OtaState& state) {
    BootstrapNetwork::OtaState netState{
        state.justUpdated,
        state.previousVersion,
        state.lastCheck
    };
    BootstrapNetwork::onWifiReady(ctx, hostname, netState);
}

void postConfiguration(AppContext& ctx, const char* hostname, OtaState& state) {
    BootstrapNetwork::OtaState netState{
        state.justUpdated,
        state.previousVersion,
        state.lastCheck
    };
    BootstrapNetwork::postConfiguration(ctx, hostname, netState);
}

} // namespace SystemBoot
