// Mailer implementation can be compiled out to save flash on ESP32 WROOM
#include "mailer.h"
#include "config.h"
#include "system_sensors.h"
#include "system_actuators.h"
#include "diagnostics.h"
#include "nvs_manager.h"
#include "nvs_keys.h"
#include "tls_mutex.h"  // v11.149: Mutex pour sérialiser TLS (SMTP/HTTPS)
#include <WiFi.h>
#include "wifi_manager.h"  // Pour WiFiHelpers
#include <time.h>
#include <LittleFS.h>
#include <esp_task_wdt.h> // Pour esp_task_wdt_reset() dans mailTask
#include <esp_heap_caps.h>
#include <cstring>

#if FEATURE_MAIL && FEATURE_MAIL != 0

// Buffer statique pour formatUptime (conforme .cursorrules)
static char g_uptimeBuffer[48];

// Construit un bloc d'informations système (réseau, version, mémoire, uptime)
static const char* formatUptime(unsigned long ms) {
  unsigned long totalSec = ms / 1000UL;
  unsigned int days = totalSec / 86400UL;
  totalSec %= 86400UL;
  unsigned int hours = totalSec / 3600UL;
  totalSec %= 3600UL;
  unsigned int mins = totalSec / 60UL;
  unsigned int secs = totalSec % 60UL;
  snprintf(g_uptimeBuffer, sizeof(g_uptimeBuffer), "%ud %02u:%02u:%02u", days, hours, mins, secs);
  return g_uptimeBuffer;
}

// Buffers statiques pour éviter fragmentation mémoire (conforme .cursorrules)
// v11.144: Réduits pour économiser ~4.5 KB de RAM
static char g_systemInfoFooterBuffer[1024];   // Réduit de 4096
static char g_detailedTimeReportBuffer[512];  // Réduit de 2048
static char g_lightFooterBuffer[256];         // Footer allégé
// Piste 4 rapport mémoire: un seul buffer pour sendSync et sendAlertSync (évite ~2.5 KB)
static char s_mailMessageBuffer[BufferConfig::EMAIL_MAX_SIZE_BYTES + 512];
// v11.178: kLittleFsLabel supprimé (non utilisé - audit dead-code)

// ======================
// FONCTIONS HELPER POUR ÉVITER LA DUPLICATION
// ======================

// Affiche une chaîne en ASCII sûr (0x20–0x7E) pour les logs série (évite caractères UTF-8 illisibles)
static void logSafeStr(const char* s, size_t maxLen) {
  if (!s) return;
  for (size_t i = 0; i < maxLen && s[i]; i++) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    Serial.print((c >= 0x20 && c <= 0x7E) ? static_cast<char>(c) : '?');
  }
}

// Epoch validé pour affichage (évite valeurs aberrantes en cas de régression RTC)
static time_t getSafeEpochForDisplay() {
  time_t t = time(nullptr);
  if (t >= SleepConfig::EPOCH_MIN_VALID && t <= SleepConfig::EPOCH_MAX_VALID && t != 0) {
    return t;
  }
  unsigned long saved;
  g_nvsManager.loadULong(NVS_NAMESPACES::SYSTEM, NVSKeys::System::RTC_EPOCH, saved, 0);
  time_t savedEpoch = static_cast<time_t>(saved);
  if (savedEpoch >= SleepConfig::EPOCH_MIN_VALID && savedEpoch <= SleepConfig::EPOCH_MAX_VALID) {
    return savedEpoch;
  }
  return SleepConfig::EPOCH_DEFAULT_FALLBACK;
}

// Helper: Ajouter les informations temporelles (temps actuel, epoch, uptime)
static int appendTimeInfo(char* buf, size_t& remaining) {
  int written = 0;
  time_t now = getSafeEpochForDisplay();
  
  written = snprintf(buf, remaining, "- Epoch: %lu\n", (unsigned long)now);
  if (written < 0 || (size_t)written >= remaining) {
    return -1;
  }
  buf += written;
  remaining -= written;
  
  if (now > 100000) {
    struct tm tmInfo;
    if (localtime_r(&now, &tmInfo)) {
      char tbuf[32];
      strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tmInfo);
      written = snprintf(buf, remaining, "- Local time: %s\n", tbuf);
      if (written < 0 || (size_t)written >= remaining) {
        return -1;
      }
      buf += written;
      remaining -= written;
    }
  }
  
  const char* uptimeStr = formatUptime(millis());
  written = snprintf(buf, remaining, "- Uptime: %s\n", uptimeStr);
  if (written < 0 || (size_t)written >= remaining) {
    return -1;
  }
  buf += written;
  remaining -= written;
  
  return 0;
}

// Helper: Ajouter les informations réseau (WiFi, SSID, IP, RSSI)
static int appendNetworkInfo(char* buf, size_t& remaining, bool detailed = false) {
  int written = 0;
  bool connected = WiFi.isConnected();
  char ssidBuf[33];
  if (connected) {
    WiFiHelpers::getSSID(ssidBuf, sizeof(ssidBuf));
  } else {
    strncpy(ssidBuf, "(déconnecté)", sizeof(ssidBuf) - 1);
    ssidBuf[sizeof(ssidBuf) - 1] = '\0';
  }
  const char* ssid = ssidBuf;
  IPAddress ipAddr = connected ? WiFi.localIP() : IPAddress(0, 0, 0, 0);
  long rssi = connected ? WiFi.RSSI() : 0;
  
  if (connected) {
    if (detailed) {
      // Version détaillée avec IP complète
      written = snprintf(buf, remaining, "- WiFi: Connecté\n"
                                         "- SSID: %s\n"
                                         "- IP: %d.%d.%d.%d\n"
                                         "- RSSI: %ld dBm\n",
                         ssid, ipAddr[0], ipAddr[1], ipAddr[2], ipAddr[3], rssi);
    } else {
      // Version allégée
      written = snprintf(buf, remaining, "- WiFi: Connecté (%s)\n", ssid);
    }
  } else {
    written = snprintf(buf, remaining, "- WiFi: Déconnecté\n");
  }
  
  if (written < 0 || (size_t)written >= remaining) {
    return -1;
  }
  buf += written;
  remaining -= written;
  
  return 0;
}

// Helper: Ajouter les informations mémoire (heap libre, min heap)
static int appendMemoryInfo(char* buf, size_t& remaining) {
  int written = 0;
  size_t psram = ESP.getPsramSize();
  
  if (psram > 0) {
    written = snprintf(buf, remaining, "- Heap libre: %u bytes\n"
                                       "- Heap min: %u bytes\n",
                       ESP.getFreeHeap(), ESP.getMinFreeHeap());
  } else {
    written = snprintf(buf, remaining, "- Heap libre: %u bytes\n"
                                       "- Heap min: %u bytes\n",
                       ESP.getFreeHeap(), ESP.getMinFreeHeap());
  }
  
  if (written < 0 || (size_t)written >= remaining) {
    return -1;
  }
  buf += written;
  remaining -= written;
  
  return 0;
}

// ======================
// FOOTER ALLÉGÉ
// ======================

// Footer une ligne : version + heure + temp eau (pertinent, peu chargé)
static const char* buildLightFooter() {
  char* buf = g_lightFooterBuffer;
  size_t remaining = sizeof(g_lightFooterBuffer);
  time_t now = getSafeEpochForDisplay();
  char timeBuf[24] = "(heure N/A)";
  if (now >= SleepConfig::EPOCH_MIN_VALID && now <= SleepConfig::EPOCH_MAX_VALID) {
    struct tm tmInfo;
    if (localtime_r(&now, &tmInfo)) {
      strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M", &tmInfo);
    }
  }
  extern SystemSensors sensors;
  float tempWater = sensors.read().tempWater;
  int n = snprintf(buf, remaining, "\n[FFP5CS v%s] %s | Eau %.1f°C",
                   ProjectConfig::VERSION, timeBuf, tempWater);
  if (n < 0 || (size_t)n >= remaining) {
    buf[remaining - 1] = '\0';
  }
  return g_lightFooterBuffer;
}

