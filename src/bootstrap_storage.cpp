#include "bootstrap_storage.h"

#include <Arduino.h>
#include <LittleFS.h>

#include "event_log.h"
#include "nvs_manager.h"

namespace BootstrapStorage {

void initialize(AppContext& ctx) {
  EventLog::begin();
  EventLog::add("Boot start");

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

  g_nvsManager.enableDeferredFlush(true);
  g_nvsManager.setFlushInterval(3000);
  g_nvsManager.schedulePeriodicCleanup();
  g_nvsManager.migrateFromOldSystem();
}

}  // namespace BootstrapStorage


