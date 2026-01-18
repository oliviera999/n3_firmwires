// Mailer implementation can be compiled out to save flash on ESP32 WROOM
#include "mailer.h"
#include "config.h"
#include "system_sensors.h"
#include "system_actuators.h"
#include "diagnostics.h"
#include "nvs_manager.h"
#include "tls_mutex.h"  // v11.149: Mutex pour sérialiser TLS (SMTP/HTTPS)
#include <WiFi.h>
#include <time.h>
#include <LittleFS.h>
#include <esp_task_wdt.h> // Pour esp_task_wdt_reset() dans mailTask

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
static const char* kLittleFsLabel = "littlefs";

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
  
  written = snprintf(buf, remaining, "\n\n-- Infos système --\n"
                                     "- Version: %s\n"
                                     "- Carte: %s\n"
                                     "- Environnement: %s\n",
                     ProjectConfig::VERSION, board, Utils::getProfileName());
  if (written < 0 || (size_t)written >= remaining) {
    buf[remaining - 1] = '\0';
    return buf;
  }
  buf += written;
  remaining -= written;

  // Réseau
  bool connected = WiFi.isConnected();
  const char* ssid = connected ? WiFi.SSID().c_str() : "(déconnecté)";
  IPAddress ipAddr = connected ? WiFi.localIP() : IPAddress(0, 0, 0, 0);
  long rssi = connected ? WiFi.RSSI() : 0;
  const char* mac = WiFi.macAddress().c_str();
  
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
    const char* host = WiFi.getHostname();
    const char* bssid = WiFi.BSSIDstr().c_str();
    
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
    written = snprintf(buf, remaining, "- AP SSID: %s\n"
                                       "- AP IP: %d.%d.%d.%d\n",
                       WiFi.softAPSSID().c_str(), apIp[0], apIp[1], apIp[2], apIp[3]);
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
  
  time_t now = time(nullptr);
  written = snprintf(buf, remaining, "- Epoch: %lu\n", (unsigned long)now);
  if (written < 0 || (size_t)written >= remaining) {
    buf[remaining - 1] = '\0';
    return g_systemInfoFooterBuffer;
  }
  buf += written;
  remaining -= written;
  
  if (now > 100000) {
    struct tm tmInfo;
    localtime_r(&now, &tmInfo);
    char tbuf[32];
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tmInfo);
    written = snprintf(buf, remaining, "- Local time: %s\n"
                                       "- Jour de la semaine: %d (0=dimanche)\n"
                                       "- Jour de l'année: %d\n"
                                       "- DST actif: %s\n"
                                       "- Timezone: Maroc UTC+1\n",
                       tbuf, tmInfo.tm_wday, tmInfo.tm_yday, tmInfo.tm_isdst ? "OUI" : "NON");
    if (written < 0 || (size_t)written >= remaining) {
      buf[remaining - 1] = '\0';
      return g_systemInfoFooterBuffer;
    }
    buf += written;
    remaining -= written;
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
  
  // Informations de dérive temporelle si disponibles
  // extern TimeDriftMonitor timeDriftMonitor;
  // if (timeDriftMonitor.isDriftCalculated()) {
  //   footer += "- Dérive PPM: "; footer += String(timeDriftMonitor.getDriftPPM(), 2); footer += "\n";
  //   footer += "- Dérive secondes: "; footer += String(timeDriftMonitor.getDriftSeconds(), 2); footer += "\n";
  //   time_t lastSync = timeDriftMonitor.getLastSyncTime();
  //   if (lastSync > 0) {
  //     struct tm syncInfo;
  //     localtime_r(&lastSync, &syncInfo);
  //     char syncBuf[32];
  //     strftime(syncBuf, sizeof(syncBuf), "%Y-%m-%d %H:%M:%S", &syncInfo);
  //     footer += "- Dernière sync NTP: "; footer += syncBuf; footer += "\n";
  //   }
  // } else {
  //   footer += "- Dérive: Non calculée\n";
  // }
  
  // Informations RTC/Flash
  unsigned long savedEpoch;
  g_nvsManager.loadULong(NVS_NAMESPACES::TIME, "rtc_epoch", savedEpoch, 0);
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

  // Mémoire
  size_t psram = ESP.getPsramSize();
  if (psram > 0) {
    written = snprintf(buf, remaining, "- Heap free: %u bytes\n"
                                       "- Heap min free: %u bytes\n"
                                       "- Heap max alloc: %u bytes\n"
                                       "- PSRAM size: %zu bytes\n"
                                       "- PSRAM free: %u bytes\n",
                       ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(), psram, ESP.getFreePsram());
  } else {
    written = snprintf(buf, remaining, "- Heap free: %u bytes\n"
                                       "- Heap min free: %u bytes\n"
                                       "- Heap max alloc: %u bytes\n",
                       ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
  }
  if (written < 0 || (size_t)written >= remaining) {
    buf[remaining - 1] = '\0';
    return g_systemInfoFooterBuffer;
  }
  buf += written;
  remaining -= written;

  // Diagnostics persistés si disponibles
  int rebootCount;
  g_nvsManager.loadInt(NVS_NAMESPACES::LOGS, "diag_rebootCnt", rebootCount, 0);
  int minHeap;
  g_nvsManager.loadInt(NVS_NAMESPACES::LOGS, "diag_minHeap", minHeap, 0);
  if (minHeap > 0) {
    written = snprintf(buf, remaining, "- Reboots: %d\n"
                                       "- Min heap: %d bytes\n",
                       rebootCount, minHeap);
  } else {
    written = snprintf(buf, remaining, "- Reboots: %d\n", rebootCount);
  }
  if (written < 0 || (size_t)written >= remaining) {
    buf[remaining - 1] = '\0';
    return g_systemInfoFooterBuffer;
  }
  buf += written;
  remaining -= written;

  // Uptime
  const char* uptimeStr = formatUptime(millis());
  written = snprintf(buf, remaining, "- Uptime: %s\n", uptimeStr);
  if (written < 0 || (size_t)written >= remaining) {
    buf[remaining - 1] = '\0';
    return g_systemInfoFooterBuffer;
  }
  buf += written;
  remaining -= written;

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

  // Filesystem (LittleFS)
  if (LittleFS.begin(false, "/littlefs", 10, kLittleFsLabel)) {
    size_t total = LittleFS.totalBytes();
    size_t used  = LittleFS.usedBytes();
    written = snprintf(buf, remaining, "- FS LittleFS: %zu/%zu bytes\n", used, total);
    if (written < 0 || (size_t)written >= remaining) {
      buf[remaining - 1] = '\0';
      return g_systemInfoFooterBuffer;
    }
    buf += written;
    remaining -= written;
    // Ne pas démonter LittleFS ici pour éviter les erreurs de montage dans d'autres modules
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
    // Namespace bouffe
    bool bouffeMatinOk, bouffeMidiOk, bouffeSoirOk, pompeAquaLocked, forceWakeUp;
    int lastJourBouf;
    g_nvsManager.loadBool(NVS_NAMESPACES::CONFIG, "bouffe_matin", bouffeMatinOk, false);
    g_nvsManager.loadBool(NVS_NAMESPACES::CONFIG, "bouffe_midi", bouffeMidiOk, false);
    g_nvsManager.loadBool(NVS_NAMESPACES::CONFIG, "bouffe_soir", bouffeSoirOk, false);
    g_nvsManager.loadInt(NVS_NAMESPACES::CONFIG, "bouffe_jour", lastJourBouf, -1);
    g_nvsManager.loadBool(NVS_NAMESPACES::CONFIG, "bf_pmp_lock", pompeAquaLocked, false);
    g_nvsManager.loadBool(NVS_NAMESPACES::CONFIG, "bf_force_wk", forceWakeUp, false);
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

    // Namespace ota
    bool otaUpdateFlag;
    g_nvsManager.loadBool(NVS_NAMESPACES::SYSTEM, "ota_updateFlag", otaUpdateFlag, true);
    written = snprintf(buf, remaining, "- ota.updateFlag: %s\n", otaUpdateFlag ? "true" : "false");
    if (written < 0 || (size_t)written >= remaining) {
      buf[remaining - 1] = '\0';
      return g_systemInfoFooterBuffer;
    }
    buf += written;
    remaining -= written;

    // Namespace remoteVars (aperçu) - v11.103: Utilisation du gestionnaire NVS centralisé
    String remoteJson;
    g_nvsManager.loadJsonDecompressed(NVS_NAMESPACES::CONFIG, "remote_json", remoteJson, "");
    if (remoteJson.length() > 0) {
      size_t previewLen = min((size_t)200, (size_t)remoteJson.length());
      char previewBuf[201];
      strncpy(previewBuf, remoteJson.c_str(), previewLen);
      previewBuf[previewLen] = '\0';
      if (remoteJson.length() > 200) {
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

    // Namespace rtc
    unsigned long savedEpoch2;
    g_nvsManager.loadULong(NVS_NAMESPACES::TIME, "rtc_epoch", savedEpoch2, 0);
    written = snprintf(buf, remaining, "- rtc.epoch: %lu\n", savedEpoch2);
    if (written < 0 || (size_t)written >= remaining) {
      buf[remaining - 1] = '\0';
      return g_systemInfoFooterBuffer;
    }
    buf += written;
    remaining -= written;

    // Namespace diagnostics (déjà partiellement inclus plus haut, rappel condensé)
    int rebootCnt2, minHeap2;
    g_nvsManager.loadInt(NVS_NAMESPACES::LOGS, "diag_rebootCnt", rebootCnt2, 0);
    g_nvsManager.loadInt(NVS_NAMESPACES::LOGS, "diag_minHeap", minHeap2, 0);
    written = snprintf(buf, remaining, "- diagnostics.rebootCnt: %d\n"
                                       "- diagnostics.minHeap: %d\n",
                       rebootCnt2, minHeap2);
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
  
  time_t now = time(nullptr);
  struct tm timeinfo;
  if (localtime_r(&now, &timeinfo)) {
    char timeBuf[64];
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    
    // Informations sur le temps de fonctionnement
    unsigned long uptimeMs = millis();
    unsigned long uptimeSec = uptimeMs / 1000;
    unsigned long uptimeMin = uptimeSec / 60;
    unsigned long uptimeHour = uptimeMin / 60;
    unsigned long uptimeDay = uptimeHour / 24;
    
    // Construire uptime string
    char uptimeStr[64] = "";
    if (uptimeDay > 0) {
      snprintf(uptimeStr, sizeof(uptimeStr), "%luj ", uptimeDay);
    }
    if (uptimeHour % 24 > 0) {
      size_t len = strlen(uptimeStr);
      snprintf(uptimeStr + len, sizeof(uptimeStr) - len, "%luh ", uptimeHour % 24);
    }
    if (uptimeMin % 60 > 0) {
      size_t len = strlen(uptimeStr);
      snprintf(uptimeStr + len, sizeof(uptimeStr) - len, "%lum ", uptimeMin % 60);
    }
    size_t len = strlen(uptimeStr);
    snprintf(uptimeStr + len, sizeof(uptimeStr) - len, "%lus", uptimeSec % 60);
    
    written = snprintf(buf, remaining, "Heure actuelle: %s\n"
                                       "Epoch: %lu\n"
                                       "Jour de la semaine: %d (0=dimanche)\n"
                                       "Jour de l'année: %d\n"
                                       "DST actif: %s\n"
                                       "Uptime: %s\n"
                                       "Serveur NTP: %s\n"
                                       "GMT Offset: +%ldh\n"
                                       "DST Offset: +%ldh\n",
                       timeBuf, (unsigned long)now, timeinfo.tm_wday, timeinfo.tm_yday,
                       timeinfo.tm_isdst ? "OUI" : "NON", uptimeStr,
                       SystemConfig::NTP_SERVER,
                       SystemConfig::NTP_GMT_OFFSET_SEC/3600,
                       SystemConfig::NTP_DAYLIGHT_OFFSET_SEC/3600);
    if (written < 0 || (size_t)written >= remaining) {
      buf[remaining - 1] = '\0';
      return g_detailedTimeReportBuffer;
    }
    buf += written;
    remaining -= written;
    
    // Informations de dérive si disponibles
    // extern TimeDriftMonitor timeDriftMonitor;
    // if (timeDriftMonitor.isDriftCalculated()) {
    //   report += "Dérive PPM: "; report += String(timeDriftMonitor.getDriftPPM(), 3); report += "\n";
    //   report += "Dérive secondes: "; report += String(timeDriftMonitor.getDriftSeconds(), 3); report += "\n";
    //   time_t lastSync = timeDriftMonitor.getLastSyncTime();
    //   if (lastSync > 0) {
    //     struct tm syncInfo;
    //     localtime_r(&lastSync, &syncInfo);
    //     char syncBuf[32];
    //     strftime(syncBuf, sizeof(syncBuf), "%Y-%m-%d %H:%M:%S", &syncInfo);
    //     report += "Dernière sync NTP: "; report += syncBuf; report += "\n";
    //     
    //     // Calcul du temps écoulé depuis la dernière sync
    //     time_t timeSinceSync = now - lastSync;
    //     if (timeSinceSync < 3600) {
    //       report += "Temps depuis sync: "; report += String(timeSinceSync/60); report += " minutes\n";
    //     } else if (timeSinceSync < 86400) {
    //       report += "Temps depuis sync: "; report += String(timeSinceSync/3600); report += " heures\n";
    //     } else {
    //       report += "Temps depuis sync: "; report += String(timeSinceSync/86400); report += " jours\n";
    //     }
    //   }
    // } else {
    //   report += "Dérive: Non calculée\n";
    // }
    
    // Informations RTC/Flash
    unsigned long savedEpoch;
    g_nvsManager.loadULong(NVS_NAMESPACES::TIME, "rtc_epoch", savedEpoch, 0);
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
  
  // Ajouter le rapport de redémarrage (qui retourne un String)
  String restartReport = diagnostics.generateRestartReport();
  size_t restartLen = restartReport.length();
  if (restartLen > 0 && restartLen < remaining) {
    strncpy(buf, restartReport.c_str(), remaining - 1);
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

  // DEBUG: Activer les logs SMTP détaillés
  _smtp.debug(1);
  
  // v11.151: Reconnexion automatique et timeout augmenté
  MailClient.networkReconnect(true);
  _smtp.setTCPTimeout(30);  // 30 secondes de timeout TCP

  // Diagnostic de la configuration
  Serial.printf("[Mail] SMTP_HOST: '%s'\n", EmailConfig::SMTP_HOST);
  Serial.printf("[Mail] SMTP_PORT: %d\n", EmailConfig::SMTP_PORT);
  Serial.printf("[Mail] AUTHOR_EMAIL: '%s'\n", Secrets::AUTHOR_EMAIL);
  Serial.printf("[Mail] AUTHOR_PASSWORD length: %d\n", strlen(Secrets::AUTHOR_PASSWORD));
  
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
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[Mail] waitForNetworkReady: WiFi non connecté, abandon"));
    return false;
  }
  
  const uint32_t STABILIZATION_DELAY_MS = 2000;  // 2 secondes de stabilisation (plus long pour SMTP TLS)
  const uint32_t MAX_WAIT_MS = 8000;             // 8 secondes max d'attente totale
  uint32_t startMs = millis();
  
  Serial.println(F("[Mail] Attente stabilisation réseau pour SMTP..."));
  
  // Phase 1: Délai minimum de stabilisation TCP/IP
  vTaskDelay(pdMS_TO_TICKS(STABILIZATION_DELAY_MS));
  
  // Phase 2: Vérifier que l'IP est toujours valide et DNS fonctionne
  while ((millis() - startMs) < MAX_WAIT_MS) {
    if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
      // Test DNS rapide pour vérifier que le réseau est vraiment opérationnel
      IPAddress dnsResult;
      if (WiFi.hostByName("smtp.gmail.com", dnsResult)) {
        Serial.printf("[Mail] ✅ Réseau prêt pour SMTP (%s, DNS OK, %lu ms)\n", 
                      WiFi.localIP().toString().c_str(), millis() - startMs);
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
    Serial.println(F("[Mail] ⚠️ Connexion SMTP requise"));
    // tentative de reconnexion
    _smtp.closeSession();
    Serial.println(F("[Mail] Tentative de connexion SMTP..."));
    _ready = _smtp.connect(&_cfg);
    if(!_ready){
      Serial.printf("[Mail] ❌ Reconnexion SMTP échouée - code: %d\n", _smtp.statusCode());
      Serial.printf("[Mail] ❌ Erreur: %s\n", _smtp.errorReason().c_str());
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
  static String finalMessageStatic;  // String statique pour persistance
  
  Serial.println(F("[Mail] Trace 3: Buffers allocated"));

  // Construire l'objet avec l'environnement de manière explicite
  const char* envName = Utils::getProfileName();
  
  // Construction du nom d'expéditeur dans un buffer statique
  snprintf(fromNameBuf, sizeof(fromNameBuf), "FFP5CS [%s]", envName ? envName : "");
  
  // Construction du sujet dans un buffer statique
  snprintf(subjectBuf, sizeof(subjectBuf), "[%s] %s", envName ? envName : "", subject ? subject : "");
  
  // Construction du message final (String statique pour persistance)
  // Note: buildSystemInfoFooter() retourne maintenant const char* (buffer statique)
  finalMessageStatic = message ? message : "";
  Serial.println(F("[Mail] Trace 3.1: Appending footer..."));
  // finalMessageStatic += buildSystemInfoFooter(); // DÉSACTIVÉ POUR TEST CRASH
  finalMessageStatic += "\n\n[Footer désactivé pour debug]";
  
  Serial.println(F("[Mail] Trace 4: Message built"));

  // Configuration du message SMTP
  SMTP_Message msg;
  msg.sender.name  = fromNameBuf;
  msg.sender.email = Secrets::AUTHOR_EMAIL;
  msg.subject      = subjectBuf;
  msg.addRecipient(toName, toEmail);
  msg.text.content = finalMessageStatic.c_str();
  
  Serial.println(F("[Mail] Trace 5: Msg struct configured"));

  // Affichage des détails du mail avant envoi avec informations temporelles
  time_t mailTime = time(nullptr);
  struct tm mailTimeInfo;
  localtime_r(&mailTime, &mailTimeInfo);
  char mailTimeBuf[32];
  strftime(mailTimeBuf, sizeof(mailTimeBuf), "%Y-%m-%d %H:%M:%S", &mailTimeInfo);
  
  Serial.println(F("[Mail] ===== DÉTAILS DU MAIL ====="));
  Serial.printf("[Mail] Heure d'envoi: %s (epoch: %lu)\n", mailTimeBuf, mailTime);
  Serial.printf("[Mail] De: %s <%s>\n", msg.sender.name, msg.sender.email);
  Serial.printf("[Mail] À: %s <%s>\n", toName, toEmail);
  Serial.printf("[Mail] Objet: %s\n", msg.subject);
  Serial.println(F("[Mail] Contenu (aperçu):"));
  Serial.println(finalMessageStatic.substring(0, 200));
  Serial.println(F("[Mail] ==========================="));
  
  Serial.println(F("[Mail] Trace 6: Calling sendMail..."));
  bool ok = MailClient.sendMail(&_smtp, &msg);
  Serial.printf("[Mail] Trace 7: sendMail returned %s\n", ok ? "TRUE" : "FALSE");

  if (!ok) {
    Serial.printf("[Mail] ERREUR sendMail code=%d err=%d reason=%s\n", _smtp.statusCode(), _smtp.errorCode(), _smtp.errorReason().c_str());
  } else {
    Serial.println(F("[Mail] Message envoyé avec succès ✔"));
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
bool Mailer::sendAlert(const char* subject, const String& message, const char* toEmail) {
  (void)subject; (void)message; (void)toEmail; return false;
}
bool Mailer::sendAlertSync(const char* subject, const String& message, const char* toEmail) {
  (void)subject; (void)message; (void)toEmail; return false;
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
bool Mailer::sendAlertSync(const char* subject, const String& message, const char* toEmail) {
  Serial.println(F("[Mail] ===== DIAGNOSTIC SENDALERT ====="));
  Serial.printf("[Mail] _ready: %s\n", _ready ? "TRUE" : "FALSE");
  Serial.printf("[Mail] subject: '%s'\n", subject ? subject : "NULL");
  Serial.printf("[Mail] message length: %d\n", message.length());
  Serial.printf("[Mail] toEmail: '%s'\n", toEmail ? toEmail : "NULL");
  
  // Vérifications préalables
  if (!subject) {
    Serial.println(F("[Mail] ❌ ERREUR: subject est NULL"));
    return false;
  }
  if (!toEmail || strlen(toEmail) == 0) {
    Serial.println(F("[Mail] ❌ ERREUR: toEmail est vide/NULL (configuration incomplète)"));
    return false;
  }
  if (message.length() == 0) {
    Serial.println(F("[Mail] ❌ ERREUR: message vide"));
    return false;
  }
  
  String alertSubject = String("FFP5CS - ") + subject;
  Serial.printf("[Mail] alertSubject créer: '%s'\n", alertSubject.c_str());
  
  // Ajouter le rapport temporel détaillé au message d'alerte
  String enhancedMessage = message;
  Serial.printf("[Mail] enhancedMessage initial: %d chars\n", enhancedMessage.length());
  
  // Instance temporaire de Diagnostics pour lire les infos persistées (sans effets de bord)
  Diagnostics tempDiag;
  tempDiag.loadFromNvs();
  const char* timeReport = buildDetailedTimeReport(tempDiag);
  enhancedMessage += timeReport;
  Serial.printf("[Mail] enhancedMessage final: %d chars\n", enhancedMessage.length());
  
  Serial.println(F("[Mail] ===== ENVOI D'ALERTE (SYNC) ====="));
  Serial.printf("[Mail] Type: Alerte système\n");
  Serial.printf("[Mail] Destinataire: %s\n", toEmail);
  Serial.printf("[Mail] Objet original: %s\n", subject);
  Serial.printf("[Mail] Objet final: %s\n", alertSubject.c_str());
  Serial.println(F("[Mail] ==========================="));
  
  bool result = sendSync(alertSubject.c_str(), enhancedMessage.c_str(), "User", toEmail);
  Serial.printf("[Mail] ===== RÉSULTAT SENDALERTSYNC: %s =====\n", result ? "SUCCESS" : "FAILED");
  
  // Nettoyer les infos PANIC après l'envoi réussi du mail (pour éviter de les réutiliser au prochain boot)
  // Utiliser l'instance globale de Diagnostics pour nettoyer les infos dans NVS
  if (result && tempDiag.hasPanicInfo()) {
    extern Diagnostics diag;
    diag.clearPanicInfoAfterReport();
    Serial.println(F("[Mail] ✅ Infos PANIC nettoyées après envoi du mail"));
  }
  
  return result;
}

bool Mailer::sendSleepMail(const char* reason, uint32_t sleepDurationSeconds, const SensorReadings& readings) {
  String sleepSubject = String("FFP5CS - Mise en veille");
  
  String sleepMessage;
  sleepMessage.reserve(1024);
  sleepMessage += "Le système FFP5CS entre en veille légère\n\n";
  sleepMessage += "-- INFORMATIONS DE MISE EN VEILLE --\n";
  sleepMessage += "Raison: "; sleepMessage += reason; sleepMessage += "\n";
  sleepMessage += "Durée prévue: "; sleepMessage += String(sleepDurationSeconds); sleepMessage += " secondes\n";
  sleepMessage += "Timestamp: "; 
  
  // Ajouter l'heure actuelle
  time_t now = time(nullptr);
  struct tm timeinfo;
  if (localtime_r(&now, &timeinfo)) {
    char timeBuf[32];
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    sleepMessage += timeBuf;
  } else {
    sleepMessage += "Erreur heure";
  }
  sleepMessage += "\n";
  
  // Ajouter les informations système détaillées
  // UTILISE LES DERNIÈRES LECTURES (passées en paramètre) au lieu de relire les capteurs
  sleepMessage += "\n-- ÉTAT SYSTÈME AVANT VEILLE --\n";
  extern SystemActuators acts;
  sleepMessage += "- Temp eau: "; sleepMessage += String(readings.tempWater, 1); sleepMessage += " °C\n";
  sleepMessage += "- Temp air: "; sleepMessage += String(readings.tempAir, 1); sleepMessage += " °C\n";
  sleepMessage += "- Aqua lvl: "; sleepMessage += String(readings.wlAqua); sleepMessage += " cm\n";
  sleepMessage += "- Réserve lvl: "; sleepMessage += String(readings.wlTank); sleepMessage += " cm\n";
  sleepMessage += "- Pompe aquarium: "; sleepMessage += (acts.isAquaPumpRunning() ? "ON" : "OFF"); sleepMessage += "\n";
  sleepMessage += "- Pompe réservoir: "; sleepMessage += (acts.isTankPumpRunning() ? "ON" : "OFF"); sleepMessage += "\n";
  sleepMessage += "- Chauffage: "; sleepMessage += (acts.isHeaterOn() ? "ON" : "OFF"); sleepMessage += "\n";
  sleepMessage += "- Lumière: "; sleepMessage += (acts.isLightOn() ? "ON" : "OFF"); sleepMessage += "\n";
  
  // Ajouter les informations de délai d'activité
  sleepMessage += "\n-- Configuration Sleep --\n";
  sleepMessage += "- Délai d'activité: Configuration adaptative\n";
  sleepMessage += "- Mode adaptatif: ACTIF\n";
  
  Serial.println(F("[Mail] ===== ENVOI MAIL VEILLE ====="));
  Serial.printf("[Mail] Raison: %s\n", reason);
  Serial.printf("[Mail] Durée: %u s\n", sleepDurationSeconds);
  Serial.println(F("[Mail] ⚡ Utilisation des dernières lectures (pas de nouvelle lecture capteurs)"));
  Serial.println(F("[Mail] =============================="));
  
  return sendSync(sleepSubject.c_str(), sleepMessage.c_str(), "User", EmailConfig::DEFAULT_RECIPIENT);
}

bool Mailer::sendWakeMail(const char* reason, uint32_t actualSleepSeconds, const SensorReadings& readings) {
  String wakeSubject = String("FFP5CS - Réveil du système");
  
  String wakeMessage;
  wakeMessage.reserve(1024);
  wakeMessage += "Le système FFP5CS s'est réveillé de sa veille légère\n\n";
  wakeMessage += "-- INFORMATIONS DE RÉVEIL --\n";
  wakeMessage += "Raison: "; wakeMessage += reason; wakeMessage += "\n";
  wakeMessage += "Durée réelle de veille: "; wakeMessage += String(actualSleepSeconds); wakeMessage += " secondes\n";
  wakeMessage += "Timestamp: "; 
  
  // Ajouter l'heure actuelle
  time_t now = time(nullptr);
  struct tm timeinfo;
  if (localtime_r(&now, &timeinfo)) {
    char timeBuf[32];
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    wakeMessage += timeBuf;
  } else {
    wakeMessage += "Erreur heure";
  }
  wakeMessage += "\n";
  
  // Ajouter les informations système détaillées
  // UTILISE LES DERNIÈRES LECTURES (passées en paramètre) au lieu de relire les capteurs
  wakeMessage += "\n-- ÉTAT SYSTÈME AU RÉVEIL --\n";
  extern SystemActuators acts;
  wakeMessage += "- Temp eau: "; wakeMessage += String(readings.tempWater, 1); wakeMessage += " °C\n";
  wakeMessage += "- Temp air: "; wakeMessage += String(readings.tempAir, 1); wakeMessage += " °C\n";
  wakeMessage += "- Aqua lvl: "; wakeMessage += String(readings.wlAqua); wakeMessage += " cm\n";
  wakeMessage += "- Réserve lvl: "; wakeMessage += String(readings.wlTank); wakeMessage += " cm\n";
  wakeMessage += "- Pompe aquarium: "; wakeMessage += (acts.isAquaPumpRunning() ? "ON" : "OFF"); wakeMessage += "\n";
  wakeMessage += "- Pompe réservoir: "; wakeMessage += (acts.isTankPumpRunning() ? "ON" : "OFF"); wakeMessage += "\n";
  wakeMessage += "- Chauffage: "; wakeMessage += (acts.isHeaterOn() ? "ON" : "OFF"); wakeMessage += "\n";
  wakeMessage += "- Lumière: "; wakeMessage += (acts.isLightOn() ? "ON" : "OFF"); wakeMessage += "\n";
  
  // Ajouter les informations de délai d'activité
  wakeMessage += "\n-- Configuration Sleep --\n";
  wakeMessage += "- Délai d'activité: Configuration adaptative\n";
  wakeMessage += "- Mode adaptatif: ACTIF\n";
  
  // Ajouter les informations de connexion WiFi
  wakeMessage += "\n-- CONNEXION RÉSEAU --\n";
  if (WiFi.status() == WL_CONNECTED) {
    wakeMessage += "- WiFi: Connecté\n";
    wakeMessage += "- SSID: "; wakeMessage += WiFi.SSID(); wakeMessage += "\n";
    wakeMessage += "- IP: "; wakeMessage += WiFi.localIP().toString(); wakeMessage += "\n";
    wakeMessage += "- RSSI: "; wakeMessage += String(WiFi.RSSI()); wakeMessage += " dBm\n";
  } else {
    wakeMessage += "- WiFi: Déconnecté\n";
  }
  
  Serial.println(F("[Mail] ===== ENVOI MAIL RÉVEIL ====="));
  Serial.printf("[Mail] Raison: %s\n", reason);
  Serial.printf("[Mail] Durée veille: %u s\n", actualSleepSeconds);
  Serial.println(F("[Mail] =============================="));
  
  return sendSync(wakeSubject.c_str(), wakeMessage.c_str(), "User", EmailConfig::DEFAULT_RECIPIENT);
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
    success = sendAlertSync(item.subject, String(item.message), item.toEmail);
  } else {
    success = sendSync(item.subject, item.message, "User", item.toEmail);
  }
  
  if (success) {
    _mailsSent++;
    Serial.printf("[Mail] ✅ Mail envoyé avec succès (%u total)\n", _mailsSent);
  } else {
    _mailsFailed++;
    Serial.printf("[Mail] ❌ Échec envoi mail (%u échecs)\n", _mailsFailed);
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
  
  // Ajoute à la queue (non-bloquant, timeout 100ms)
  if (xQueueSend(_mailQueue, &item, pdMS_TO_TICKS(100)) != pdTRUE) {
    Serial.println(F("[Mail] ⚠️ Queue pleine, mail ignoré"));
    return false;
  }
  
  Serial.printf("[Mail] 📥 Mail ajouté à la queue (%u en attente): '%s'\n", 
                getQueuedMails(), subject);
  return true;
}

// Méthode sendAlert() asynchrone - ajoute à la queue et retourne immédiatement
bool Mailer::sendAlert(const char* subject, const String& message, const char* toEmail) {
  Serial.println(F("[Mail] ===== SENDALERT ASYNC (v11.142) ====="));
  
  if (!_mailQueue) {
    Serial.println(F("[Mail] ⚠️ Queue non initialisée, envoi synchrone..."));
    return sendAlertSync(subject, message, toEmail);
  }
  
  // Vérifications préalables
  if (!subject) {
    Serial.println(F("[Mail] ❌ ERREUR: subject est NULL"));
    return false;
  }
  if (!toEmail || strlen(toEmail) == 0) {
    Serial.println(F("[Mail] ❌ ERREUR: toEmail est vide/NULL"));
    return false;
  }
  if (message.length() == 0) {
    Serial.println(F("[Mail] ❌ ERREUR: message vide"));
    return false;
  }
  
  MailQueueItem item;
  memset(&item, 0, sizeof(item));
  
  // Copie sécurisée des données
  strncpy(item.subject, subject, sizeof(item.subject) - 1);
  
  // Tronquer le message si trop long
  size_t msgLen = message.length();
  if (msgLen >= sizeof(item.message)) {
    Serial.printf("[Mail] ⚠️ Message tronqué de %u à %u caractères\n", 
                  msgLen, sizeof(item.message) - 1);
    msgLen = sizeof(item.message) - 1;
  }
  strncpy(item.message, message.c_str(), msgLen);
  item.message[msgLen] = '\0';
  
  strncpy(item.toEmail, toEmail, sizeof(item.toEmail) - 1);
  item.isAlert = true;
  
  // Ajoute à la queue (non-bloquant, timeout 100ms)
  if (xQueueSend(_mailQueue, &item, pdMS_TO_TICKS(100)) != pdTRUE) {
    Serial.println(F("[Mail] ⚠️ Queue pleine, alerte ignorée"));
    return false;
  }
  
  Serial.printf("[Mail] 📥 Alerte ajoutée à la queue (%u en attente): '%s'\n", 
                getQueuedMails(), subject);
  Serial.println(F("[Mail] ✅ Retour immédiat (non-bloquant)"));
  return true;
}
#endif