static const char* buildSystemInfoFooter() {
  char* buf = g_systemInfoFooterBuffer;
  size_t remaining = sizeof(g_systemInfoFooterBuffer);
  int written = 0;

  // Version / carte
#ifdef BOARD_S3
  const char* board = "ESP32-S3";
#else
  const char* board = "ESP32";
#endif
  
  // v11.179: Protection contre NULL (fix crash LoadProhibited)
  const char* profileName = Utils::getProfileName();
  if (!profileName) profileName = "(unknown)";
  
  written = snprintf(buf, remaining, "\n\n-- Infos système --\n"
                                     "- Version: %s\n"
                                     "- Carte: %s\n"
                                     "- Environnement: %s\n",
                     ProjectConfig::VERSION, board, profileName);
  if (written < 0 || (size_t)written >= remaining) {
    buf[remaining - 1] = '\0';
    return buf;
  }
  buf += written;
  remaining -= written;

  // Réseau
  bool connected = WiFi.isConnected();
  char ssidBuf[33];
  if (connected) {
    WiFiHelpers::getSSID(ssidBuf, sizeof(ssidBuf));
  } else {
    strncpy(ssidBuf, "(déconnecté)", sizeof(ssidBuf) - 1);
    ssidBuf[sizeof(ssidBuf) - 1] = '\0';
  }
  const char* ssid = ssidBuf;
  IPAddress ipAddr = connected ? WiFi.localIP() : IPAddress(0, 0, 0, 0);
  long rssi = connected ? WiFi.RSSI() : 0;
  char macBuf[18];
  WiFiHelpers::getMACString(macBuf, sizeof(macBuf));
  const char* mac = macBuf;
  
  if (connected) {
    written = snprintf(buf, remaining, "- SSID: %s\n"
                                       "- IP: %d.%d.%d.%d\n"
                                       "- RSSI: %ld dBm\n"
                                       "- MAC: %s\n",
                       ssid, ipAddr[0], ipAddr[1], ipAddr[2], ipAddr[3], rssi, mac);
  } else {
    written = snprintf(buf, remaining, "- SSID: %s\n"
                                       "- IP: -\n"
                                       "- RSSI: %ld dBm\n"
                                       "- MAC: %s\n",
                       ssid, rssi, mac);
  }
  if (written < 0 || (size_t)written >= remaining) {
    buf[remaining - 1] = '\0';
    return g_systemInfoFooterBuffer;
  }
  buf += written;
  remaining -= written;

  // Qualité WiFi (%), canal, BSSID, hostname, GW, masque, DNS
  if (connected) {
    int wifiQuality = 0;
    if (rssi <= -100) wifiQuality = 0; else if (rssi >= -50) wifiQuality = 100; else wifiQuality = 2 * (rssi + 100);
    IPAddress gateway = WiFi.gatewayIP();
    IPAddress subnet = WiFi.subnetMask();
    IPAddress dns = WiFi.dnsIP(0);
    // v11.179: Protection contre hostname NULL (fix crash LoadProhibited)
    const char* host = WiFi.getHostname();
    if (!host) host = "(unknown)";
    char bssidBuf[18];
    // v11.179: Utiliser WiFi.BSSID() au lieu de BSSIDstr() pour éviter String temporaire corrompue
    uint8_t* bssidPtr = WiFi.BSSID();
    if (bssidPtr) {
      snprintf(bssidBuf, sizeof(bssidBuf), "%02X:%02X:%02X:%02X:%02X:%02X",
               bssidPtr[0], bssidPtr[1], bssidPtr[2], bssidPtr[3], bssidPtr[4], bssidPtr[5]);
    } else {
      strncpy(bssidBuf, "(unavailable)", sizeof(bssidBuf) - 1);
    }
    bssidBuf[sizeof(bssidBuf) - 1] = '\0';
    const char* bssid = bssidBuf;
    
    if (host && *host) {
      written = snprintf(buf, remaining, "- Qualité: %d%%\n"
                                         "- Canal: %d\n"
                                         "- BSSID: %s\n"
                                         "- Hostname: %s\n"
                                         "- Passerelle: %d.%d.%d.%d\n"
                                         "- Masque: %d.%d.%d.%d\n"
                                         "- DNS: %d.%d.%d.%d\n",
                         wifiQuality, WiFi.channel(), bssid, host,
                         gateway[0], gateway[1], gateway[2], gateway[3],
                         subnet[0], subnet[1], subnet[2], subnet[3],
                         dns[0], dns[1], dns[2], dns[3]);
    } else {
      written = snprintf(buf, remaining, "- Qualité: %d%%\n"
                                         "- Canal: %d\n"
                                         "- BSSID: %s\n"
                                         "- Passerelle: %d.%d.%d.%d\n"
                                         "- Masque: %d.%d.%d.%d\n"
                                         "- DNS: %d.%d.%d.%d\n",
                         wifiQuality, WiFi.channel(), bssid,
                         gateway[0], gateway[1], gateway[2], gateway[3],
                         subnet[0], subnet[1], subnet[2], subnet[3],
                         dns[0], dns[1], dns[2], dns[3]);
    }
    if (written < 0 || (size_t)written >= remaining) {
      buf[remaining - 1] = '\0';
      return g_systemInfoFooterBuffer;
    }
    buf += written;
    remaining -= written;
  }

  // Mode WiFi et AP si actif
  wifi_mode_t mode = WiFi.getMode();
  const char* modeStr = (mode == WIFI_OFF ? "OFF" : mode == WIFI_STA ? "STA" : mode == WIFI_AP ? "AP" : mode == WIFI_AP_STA ? "AP+STA" : "UNKNOWN");
  written = snprintf(buf, remaining, "- WiFi mode: %s\n", modeStr);
  if (written < 0 || (size_t)written >= remaining) {
    buf[remaining - 1] = '\0';
    return g_systemInfoFooterBuffer;
  }
  buf += written;
  remaining -= written;
  
  if (mode == WIFI_AP || mode == WIFI_AP_STA) {
    IPAddress apIp = WiFi.softAPIP();
    char apSSIDBuf[33];
    WiFiHelpers::getAPSSID(apSSIDBuf, sizeof(apSSIDBuf));
    written = snprintf(buf, remaining, "- AP SSID: %s\n"
                                       "- AP IP: %d.%d.%d.%d\n",
                       apSSIDBuf, apIp[0], apIp[1], apIp[2], apIp[3]);
    if (written < 0 || (size_t)written >= remaining) {
      buf[remaining - 1] = '\0';
      return g_systemInfoFooterBuffer;
    }
    buf += written;
    remaining -= written;
  }

  // ======================
  // INFORMATIONS TEMPORELLES DÉTAILLÉES
  // ======================
  written = snprintf(buf, remaining, "\n-- Informations temporelles --\n");
  if (written < 0 || (size_t)written >= remaining) {
    buf[remaining - 1] = '\0';
    return g_systemInfoFooterBuffer;
  }
  buf += written;
  remaining -= written;
  
  // Utiliser le helper pour les infos temporelles de base
  if (appendTimeInfo(buf, remaining) < 0) {
    buf[remaining - 1] = '\0';
    return g_systemInfoFooterBuffer;
  }
  
  // Informations supplémentaires spécifiques au footer complet
  time_t now = getSafeEpochForDisplay();
  if (now > 100000) {
    struct tm tmInfo;
    if (localtime_r(&now, &tmInfo)) {
      char tbuf[32];
      strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tmInfo);
      written = snprintf(buf, remaining, "- Jour de la semaine: %d (0=dimanche)\n"
                                         "- Jour de l'année: %d\n"
                                         "- DST actif: %s\n"
                                         "- Timezone: Maroc UTC+1\n",
                         tmInfo.tm_wday, tmInfo.tm_yday, tmInfo.tm_isdst ? "OUI" : "NON");
      if (written < 0 || (size_t)written >= remaining) {
        buf[remaining - 1] = '\0';
        return g_systemInfoFooterBuffer;
      }
      buf += written;
      remaining -= written;
    }
  }
  
  // Informations NTP
  written = snprintf(buf, remaining, "- NTP Server: %s\n"
                                     "- GMT Offset: +%ldh\n"
                                     "- DST Offset: +%ldh\n",
                     SystemConfig::NTP_SERVER,
                     SystemConfig::NTP_GMT_OFFSET_SEC/3600,
                     SystemConfig::NTP_DAYLIGHT_OFFSET_SEC/3600);
  if (written < 0 || (size_t)written >= remaining) {
    buf[remaining - 1] = '\0';
    return g_systemInfoFooterBuffer;
  }
  buf += written;
  remaining -= written;
  
  // Informations RTC/Flash (SYSTEM namespace)
  unsigned long savedEpoch;
  g_nvsManager.loadULong(NVS_NAMESPACES::SYSTEM, NVSKeys::System::RTC_EPOCH, savedEpoch, 0);
  if (savedEpoch > 0) {
    written = snprintf(buf, remaining, "- RTC Flash epoch: %lu\n", savedEpoch);
    if (written < 0 || (size_t)written >= remaining) {
      buf[remaining - 1] = '\0';
      return g_systemInfoFooterBuffer;
    }
    buf += written;
    remaining -= written;
    
    if (savedEpoch != (unsigned long)now) {
      long diff = (long)now - (long)savedEpoch;
      written = snprintf(buf, remaining, "- Diff RTC vs actuel: %ld secondes\n", diff);
      if (written < 0 || (size_t)written >= remaining) {
        buf[remaining - 1] = '\0';
        return g_systemInfoFooterBuffer;
      }
      buf += written;
      remaining -= written;
    }
  }

  // Mémoire (utiliser helper de base, puis ajouter infos supplémentaires)
  if (appendMemoryInfo(buf, remaining) < 0) {
    buf[remaining - 1] = '\0';
    return g_systemInfoFooterBuffer;
  }
  
  // Informations mémoire supplémentaires pour footer complet
  size_t psram = ESP.getPsramSize();
  if (psram > 0) {
    written = snprintf(buf, remaining, "- Heap max alloc: %u bytes\n"
                                       "- PSRAM size: %zu bytes\n"
                                       "- PSRAM free: %u bytes\n",
                       ESP.getMaxAllocHeap(), psram, ESP.getFreePsram());
    if (written < 0 || (size_t)written >= remaining) {
      buf[remaining - 1] = '\0';
      return g_systemInfoFooterBuffer;
    }
    buf += written;
    remaining -= written;
  } else {
    written = snprintf(buf, remaining, "- Heap max alloc: %u bytes\n",
                       ESP.getMaxAllocHeap());
    if (written < 0 || (size_t)written >= remaining) {
      buf[remaining - 1] = '\0';
      return g_systemInfoFooterBuffer;
    }
    buf += written;
    remaining -= written;
  }

  // Diagnostics persistés si disponibles
  int rebootCount;
  g_nvsManager.loadInt(NVS_NAMESPACES::LOGS, NVSKeys::Diag::REBOOT_CNT, rebootCount, 0);
  // minHeap: UINT32_MAX = pas encore de mesure
  unsigned long minHeapUL = UINT32_MAX;
  g_nvsManager.loadULong(NVS_NAMESPACES::LOGS, NVSKeys::Diag::MIN_HEAP, minHeapUL, UINT32_MAX);
  if (minHeapUL < UINT32_MAX) {
    written = snprintf(buf, remaining, "- Reboots: %d\n"
                                       "- Min heap: %lu bytes\n",
                       rebootCount, minHeapUL);
  } else {
    written = snprintf(buf, remaining, "- Reboots: %d\n", rebootCount);
  }
  if (written < 0 || (size_t)written >= remaining) {
    buf[remaining - 1] = '\0';
    return g_systemInfoFooterBuffer;
  }
  buf += written;
  remaining -= written;

  // Uptime déjà inclus dans appendTimeInfo(), pas besoin de le répéter

  // ======================
  // Mesures & Actionneurs
  // ======================
  written = snprintf(buf, remaining, "\n-- Mesures / Actionneurs --\n");
  if (written < 0 || (size_t)written >= remaining) {
    buf[remaining - 1] = '\0';
    return g_systemInfoFooterBuffer;
  }
  buf += written;
  remaining -= written;
  
  extern SystemSensors sensors;
  extern SystemActuators acts;
  // Lecture instantanée (peut prendre quelques centaines de ms)
  SensorReadings rs = sensors.read();
  int diffM = sensors.diffMaree(rs.wlAqua);
  written = snprintf(buf, remaining, "- Temp eau: %.1f °C\n"
                                     "- Temp air: %.1f °C\n"
                                     "- Humidité: %.0f %%\n"
                                     "- Aqua lvl: %d cm\n"
                                     "- Réserve lvl: %d cm\n"
                                     "- Potager lvl: %d cm\n"
                                     "- Luminosité: %d\n"
                                     "- Diff marée: %d\n",
                     rs.tempWater, rs.tempAir, rs.humidity, rs.wlAqua, rs.wlTank, rs.wlPota, rs.luminosite, diffM);
  if (written < 0 || (size_t)written >= remaining) {
    buf[remaining - 1] = '\0';
    return g_systemInfoFooterBuffer;
  }
  buf += written;
  remaining -= written;
  
  // Ajouter les informations de délai d'activité
  written = snprintf(buf, remaining, "\n-- Configuration Sleep --\n"
                                     "- Délai d'activité: Configuration adaptative\n"
                                     "- Mode adaptatif: ACTIF\n");
  if (written < 0 || (size_t)written >= remaining) {
    buf[remaining - 1] = '\0';
    return g_systemInfoFooterBuffer;
  }
  buf += written;
  remaining -= written;
  
  // États actionneurs
  unsigned long curRun = acts.getTankPumpCurrentRuntime();
  written = snprintf(buf, remaining, "- Pompe aquarium: %s\n"
                                     "- Pompe réservoir: %s\n"
                                     "- Chauffage: %s\n"
                                     "- Lumière: %s\n"
                                     "- Pompe réservoir runtime courant: %lu ms\n"
                                     "- Pompe réservoir runtime total: %lu ms\n"
                                     "- Pompe réservoir arrêts totaux: %u\n",
                     acts.isAquaPumpRunning() ? "ON" : "OFF",
                     acts.isTankPumpRunning() ? "ON" : "OFF",
                     acts.isHeaterOn() ? "ON" : "OFF",
                     acts.isLightOn() ? "ON" : "OFF",
                     curRun, acts.getTankPumpTotalRuntime(), acts.getTankPumpTotalStops());
  if (written < 0 || (size_t)written >= remaining) {
    buf[remaining - 1] = '\0';
    return g_systemInfoFooterBuffer;
  }
  buf += written;
  remaining -= written;

  // CPU / Chip / SDK
  written = snprintf(buf, remaining, "- CPU freq: %d MHz\n"
                                     "- Chip: %s, rev %d, cores %d\n"
                                     "- SDK: %s\n",
                     getCpuFrequencyMhz(), ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(), ESP.getSdkVersion());
  if (written < 0 || (size_t)written >= remaining) {
    buf[remaining - 1] = '\0';
    return g_systemInfoFooterBuffer;
  }
  buf += written;
  remaining -= written;

  // Sketch / flash
  written = snprintf(buf, remaining, "- Sketch size: %u bytes\n"
                                     "- Free sketch: %u bytes\n"
                                     "- Flash size: %u bytes\n",
                     ESP.getSketchSize(), ESP.getFreeSketchSpace(), ESP.getFlashChipSize());
  if (written < 0 || (size_t)written >= remaining) {
    buf[remaining - 1] = '\0';
    return g_systemInfoFooterBuffer;
  }
  buf += written;
  remaining -= written;

  // Filesystem (LittleFS) - FS deja montee au boot, appel direct sans begin()
  {
    size_t total = LittleFS.totalBytes();
    size_t used  = LittleFS.usedBytes();
    if (total > 0) {  // FS accessible
      written = snprintf(buf, remaining, "- FS LittleFS: %zu/%zu bytes\n", used, total);
      if (written < 0 || (size_t)written >= remaining) {
        buf[remaining - 1] = '\0';
        return g_systemInfoFooterBuffer;
      }
      buf += written;
      remaining -= written;
    }
  }

  // Endpoints serveur utiles
  written = snprintf(buf, remaining, "- POST URL: %s\n"
                                     "- OUTPUT URL: %s\n",
                     ServerConfig::POST_DATA_ENDPOINT, ServerConfig::OUTPUT_ENDPOINT);
  if (written < 0 || (size_t)written >= remaining) {
    buf[remaining - 1] = '\0';
    return g_systemInfoFooterBuffer;
  }
  buf += written;
  remaining -= written;

  // ======================
  // NVS principales (resume)
  // ======================
  written = snprintf(buf, remaining, "\n-- NVS principales --\n");
  if (written < 0 || (size_t)written >= remaining) {
    buf[remaining - 1] = '\0';
    return g_systemInfoFooterBuffer;
  }
  buf += written;
  remaining -= written;
  
  {
    // Namespace bouffe (CONFIG) + force_wake_up (SYSTEM)
    bool bouffeMatinOk, bouffeMidiOk, bouffeSoirOk, pompeAquaLocked, forceWakeUp;
    int lastJourBouf;
    g_nvsManager.loadBool(NVS_NAMESPACES::CONFIG, NVSKeys::Config::BOUFFE_MATIN, bouffeMatinOk, false);
    g_nvsManager.loadBool(NVS_NAMESPACES::CONFIG, NVSKeys::Config::BOUFFE_MIDI, bouffeMidiOk, false);
    g_nvsManager.loadBool(NVS_NAMESPACES::CONFIG, NVSKeys::Config::BOUFFE_SOIR, bouffeSoirOk, false);
    g_nvsManager.loadInt(NVS_NAMESPACES::CONFIG, NVSKeys::Config::BOUFFE_JOUR, lastJourBouf, -1);
    g_nvsManager.loadBool(NVS_NAMESPACES::CONFIG, NVSKeys::Config::BF_PMP_LOCK, pompeAquaLocked, false);
    // v11.172: Clé unique (migration terminée)
    g_nvsManager.loadBool(NVS_NAMESPACES::SYSTEM, NVSKeys::System::FORCE_WAKE_UP, forceWakeUp, false);
    written = snprintf(buf, remaining, "- bouffeMatinOk: %s\n"
                                       "- bouffeMidiOk: %s\n"
                                       "- bouffeSoirOk: %s\n"
                                       "- lastJourBouf: %d\n"
                                       "- pompeAquaLocked: %s\n"
                                       "- forceWakeUp: %s\n",
                       bouffeMatinOk ? "true" : "false",
                       bouffeMidiOk ? "true" : "false",
                       bouffeSoirOk ? "true" : "false",
                       lastJourBouf,
                       pompeAquaLocked ? "true" : "false",
                       forceWakeUp ? "true" : "false");
    if (written < 0 || (size_t)written >= remaining) {
      buf[remaining - 1] = '\0';
      return g_systemInfoFooterBuffer;
    }
    buf += written;
    remaining -= written;

    // Namespace OTA - clé unifiée avec ConfigManager (défaut false, cohérent avec config)
    bool otaUpdateFlag;
    g_nvsManager.loadBool(NVS_NAMESPACES::SYSTEM, NVSKeys::System::OTA_UPDATE_FLAG, otaUpdateFlag, false);
    written = snprintf(buf, remaining, "- ota.updateFlag: %s\n", otaUpdateFlag ? "true" : "false");
    if (written < 0 || (size_t)written >= remaining) {
      buf[remaining - 1] = '\0';
      return g_systemInfoFooterBuffer;
    }
    buf += written;
    remaining -= written;

    // Namespace remoteVars (aperçu) - v11.103: Utilisation du gestionnaire NVS centralisé
    char remoteJson[2048];
    g_nvsManager.loadString(NVS_NAMESPACES::CONFIG, NVSKeys::Config::REMOTE_JSON, remoteJson, sizeof(remoteJson), "");
    if (strlen(remoteJson) > 0) {
      size_t previewLen = min((size_t)200, strlen(remoteJson));
      char previewBuf[201];
      strncpy(previewBuf, remoteJson, previewLen);
      previewBuf[previewLen] = '\0';
      if (strlen(remoteJson) > 200) {
        written = snprintf(buf, remaining, "- remoteVars.json: %s...\n", previewBuf);
      } else {
        written = snprintf(buf, remaining, "- remoteVars.json: %s\n", previewBuf);
      }
    } else {
      written = snprintf(buf, remaining, "- remoteVars.json: (vide)\n");
    }
    if (written < 0 || (size_t)written >= remaining) {
      buf[remaining - 1] = '\0';
      return g_systemInfoFooterBuffer;
    }
    buf += written;
    remaining -= written;

    // rtc_epoch (SYSTEM namespace)
    unsigned long savedEpoch2;
    g_nvsManager.loadULong(NVS_NAMESPACES::SYSTEM, NVSKeys::System::RTC_EPOCH, savedEpoch2, 0);
    written = snprintf(buf, remaining, "- sys.rtc_epoch: %lu\n", savedEpoch2);
    if (written < 0 || (size_t)written >= remaining) {
      buf[remaining - 1] = '\0';
      return g_systemInfoFooterBuffer;
    }
    buf += written;
    remaining -= written;

    // Namespace diagnostics (déjà partiellement inclus plus haut, rappel condensé)
    int rebootCnt2;
    unsigned long minHeap2;
    g_nvsManager.loadInt(NVS_NAMESPACES::LOGS, NVSKeys::Diag::REBOOT_CNT, rebootCnt2, 0);
    g_nvsManager.loadULong(NVS_NAMESPACES::LOGS, NVSKeys::Diag::MIN_HEAP, minHeap2, UINT32_MAX);
    written = snprintf(buf, remaining, "- diagnostics.rebootCnt: %d\n"
                                       "- diagnostics.minHeap: %lu\n",
                       rebootCnt2, (minHeap2 < UINT32_MAX) ? minHeap2 : 0UL);
    if (written < 0 || (size_t)written >= remaining) {
      buf[remaining - 1] = '\0';
      return g_systemInfoFooterBuffer;
    }
    buf += written;
    remaining -= written;
  }

  return g_systemInfoFooterBuffer;
}

// Fonction pour générer un rapport temporel détaillé
static const char* buildDetailedTimeReport(const Diagnostics& diagnostics) {
  char* buf = g_detailedTimeReportBuffer;
  size_t remaining = sizeof(g_detailedTimeReportBuffer);
  int written = 0;
  
  written = snprintf(buf, remaining, "\n\n-- RAPPORT TEMPOREL DÉTAILLÉ --\n");
  if (written < 0 || (size_t)written >= remaining) {
    buf[remaining - 1] = '\0';
    return buf;
  }
  buf += written;
  remaining -= written;
  
  time_t now = getSafeEpochForDisplay();
  struct tm timeinfo;
  if (localtime_r(&now, &timeinfo)) {
    char timeBuf[64];
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    
    // Utiliser le helper pour les infos temporelles de base
    // Mais on doit d'abord ajouter le header et les infos spécifiques
    written = snprintf(buf, remaining, "Heure actuelle: %s\n"
                                       "Epoch: %lu\n"
                                       "Jour de la semaine: %d (0=dimanche)\n"
                                       "Jour de l'année: %d\n"
                                       "DST actif: %s\n",
                       timeBuf, (unsigned long)now, timeinfo.tm_wday, timeinfo.tm_yday,
                       timeinfo.tm_isdst ? "OUI" : "NON");
    if (written < 0 || (size_t)written >= remaining) {
      buf[remaining - 1] = '\0';
      return g_detailedTimeReportBuffer;
    }
    buf += written;
    remaining -= written;
    
    // Uptime (utiliser formatUptime)
    const char* uptimeStr = formatUptime(millis());
    written = snprintf(buf, remaining, "Uptime: %s\n", uptimeStr);
    if (written < 0 || (size_t)written >= remaining) {
      buf[remaining - 1] = '\0';
      return g_detailedTimeReportBuffer;
    }
    buf += written;
    remaining -= written;
    
    // Informations NTP
    written = snprintf(buf, remaining, "Serveur NTP: %s\n"
                                       "GMT Offset: +%ldh\n"
                                       "DST Offset: +%ldh\n",
                       SystemConfig::NTP_SERVER,
                       SystemConfig::NTP_GMT_OFFSET_SEC/3600,
                       SystemConfig::NTP_DAYLIGHT_OFFSET_SEC/3600);
    if (written < 0 || (size_t)written >= remaining) {
      buf[remaining - 1] = '\0';
      return g_detailedTimeReportBuffer;
    }
    buf += written;
    remaining -= written;
    
    // Informations RTC/Flash (SYSTEM namespace)
    unsigned long savedEpoch;
    g_nvsManager.loadULong(NVS_NAMESPACES::SYSTEM, NVSKeys::System::RTC_EPOCH, savedEpoch, 0);
    if (savedEpoch > 0) {
      written = snprintf(buf, remaining, "RTC Flash epoch: %lu\n", savedEpoch);
      if (written < 0 || (size_t)written >= remaining) {
        buf[remaining - 1] = '\0';
        return g_detailedTimeReportBuffer;
      }
      buf += written;
      remaining -= written;
      
      if (savedEpoch != (unsigned long)now) {
        long diff = (long)now - (long)savedEpoch;
        if (abs(diff) > 60) {
          written = snprintf(buf, remaining, "Diff RTC vs actuel: %ld secondes\n"
                                             "⚠️ Écart important entre RTC et temps actuel!\n",
                             diff);
        } else {
          written = snprintf(buf, remaining, "Diff RTC vs actuel: %ld secondes\n", diff);
        }
        if (written < 0 || (size_t)written >= remaining) {
          buf[remaining - 1] = '\0';
          return g_detailedTimeReportBuffer;
        }
        buf += written;
        remaining -= written;
      }
    }
  } else {
    written = snprintf(buf, remaining, "Erreur: Impossible de récupérer l'heure locale\n");
    if (written < 0 || (size_t)written >= remaining) {
      buf[remaining - 1] = '\0';
      return g_detailedTimeReportBuffer;
    }
    buf += written;
    remaining -= written;
  }
  
  // Ajouter les informations de redémarrage
  written = snprintf(buf, remaining, "\n-- INFORMATIONS DE REDÉMARRAGE --\n");
  if (written < 0 || (size_t)written >= remaining) {
    buf[remaining - 1] = '\0';
    return g_detailedTimeReportBuffer;
  }
  buf += written;
  remaining -= written;
  
  // Ajouter le rapport de redémarrage (utilise buffer statique)
  char restartReportBuffer[2048];
  diagnostics.generateRestartReport(restartReportBuffer, sizeof(restartReportBuffer));
  size_t restartLen = strlen(restartReportBuffer);
  if (restartLen > 0 && restartLen < remaining) {
    strncpy(buf, restartReportBuffer, remaining - 1);
    buf[remaining - 1] = '\0';
  }
  
  return g_detailedTimeReportBuffer;
}

bool Mailer::begin() {
  Serial.println(F("[Mail] ===== INITIALISATION MAILER ====="));
  
  // Prépare uniquement la configuration SMTP.
  // La connexion TLS/SMTP sera établie à la première utilisation dans send().

  _cfg = Session_Config();
  _cfg.server.host_name = EmailConfig::SMTP_HOST;
  _cfg.server.port      = EmailConfig::SMTP_PORT;
  _cfg.login.email      = Secrets::AUTHOR_EMAIL;
  _cfg.login.password   = Secrets::AUTHOR_PASSWORD;

  // v11.151: Reconnexion automatique et timeout conforme à .cursorrules (max 5s)
  MailClient.networkReconnect(true);
  _smtp.setTCPTimeout(5);  // 5 secondes de timeout TCP (conforme .cursorrules: max 5s pour opérations réseau)

  // Vérifications de configuration
  if (!EmailConfig::SMTP_HOST || strlen(EmailConfig::SMTP_HOST) == 0) {
    Serial.println(F("[Mail] ❌ ERREUR: SMTP_HOST non configuré"));
    return false;
  }
  if (!Secrets::AUTHOR_EMAIL || strlen(Secrets::AUTHOR_EMAIL) == 0) {
    Serial.println(F("[Mail] ❌ ERREUR: AUTHOR_EMAIL non configuré"));
    return false;
  }
  if (!Secrets::AUTHOR_PASSWORD || strlen(Secrets::AUTHOR_PASSWORD) == 0) {
    Serial.println(F("[Mail] ❌ ERREUR: AUTHOR_PASSWORD non configuré"));
    return false;
  }
  
  // Pas de connexion ici pour éviter le blocage au démarrage
  _ready = false;
  Serial.println(F("[Mail] ✅ Configuration SMTP prête (connexion différée)"));
  Serial.println(F("[Mail] ===== FIN INITIALISATION MAILER ====="));
  return true;
}

// Fonction d'attente réseau pour SMTP (similaire à PowerManager::waitForNetworkReady)
static bool waitForNetworkReadyForSMTP() {
  wl_status_t st = WiFi.status();
  if (st != WL_CONNECTED) {
    Serial.printf("[Mail] waitForNetworkReady: WiFi non connecté (status=%d), abandon\n", (int)st);
    return false;
  }
  
  // v11.165: Timeouts réduits (règle offline-first: max 3s blocage)
  const uint32_t STABILIZATION_DELAY_MS = 1000;  // 1 seconde de stabilisation
  const uint32_t MAX_WAIT_MS = 3000;             // 3 secondes max d'attente totale
  uint32_t startMs = millis();
  
  Serial.println(F("[Mail] Attente stabilisation réseau pour SMTP..."));
  
  // Phase 1: Délai minimum de stabilisation TCP/IP
  vTaskDelay(pdMS_TO_TICKS(STABILIZATION_DELAY_MS));
  
  // Phase 2: Vérifier que l'IP est toujours valide et DNS fonctionne
  while ((millis() - startMs) < MAX_WAIT_MS) {
    if (esp_task_wdt_status(NULL) == ESP_OK) {
      esp_task_wdt_reset();  // Plan: watchdog dans boucles longues
    }
    if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
      // Test DNS rapide pour vérifier que le réseau est vraiment opérationnel
      IPAddress dnsResult;
      if (WiFi.hostByName("smtp.gmail.com", dnsResult)) {
        IPAddress localIP = WiFi.localIP();
        Serial.printf("[Mail] ✅ Réseau prêt pour SMTP (%d.%d.%d.%d, DNS OK, %lu ms)\n", 
                      localIP[0], localIP[1], localIP[2], localIP[3], millis() - startMs);
        return true;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(300));
  }
  
  // Timeout atteint mais WiFi connecté - on tente quand même
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[Mail] ⚠️ Réseau partiellement prêt après %lu ms (DNS timeout), on tente quand même\n", 
                  millis() - startMs);
    return true;
  }
  
  Serial.println(F("[Mail] ❌ Réseau perdu pendant stabilisation"));
  return false;
}

bool Mailer::sendSync(const char* subject, const char* message, const char* toName, const char* toEmail) {
  Serial.println(F("[Mail] Trace 1: Start sendSync"));
  Serial.println(F("[Mail] ===== DIAGNOSTIC SEND ====="));
  Serial.printf("[Mail] _ready: %s\n", _ready ? "TRUE" : "FALSE");
  Serial.printf("[Mail] _smtp.connected(): %s\n", _smtp.connected() ? "TRUE" : "FALSE");
  
  // === PROTECTION SIMPLIFIÉE v11.151 ===
  // Garde seulement le mutex TLS, supprime la protection heap trop restrictive
  // Le heap bas causait le blocage de TOUS les mails
  
  // 1. Vérifier que le système n'entre pas en light sleep
  extern volatile bool g_enteringLightSleep;
  if (g_enteringLightSleep) {
    Serial.println(F("[Mail] ⛔ Envoi annulé: système en transition vers light sleep"));
    return false;
  }
  
  // 2. Log du heap (informatif seulement, n'empêche plus l'envoi)
  uint32_t freeHeap = ESP.getFreeHeap();
  Serial.printf("[Mail] 📊 Heap disponible: %u bytes\n", freeHeap);
  if (freeHeap < 40000) {
    Serial.println(F("[Mail] ⚠️ Heap bas - tentative d'envoi quand même"));
  }
  
  // 3. Acquérir le mutex TLS (empêche collision SMTP/HTTPS)
  if (!TLSMutex::acquire(10000)) {  // Timeout 10s pour SMTP
    Serial.println(F("[Mail] ⛔ Envoi annulé: impossible d'acquérir le mutex TLS"));
    return false;
  }
  
  Serial.printf("[Mail] ✅ Mutex TLS acquis (heap: %u bytes)\n", ESP.getFreeHeap());
  // === FIN PROTECTION ===
  
  // Vérifier que le réseau est prêt avant de tenter SMTP
  if (!waitForNetworkReadyForSMTP()) {
    Serial.println(F("[Mail] ❌ Réseau non prêt, abandon envoi mail"));
    TLSMutex::release();
    return false;
  }
  
  if (!_ready || !_smtp.connected()) {
    uint32_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    if (largestBlock < HeapConfig::MIN_HEAP_BLOCK_FOR_MAIL_TLS) {
      Serial.printf("[Mail] ⛔ Connexion SMTP reportée: bloc contigu insuffisant (%u < %u bytes)\n",
                    (unsigned)largestBlock, (unsigned)HeapConfig::MIN_HEAP_BLOCK_FOR_MAIL_TLS);
      TLSMutex::release();
      return false;
    }
    Serial.println(F("[Mail] ⚠️ Connexion SMTP requise"));
    // tentative de reconnexion
    _smtp.closeSession();
    Serial.println(F("[Mail] Tentative de connexion SMTP..."));
    _ready = _smtp.connect(&_cfg);
    if(!_ready){
      // v11.179: Pas de String temporaire pour éviter crash dans destructeur
      Serial.printf("[Mail] ❌ Reconnexion SMTP échouée - code: %d\n", _smtp.statusCode());
      TLSMutex::release();  // v11.151: CRITIQUE - libérer le mutex avant return !
      return false;
    } else {
      Serial.println(F("[Mail] ✅ Connexion SMTP réussie"));
    }
  } else {
    Serial.println(F("[Mail] ✅ Connexion SMTP déjà active"));
  }
  
  Serial.println(F("[Mail] Trace 2: Connection OK"));

  // FIX: Utiliser des buffers statiques pour éviter les dangling pointers
  // La bibliothèque ESP Mail Client peut utiliser les pointeurs de manière asynchrone
  static char fromNameBuf[64];
  static char subjectBuf[128];
  // Piste 4: un seul buffer partagé (s_mailMessageBuffer) pour sendSync et sendAlertSync
  
  Serial.println(F("[Mail] Trace 3: Buffers allocated"));

  // Construire l'objet avec l'environnement de manière explicite
  const char* envName = Utils::getProfileName();
  
  // Construction du nom d'expéditeur dans un buffer statique
  snprintf(fromNameBuf, sizeof(fromNameBuf), "FFP5CS [%s]", envName ? envName : "");
  
  // Construction du sujet dans un buffer statique
  snprintf(subjectBuf, sizeof(subjectBuf), "[%s] %s", envName ? envName : "", subject ? subject : "");
  
  // Copier le message dans le buffer partagé seulement si différent (sendAlertSync passe déjà s_mailMessageBuffer)
  size_t msgLen = message ? strlen(message) : 0;
  if (message != s_mailMessageBuffer) {
    if (msgLen >= sizeof(s_mailMessageBuffer)) {
      msgLen = sizeof(s_mailMessageBuffer) - 1;
    }
    if (msgLen > 0) {
      strncpy(s_mailMessageBuffer, message, msgLen);
      s_mailMessageBuffer[msgLen] = '\0';
    } else {
      s_mailMessageBuffer[0] = '\0';
    }
  } else {
    msgLen = strlen(s_mailMessageBuffer);
  }
  
  // Vérifier si un footer complet est déjà présent (alerte critique)
  const char* footerMarker = "[Footer complet déjà inclus]";
  size_t markerLen = strlen(footerMarker);
  size_t currentLen = strlen(s_mailMessageBuffer);
  bool hasFullFooter = (currentLen >= markerLen) && 
                       (strcmp(s_mailMessageBuffer + currentLen - markerLen, footerMarker) == 0);
  
  if (hasFullFooter) {
    // Retirer le marqueur
    s_mailMessageBuffer[currentLen - markerLen] = '\0';
    Serial.println(F("[Mail] Trace 3.1: Footer complet déjà présent, pas d'ajout footer allégé"));
  } else {
    Serial.println(F("[Mail] Trace 3.1: Appending light footer..."));
    // Ajouter le footer allégé
    const char* lightFooter = buildLightFooter();
    // v11.179: Validation du pointeur (fix crash LoadProhibited)
    if (!lightFooter) {
      Serial.println(F("[Mail] ❌ buildLightFooter returned NULL"));
      lightFooter = "\n-- Footer unavailable --\n";
    }
    size_t footerLen = strlen(lightFooter);
    size_t remaining = sizeof(s_mailMessageBuffer) - currentLen - 1;
    // v11.178: Vérifier remaining > 0 avant strncat pour éviter underflow (audit bugs-high)
    if (remaining > 0 && footerLen < remaining) {
      strncat(s_mailMessageBuffer, lightFooter, remaining);
    } else if (remaining > 0) {
      // Tronquer le footer si nécessaire
      strncat(s_mailMessageBuffer, lightFooter, remaining - 1);
      s_mailMessageBuffer[sizeof(s_mailMessageBuffer) - 1] = '\0';
    }
  }
  
  Serial.println(F("[Mail] Trace 4: Message built"));

  // Configuration du message SMTP
  SMTP_Message msg;
  msg.sender.name  = fromNameBuf;
  msg.sender.email = Secrets::AUTHOR_EMAIL;
  msg.subject      = subjectBuf;
  msg.addRecipient(toName, toEmail);
  msg.text.content = s_mailMessageBuffer;
  
  Serial.println(F("[Mail] Trace 5: Msg struct configured"));

  // Affichage des détails du mail avant envoi avec informations temporelles
  time_t mailTime = getSafeEpochForDisplay();
  struct tm mailTimeInfo;
  localtime_r(&mailTime, &mailTimeInfo);
  char mailTimeBuf[32];
  strftime(mailTimeBuf, sizeof(mailTimeBuf), "%Y-%m-%d %H:%M:%S", &mailTimeInfo);
  
  Serial.println(F("[Mail] ===== DÉTAILS DU MAIL ====="));
  Serial.printf("[Mail] Heure d'envoi: %s (epoch: %lu)\n", mailTimeBuf, mailTime);
  Serial.print(F("[Mail] De: "));
  logSafeStr(msg.sender.name.c_str(), 60);
  Serial.print(F(" <"));
  logSafeStr(msg.sender.email.c_str(), 60);
  Serial.println(F(">"));
  Serial.print(F("[Mail] À: "));
  logSafeStr(toName, 40);
  Serial.print(F(" <"));
  logSafeStr(toEmail, 60);
  Serial.println(F(">"));
  Serial.print(F("[Mail] Objet: "));
  logSafeStr(msg.subject.c_str(), 80);
  Serial.println();
  Serial.println(F("[Mail] Contenu (aperçu):"));
  // Afficher un aperçu du message (max 200 caractères)
  size_t previewLen = strlen(s_mailMessageBuffer);
  if (previewLen > 200) previewLen = 200;
  char preview[201];
  strncpy(preview, s_mailMessageBuffer, previewLen);
  preview[previewLen] = '\0';
  Serial.println(preview);
  Serial.println(F("[Mail] ==========================="));
  
  Serial.println(F("[Mail] Trace 6: Calling sendMail..."));
  bool ok = MailClient.sendMail(&_smtp, &msg);
  Serial.printf("[Mail] Trace 7: sendMail returned %s\n", ok ? "TRUE" : "FALSE");

  if (!ok) {
    // v11.179: Pas de String temporaire pour éviter crash dans destructeur
    Serial.printf("[Mail] ERREUR sendMail code=%d err=%d\n", _smtp.statusCode(), _smtp.errorCode());
  } else {
    Serial.println(F("[Mail] Message SMTP envoyé avec succès ✔"));
  }
  
  // CRITIQUE: Fermer la session SMTP après chaque envoi pour éviter les callbacks
  // pendants qui peuvent causer un INT_WDT avec PC=0x0 si la connexion timeout
  // côté serveur (Gmail ferme les sessions inactives après quelques minutes)
  Serial.println(F("[Mail] Trace 8: Fermeture session SMTP..."));
  _smtp.closeSession();
  _ready = false;
  Serial.println(F("[Mail] ✅ Session SMTP fermée proprement"));

  // Libérer le mutex TLS (CRITIQUE - doit être fait après fermeture session)
  TLSMutex::release();
  
  return ok;
}
#else
bool Mailer::begin() { Serial.println("[Mail] Désactivé (FEATURE_MAIL=0)"); return true; }
bool Mailer::sendSync(const char*, const char*, const char*, const char*) { return false; }
bool Mailer::send(const char*, const char*, const char*, const char*) { return false; }
bool Mailer::sendAlert(const char* subject, const char* message, const char* toEmail, bool includeDetailedReport) {
  (void)subject; (void)message; (void)toEmail; (void)includeDetailedReport; return false;
}
bool Mailer::sendAlertSync(const char* subject, const char* message, const char* toEmail, bool includeDetailedReport) {
  (void)subject; (void)message; (void)toEmail; (void)includeDetailedReport; return false;
}
bool Mailer::sendSleepMail(const char* reason, uint32_t sleepDurationSeconds, const SensorReadings& readings) {
  (void)reason; (void)sleepDurationSeconds; (void)readings; return false;
}
bool Mailer::sendWakeMail(const char* reason, uint32_t actualSleepSeconds, const SensorReadings& readings) {
  (void)reason; (void)actualSleepSeconds; (void)readings; return false;
}
bool Mailer::initMailQueue() { return true; }
bool Mailer::processOneMailSync() { return false; }
bool Mailer::hasPendingMails() const { return false; }
uint32_t Mailer::getQueuedMails() const { return 0; }
#endif

#if FEATURE_MAIL && FEATURE_MAIL != 0
bool Mailer::sendAlertSync(const char* subject, const char* message, const char* toEmail, bool includeDetailedReport) {
  Serial.println(F("[Mail] ===== DIAGNOSTIC SENDALERT ====="));
  Serial.printf("[Mail] _ready: %s\n", _ready ? "TRUE" : "FALSE");
  Serial.printf("[Mail] subject: '%s'\n", subject ? subject : "NULL");
  size_t msgLen = message ? strlen(message) : 0;
  Serial.printf("[Mail] message length: %d\n", msgLen);
  Serial.printf("[Mail] toEmail: '%s'\n", toEmail ? toEmail : "NULL");
  Serial.printf("[Mail] includeDetailedReport: %s\n", includeDetailedReport ? "true" : "false");
  
  // Vérifications préalables
  if (!subject) {
    Serial.println(F("[Mail] ❌ ERREUR: subject est NULL"));
    return false;
  }
  if (!message || msgLen == 0) {
    Serial.println(F("[Mail] ❌ ERREUR: message vide"));
    return false;
  }
  
  // Utiliser fallback si toEmail vide (cohérent avec send() et sendAlert())
  const char* targetEmail = toEmail;
  if (!targetEmail || strlen(targetEmail) == 0) {
    Serial.println(F("[Mail] ⚠️ toEmail vide, utilisation DEFAULT_RECIPIENT"));
    targetEmail = EmailConfig::DEFAULT_RECIPIENT;
  }
  
  // Buffer statique pour le sujet
  static char alertSubject[128];
  snprintf(alertSubject, sizeof(alertSubject), "FFP5CS - %s", subject);
  Serial.printf("[Mail] alertSubject créer: '%s'\n", alertSubject);
  
  // Piste 4: utiliser le buffer partagé s_mailMessageBuffer (sendSync ne recopie pas si même pointeur)
  size_t offset = 0;
  size_t remaining = sizeof(s_mailMessageBuffer);
  int written = 0;
  
  // Copier le message initial
  size_t initialMsgLen = msgLen;
  if (initialMsgLen >= remaining) {
    initialMsgLen = remaining - 1;
  }
  strncpy(s_mailMessageBuffer, message, initialMsgLen);
  s_mailMessageBuffer[initialMsgLen] = '\0';
  offset = initialMsgLen;
  remaining -= initialMsgLen;
  Serial.printf("[Mail] s_mailMessageBuffer initial: %d chars\n", offset);
  
  // Rapport temporel détaillé uniquement pour alertes diagnostic (boot, OTA, panic)
  static Diagnostics tempDiag;
  tempDiag.loadFromNvs();
  bool isCritical = tempDiag.hasPanicInfo() || tempDiag.hasCrashInfo();
  if (includeDetailedReport) {
    const char* timeReport = buildDetailedTimeReport(tempDiag);
    if (!timeReport) {
      Serial.println(F("[Mail] ❌ buildDetailedTimeReport returned NULL"));
      return false;
    }
    if (remaining > 0) {
      written = snprintf(s_mailMessageBuffer + offset, remaining, "%s", timeReport);
      if (written > 0 && (size_t)written < remaining) {
        offset += written;
        remaining -= written;
      } else {
        s_mailMessageBuffer[sizeof(s_mailMessageBuffer) - 1] = '\0';
        remaining = 0;
      }
    }
    Serial.printf("[Mail] s_mailMessageBuffer après timeReport: %d chars\n", strlen(s_mailMessageBuffer));
  }
  
  Serial.println(F("[Mail] ===== ENVOI D'ALERTE (SYNC) ====="));
  Serial.printf("[Mail] Type: %s\n", includeDetailedReport ? "diagnostic" : "opérationnel");
  Serial.printf("[Mail] Destinataire: %s\n", targetEmail);
  Serial.printf("[Mail] Objet final: %s\n", alertSubject);
  Serial.println(F("[Mail] ==========================="));

  // Marqueur footer complet (alerte critique + rapport détaillé déjà inclus) - désactivé pour l'instant
  if (false && isCritical && remaining > 0) {
    const char* marker = "\n[Footer complet déjà inclus]";
    written = snprintf(s_mailMessageBuffer + offset, remaining, "%s", marker);
    if (written > 0 && (size_t)written < remaining) {
      offset += written;
      remaining -= written;
    }
  }
  
  bool result = sendSync(alertSubject, s_mailMessageBuffer, "User", targetEmail);
  Serial.printf("[Mail] ===== RÉSULTAT SENDALERTSYNC: %s =====\n", result ? "SUCCESS" : "FAILED");
  
  if (result && tempDiag.hasPanicInfo()) {
    extern Diagnostics diag;
    diag.clearPanicInfoAfterReport();
    Serial.println(F("[Mail] ✅ Infos PANIC nettoyées après envoi du mail"));
  }
  
  return result;
}

bool Mailer::sendSleepMail(const char* reason, uint32_t sleepDurationSeconds, const SensorReadings& readings) {
  static char sleepSubject[64];
  snprintf(sleepSubject, sizeof(sleepSubject), "FFP5CS - Mise en veille");
  
  static char sleepMessage[1024];
  size_t offset = 0;
  size_t remaining = sizeof(sleepMessage);
  int written = 0;
  
  // Construire le message avec snprintf()
  written = snprintf(sleepMessage + offset, remaining,
    "Le système FFP5CS entre en veille légère\n\n"
    "-- INFORMATIONS DE MISE EN VEILLE --\n"
    "Raison: %s\n"
    "Durée prévue: %u secondes\n"
    "Timestamp: ",
    reason ? reason : "N/A", sleepDurationSeconds);
  if (written < 0 || (size_t)written >= remaining) {
    sleepMessage[sizeof(sleepMessage) - 1] = '\0';
    return false;
  }
  offset += written;
  remaining -= written;
  
  // Ajouter l'heure actuelle
  time_t now = getSafeEpochForDisplay();
  struct tm timeinfo;
  if (localtime_r(&now, &timeinfo)) {
    char timeBuf[32];
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    written = snprintf(sleepMessage + offset, remaining, "%s\n", timeBuf);
  } else {
    written = snprintf(sleepMessage + offset, remaining, "Erreur heure\n");
  }
  if (written < 0 || (size_t)written >= remaining) {
    sleepMessage[sizeof(sleepMessage) - 1] = '\0';
    return false;
  }
  offset += written;
  remaining -= written;
  
  // Ajouter les informations système détaillées
  // UTILISE LES DERNIÈRES LECTURES (passées en paramètre) au lieu de relire les capteurs
  extern SystemActuators acts;
  written = snprintf(sleepMessage + offset, remaining,
    "\n-- ÉTAT SYSTÈME AVANT VEILLE --\n"
    "- Temp eau: %.1f °C\n"
    "- Temp air: %.1f °C\n"
    "- Aqua lvl: %u cm\n"
    "- Réserve lvl: %u cm\n"
    "- Pompe aquarium: %s\n"
    "- Pompe réservoir: %s\n"
    "- Chauffage: %s\n"
    "- Lumière: %s\n"
    "\n-- Configuration Sleep --\n"
    "- Délai d'activité: Configuration adaptative\n"
    "- Mode adaptatif: ACTIF\n",
    readings.tempWater, readings.tempAir, readings.wlAqua, readings.wlTank,
    acts.isAquaPumpRunning() ? "ON" : "OFF",
    acts.isTankPumpRunning() ? "ON" : "OFF",
    acts.isHeaterOn() ? "ON" : "OFF",
    acts.isLightOn() ? "ON" : "OFF");
  if (written < 0 || (size_t)written >= remaining) {
    sleepMessage[sizeof(sleepMessage) - 1] = '\0';
  }
  
  Serial.println(F("[Mail] ===== ENVOI MAIL VEILLE ====="));
  Serial.printf("[Mail] Raison: %s\n", reason);
  Serial.printf("[Mail] Durée: %u s\n", sleepDurationSeconds);
  Serial.println(F("[Mail] ⚡ Utilisation des dernières lectures (pas de nouvelle lecture capteurs)"));
  Serial.println(F("[Mail] =============================="));
  
  return sendSync(sleepSubject, sleepMessage, "User", EmailConfig::DEFAULT_RECIPIENT);
}

bool Mailer::sendWakeMail(const char* reason, uint32_t actualSleepSeconds, const SensorReadings& readings) {
  static char wakeSubject[64];
  snprintf(wakeSubject, sizeof(wakeSubject), "FFP5CS - Réveil du système");
  
  static char wakeMessage[1024];
  size_t offset = 0;
  size_t remaining = sizeof(wakeMessage);
  int written = 0;
  
  // Construire le message avec snprintf()
  written = snprintf(wakeMessage + offset, remaining,
    "Le système FFP5CS s'est réveillé de sa veille légère\n\n"
    "-- INFORMATIONS DE RÉVEIL --\n"
    "Raison: %s\n"
    "Durée réelle de veille: %u secondes\n"
    "Timestamp: ",
    reason ? reason : "N/A", actualSleepSeconds);
  if (written < 0 || (size_t)written >= remaining) {
    wakeMessage[sizeof(wakeMessage) - 1] = '\0';
    return false;
  }
  offset += written;
  remaining -= written;
  
  // Ajouter l'heure actuelle
  time_t now = getSafeEpochForDisplay();
  struct tm timeinfo;
  if (localtime_r(&now, &timeinfo)) {
    char timeBuf[32];
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    written = snprintf(wakeMessage + offset, remaining, "%s\n", timeBuf);
  } else {
    written = snprintf(wakeMessage + offset, remaining, "Erreur heure\n");
  }
  if (written < 0 || (size_t)written >= remaining) {
    wakeMessage[sizeof(wakeMessage) - 1] = '\0';
    return false;
  }
  offset += written;
  remaining -= written;
  
  // Ajouter les informations système détaillées
  // UTILISE LES DERNIÈRES LECTURES (passées en paramètre) au lieu de relire les capteurs
  extern SystemActuators acts;
  written = snprintf(wakeMessage + offset, remaining,
    "\n-- ÉTAT SYSTÈME AU RÉVEIL --\n"
    "- Temp eau: %.1f °C\n"
    "- Temp air: %.1f °C\n"
    "- Aqua lvl: %u cm\n"
    "- Réserve lvl: %u cm\n"
    "- Pompe aquarium: %s\n"
    "- Pompe réservoir: %s\n"
    "- Chauffage: %s\n"
    "- Lumière: %s\n"
    "\n-- Configuration Sleep --\n"
    "- Délai d'activité: Configuration adaptative\n"
    "- Mode adaptatif: ACTIF\n"
    "\n-- CONNEXION RÉSEAU --\n",
    readings.tempWater, readings.tempAir, readings.wlAqua, readings.wlTank,
    acts.isAquaPumpRunning() ? "ON" : "OFF",
    acts.isTankPumpRunning() ? "ON" : "OFF",
    acts.isHeaterOn() ? "ON" : "OFF",
    acts.isLightOn() ? "ON" : "OFF");
  if (written < 0 || (size_t)written >= remaining) {
    wakeMessage[sizeof(wakeMessage) - 1] = '\0';
    return false;
  }
  offset += written;
  remaining -= written;
  
  // Ajouter les informations de connexion WiFi
  if (WiFi.status() == WL_CONNECTED) {
    char ssid[33];
    char ip[16];
    WiFiHelpers::getSSID(ssid, sizeof(ssid));
    WiFiHelpers::getIPString(ip, sizeof(ip));
    written = snprintf(wakeMessage + offset, remaining,
      "- WiFi: Connecté\n"
      "- SSID: %s\n"
      "- IP: %s\n"
      "- RSSI: %d dBm\n",
      ssid, ip, WiFi.RSSI());
  } else {
    written = snprintf(wakeMessage + offset, remaining, "- WiFi: Déconnecté\n");
  }
  if (written < 0 || (size_t)written >= remaining) {
    wakeMessage[sizeof(wakeMessage) - 1] = '\0';
  }
  
  Serial.println(F("[Mail] ===== ENVOI MAIL RÉVEIL ====="));
  Serial.printf("[Mail] Raison: %s\n", reason);
  Serial.printf("[Mail] Durée veille: %u s\n", actualSleepSeconds);
  Serial.println(F("[Mail] =============================="));
  
  return sendSync(wakeSubject, wakeMessage, "User", EmailConfig::DEFAULT_RECIPIENT);
}

// ============================================================================
// MÉTHODES ASYNCHRONES (v11.142) - Non-bloquantes
// Ces méthodes ajoutent le mail à une queue et retournent immédiatement.
// v11.155: Traitement séquentiel depuis automationTask (plus de tâche dédiée)
// ============================================================================

uint32_t Mailer::getQueuedMails() const {
  if (!_mailQueue) return 0;
  return uxQueueMessagesWaiting(_mailQueue);
}

// Initialisation de la queue mail (sans tâche dédiée - v11.155: séquentiel)
bool Mailer::initMailQueue() {
  Serial.println(F("[Mail] >>> INITIALISATION QUEUE MAIL SEQUENTIELLE <<<"));
  
  // Créer la queue de mails
  _mailQueue = xQueueCreate(TaskConfig::MAIL_QUEUE_SIZE, sizeof(MailQueueItem));
  if (!_mailQueue) {
    Serial.println(F("[Mail] ❌ Échec création queue mail"));
    return false;
  }
  Serial.printf("[Mail] ✅ Queue mail créée (%d slots, traitement séquentiel)\n", TaskConfig::MAIL_QUEUE_SIZE);
  
  return true;
}

// Traitement séquentiel d'un mail depuis la queue (appelé depuis automationTask)
// Retourne true si un mail a été traité, false si aucun mail en attente
bool Mailer::processOneMailSync() {
  if (!_mailQueue) {
    return false; // Queue non initialisée
  }
  
  MailQueueItem item;
  
  // Lire un mail de la queue (non-bloquant)
  if (xQueueReceive(_mailQueue, &item, 0) != pdTRUE) {
    return false; // Aucun mail en attente
  }
  
  Serial.printf("[Mail] 📬 Traitement mail séquentiel: '%s'\n", item.subject);

  bool success;
  if (item.isAlert) {
    success = sendAlertSync(item.subject, item.message, item.toEmail, item.includeDetailedReport);
  } else {
    success = sendSync(item.subject, item.message, "User", item.toEmail);
  }
  
  if (success) {
    _mailsSent++;
    Serial.printf("[Mail] ✅ Mail SMTP envoyé avec succès (%u total)\n", _mailsSent);
  } else {
    // Re-queue une fois pour retry (échec transitoire WiFi/TLS), max 2 tentatives au total
    if (item.retryCount < 2) {
      item.retryCount++;
      if (xQueueSendToFront(_mailQueue, &item, 0) == pdTRUE) {
        Serial.printf("[Mail] 🔄 Mail remis en queue (retry %u/2): '%s'\n", item.retryCount, item.subject);
      } else {
        _mailsFailed++;
        Serial.printf("[Mail] ❌ Échec envoi mail (%u échecs)\n", _mailsFailed);
      }
    } else {
      _mailsFailed++;
      Serial.printf("[Mail] ❌ Échec envoi mail (%u échecs)\n", _mailsFailed);
    }
  }
  
  return true; // Un mail a été traité
}

// Vérification si des mails sont en attente
bool Mailer::hasPendingMails() const {
  if (!_mailQueue) return false;
  return uxQueueMessagesWaiting(_mailQueue) > 0;
}

// Méthode send() asynchrone - ajoute à la queue et retourne immédiatement
bool Mailer::send(const char* subject, const char* message, const char* toName, const char* toEmail) {
  (void)toName; // Non utilisé dans la version asynchrone
  
  if (!_mailQueue) {
    Serial.println(F("[Mail] ⚠️ Queue non initialisée, envoi synchrone..."));
    return sendSync(subject, message, toName, toEmail);
  }
  
  MailQueueItem item;
  memset(&item, 0, sizeof(item));
  
  // Copie sécurisée des données
  if (subject) {
    strncpy(item.subject, subject, sizeof(item.subject) - 1);
  }
  if (message) {
    strncpy(item.message, message, sizeof(item.message) - 1);
  }
  if (toEmail) {
    strncpy(item.toEmail, toEmail, sizeof(item.toEmail) - 1);
  } else {
    strncpy(item.toEmail, EmailConfig::DEFAULT_RECIPIENT, sizeof(item.toEmail) - 1);
  }
  item.isAlert = false;
  item.retryCount = 0;
  
  // Ajoute à la queue (timeout 100ms), retry une fois après 200ms si pleine (robustesse)
  if (xQueueSend(_mailQueue, &item, pdMS_TO_TICKS(100)) != pdTRUE) {
    vTaskDelay(pdMS_TO_TICKS(200));
    if (xQueueSend(_mailQueue, &item, pdMS_TO_TICKS(100)) != pdTRUE) {
      Serial.println(F("[Mail] ⚠️ Queue pleine, mail ignoré"));
      return false;
    }
  }
  
  Serial.printf("[Mail] 📥 Mail ajouté à la queue (%u en attente): '%s'\n",
                getQueuedMails(), subject);
  return true;
}

// Méthode sendAlert() asynchrone - ajoute à la queue et retourne immédiatement
bool Mailer::sendAlert(const char* subject, const char* message, const char* toEmail, bool includeDetailedReport) {
  Serial.println(F("[Mail] ===== SENDALERT ASYNC (v11.142) ====="));
  
  if (!_mailQueue) {
    Serial.println(F("[Mail] ⚠️ Queue non initialisée, envoi synchrone..."));
    return sendAlertSync(subject, message, toEmail, includeDetailedReport);
  }
  
  // Vérifications préalables
  if (!subject) {
    Serial.println(F("[Mail] ❌ ERREUR: subject est NULL"));
    return false;
  }
  if (!message || strlen(message) == 0) {
    Serial.println(F("[Mail] ❌ ERREUR: message vide"));
    return false;
  }
  
  MailQueueItem item;
  memset(&item, 0, sizeof(item));
  
  // Copie sécurisée des données
  strncpy(item.subject, subject, sizeof(item.subject) - 1);
  
  // Tronquer le message si trop long
  size_t msgLen = strlen(message);
  if (msgLen >= sizeof(item.message)) {
    Serial.printf("[Mail] ⚠️ Message tronqué de %u à %u caractères\n", 
                  msgLen, sizeof(item.message) - 1);
    msgLen = sizeof(item.message) - 1;
  }
  strncpy(item.message, message, msgLen);
  item.message[msgLen] = '\0';
  
  // Utiliser fallback si toEmail vide (cohérent avec send())
  if (toEmail && strlen(toEmail) > 0) {
    strncpy(item.toEmail, toEmail, sizeof(item.toEmail) - 1);
  } else {
    Serial.println(F("[Mail] ⚠️ toEmail vide, utilisation DEFAULT_RECIPIENT"));
    strncpy(item.toEmail, EmailConfig::DEFAULT_RECIPIENT, sizeof(item.toEmail) - 1);
  }
  item.isAlert = true;
  item.includeDetailedReport = includeDetailedReport;
  item.retryCount = 0;
  
  // Ajoute à la queue (timeout 100ms), retry une fois après 200ms si pleine (robustesse)
  if (xQueueSend(_mailQueue, &item, pdMS_TO_TICKS(100)) != pdTRUE) {
    vTaskDelay(pdMS_TO_TICKS(200));
    if (xQueueSend(_mailQueue, &item, pdMS_TO_TICKS(100)) != pdTRUE) {
      Serial.println(F("[Mail] ⚠️ Queue pleine, alerte ignorée"));
      return false;
    }
  }
  
  Serial.printf("[Mail] 📥 Alerte ajoutée à la queue (%u en attente): '%s'\n", 
                getQueuedMails(), subject);
  Serial.println(F("[Mail] ✅ Retour immédiat (non-bloquant)"));
  return true;
}
#endif