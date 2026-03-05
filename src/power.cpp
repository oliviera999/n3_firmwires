#include "power.h"
#include "nvs_manager.h"  // v11.107
#include "nvs_keys.h"     // NVSKeys::System::RTC_EPOCH
#include "tls_mutex.h"   // v11.149: Flag g_enteringLightSleep
#include <WiFi.h>
#include "esp_wifi.h"     // Pour esp_wifi_get_config (éviter String Arduino)
#include "wifi_manager.h"  // Pour WiFiHelpers
#include <time.h>
#include <sys/time.h>
#include <cmath>
#include <cstring>
#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "config.h"
#include "log.h"
#include "realtime_websocket.h"
#if defined(USE_RTC_DS3231)
#include "rtc_ds3231.h"
#endif

// #region agent log
#if defined(USE_RTC_DS3231)
#define _PWR_DS3231_LOG(loc, msg, dataInner, hypId) \
  Serial.printf("{\"sessionId\":\"faa4e5\",\"location\":\"%s\",\"message\":\"%s\",\"data\":{%s},\"timestamp\":%lu,\"hypothesisId\":\"%s\"}\n", (loc), (msg), (dataInner), (unsigned long)millis(), (hypId))
#endif
// #endregion

// Fonction tcpip_safe_call supprimée - appels directs utilisés à la place

PowerManager::PowerManager()
    : _gmtOffsetSec(SystemConfig::NTP_GMT_OFFSET_SEC), _daylightOffsetSec(SystemConfig::NTP_DAYLIGHT_OFFSET_SEC), _ntpServer(SystemConfig::NTP_SERVER),
      _lastNtpSync(0), _hasSavedCredentials(false),
      _lastTimeSave(0), _lastSavedEpoch(0), _lastDriftCorrection(0), _currentDriftPPM(0.0f),
      _lastDriftSeconds(0.0f), _driftAccumulator(0.0f), _sleepRemainderUs(0) {
  _lastSSID[0] = '\0';
  _lastPassword[0] = '\0';
}

void PowerManager::initWatchdog() {
  static bool initialized = false;
  if (initialized) {
    esp_task_wdt_reset();
    return;
  }
  initialized = true;
  esp_task_wdt_add(NULL);
  esp_task_wdt_reset();
  Serial.println(F("[Power] Watchdog prêt (géré par TWDT natif)"));
}

void PowerManager::resetWatchdog() {
  esp_task_wdt_reset();
}

uint32_t PowerManager::goToLightSleep(uint32_t sleepTimeSeconds) {
  Serial.printf("[Power] Mise en veille légère pour %u secondes\n", sleepTimeSeconds);
  
  // === v11.149: Signaler la transition vers light sleep ===
  // Bloque les nouvelles connexions TLS (SMTP, HTTPS) pour éviter les crashs
  g_enteringLightSleep = true;
  Serial.println(F("[Power] 🚦 Flag g_enteringLightSleep activé - nouvelles connexions TLS bloquées"));
  
  // Attendre brièvement que les connexions TLS en cours se terminent
  // Le mutex TLS sera relâché par les tâches en cours
  vTaskDelay(pdMS_TO_TICKS(500));
  
  // Timestamp avant le passage en veille
  const uint64_t startUs = esp_timer_get_time();

  // Sauvegarde de l'heure avant le sommeil (si configuré)
  if (SleepConfig::SAVE_TIME_BEFORE_SLEEP) {
    saveTimeToFlash();
  }
  
  // Sauvegarde des identifiants WiFi avant la déconnexion (si configuré)
  // Utiliser la configuration existante ou une valeur par défaut
  #ifdef SAVE_WIFI_CREDENTIALS_BEFORE_SLEEP
  if (SAVE_WIFI_CREDENTIALS_BEFORE_SLEEP) {
    saveCurrentWifiCredentials();
  }
  #else
  saveCurrentWifiCredentials(); // Par défaut, sauvegarder
  #endif
  
  // Configuration du réveil
  esp_sleep_enable_timer_wakeup(sleepTimeSeconds * 1000000ULL); // Conversion en microsecondes
  
  // Déconnexion WiFi avant le sommeil (si configuré)
  // Utiliser la configuration existante ou une valeur par défaut
  bool disconnectWifi = true;
  #ifdef DISCONNECT_WIFI_BEFORE_SLEEP
  disconnectWifi = DISCONNECT_WIFI_BEFORE_SLEEP;
  #endif

  if (disconnectWifi) {
    // AMÉLIORATION: Fermer proprement le WebSocket avant de déconnecter le WiFi
    // pour éviter les erreurs 1006 (connexion fermée sans Close frame)
    uint8_t wsClients = g_realtimeWebSocket.getConnectedClients();
    
    if (wsClients > 0) {
      Serial.printf("[Power] 🔌 Fermeture propre de %u connexion(s) WebSocket avant sleep...\n", wsClients);
      
      // Notifier les clients que le serveur entre en veille avec message explicite
      // Le client recevra un message "server_closing" qui bloquera temporairement les reconnexions
      g_realtimeWebSocket.notifyWifiChange("System entering light sleep mode");
      
      // Fermer toutes les connexions proprement avec Close frame
      g_realtimeWebSocket.closeAllConnections();
      
      // Délai critique pour assurer l'envoi des Close frames avant déconnexion WiFi
      // Réduit le risque de code 1006 (fermeture anormale)
      vTaskDelay(pdMS_TO_TICKS(350));
    }
    
    // Déconnecter le WiFi
    WiFi.disconnect();
    Serial.println(F("[Power] WiFi déconnecté avant sommeil"));
  }
  
  // DIAGNOSTIC FINAL: Log mémoire juste avant sleep
  uint32_t finalHeap = ESP.getFreeHeap();
  uint32_t minHeap = ESP.getMinFreeHeap();
  Serial.printf("[Power] 📊 Mémoire avant sleep: %u bytes (min historique: %u bytes)\n", 
                finalHeap, minHeap);
  
  if (finalHeap < 30000) {
    Serial.printf("[Power] 🚨 ATTENTION CRITIQUE: Heap très bas (%u bytes) avant sleep\n", finalHeap);
  }
  
  // Mise en veille
  esp_light_sleep_start();
  
  // Calcul effectif du temps passé en veille légère
  const uint32_t sleptSec = getSleptTime(startUs);

  // === v11.149: Désactiver le flag de transition sleep ===
  // Autorise à nouveau les connexions TLS
  g_enteringLightSleep = false;
  Serial.println(F("[Power] 🚦 Flag g_enteringLightSleep désactivé - connexions TLS autorisées"));

  // Réveil - reconnexion WiFi avec les identifiants sauvegardés (si configuré)
  if (SleepConfig::AUTO_RECONNECT_WIFI_AFTER_SLEEP) {
    Serial.println(F("[Power] Réveil - reconnexion WiFi"));
    if (!reconnectWithSavedCredentials()) {
      Serial.println(F("[Power] Échec reconnexion avec identifiants sauvegardés - laissez wifi.loop() prendre le relais"));
    } else {
      // Événement clé: après reconnexion WiFi, persiste l'heure courante
      if (SleepConfig::SAVE_TIME_BEFORE_SLEEP) {
        saveTimeToFlash();
      }
    }
  }
  
  // Retourner la durée réelle de veille
  return sleptSec;
}

// Suppression des fonctions deep sleep non utilisées

void PowerManager::setNTPConfig(int gmtOffset, int daylightOffset, const char* ntpServer) {
  _gmtOffsetSec = gmtOffset;
  _daylightOffsetSec = daylightOffset;
  _ntpServer = ntpServer;
  Serial.printf("[Power] Configuration NTP: GMT+%d, DST+%d, serveur: %s\n", 
                gmtOffset/3600, daylightOffset/3600, ntpServer);
}

void PowerManager::initTime() {
  LOG_TIME(LogConfig::LOG_INFO, "Initialisation de la gestion du temps");
  
  // Initialiser les variables de dérive
  _lastDriftCorrection = 0;
  _currentDriftPPM = 0.0f;
  _lastDriftSeconds = 0.0f;
  _driftAccumulator = 0.0f;
  
  // Configuration timezone UTC+1 (Maroc/France hiver)
  setenv("TZ", "<+01>-1", 1);
  tzset();
  
  // Chargement de l'heure sauvegardée avec fallback robuste
  time_t loadedEpoch = loadTimeWithFallback();
  
  // Configuration NTP avec offset configuré (ex. UTC+1 Maroc)
  configTime(_gmtOffsetSec, _daylightOffsetSec, _ntpServer);
  
  char timeBuf[64];
  getCurrentTimeString(timeBuf, sizeof(timeBuf));
  LOG_TIME(LogConfig::LOG_INFO, "Heure actuelle: %s (epoch: %lu)", 
           timeBuf, loadedEpoch);
  LOG_TIME(LogConfig::LOG_INFO, "Timezone configurée: GMT+%d heures (offset: %d secondes)", 
           _gmtOffsetSec/3600, _gmtOffsetSec);
  
  // Informations sur la configuration NTP
  LOG_NTP(LogConfig::LOG_INFO, "Configuration NTP - Serveur: %s", _ntpServer);
}

void PowerManager::syncTimeFromNTP() {
  if (WiFi.status() != WL_CONNECTED) {
    LOG_NTP(LogConfig::LOG_WARN, "Pas de WiFi - synchronisation NTP impossible");
    return;
  }
  
  LOG_NTP(LogConfig::LOG_INFO, "Début de synchronisation NTP avec serveur: %s", _ntpServer);
  
  // Sauvegarder l'heure locale avant sync pour calcul de dérive
  time_t localBeforeEpoch = time(nullptr);
  unsigned long localBeforeMillis = millis();
  
  // Configuration NTP avec offset configuré
  configTime(_gmtOffsetSec, _daylightOffsetSec, _ntpServer);

  // Tentative rapide de synchronisation (approche hybride)
  struct tm timeinfo;
  bool syncSuccess = false;
  unsigned long startTime = millis();
  int attempts = 0;
  
  // Tentative immédiate
  if (getLocalTime(&timeinfo)) {
    syncSuccess = true;
  } else {
    // Retry rapide après 500ms
    vTaskDelay(pdMS_TO_TICKS(500));
    if (getLocalTime(&timeinfo)) {
      syncSuccess = true;
      attempts = 1;
    } else {
      // Fallback: boucle avec timeout de 10 secondes max (feed WDT pour tâche surveillée)
      while ((millis() - startTime) < 10000) {
        if (esp_task_wdt_status(NULL) == ESP_OK) {
          esp_task_wdt_reset();
        }
        if (getLocalTime(&timeinfo)) {
          syncSuccess = true;
          break;
        }
        attempts++;
        vTaskDelay(pdMS_TO_TICKS(100));
      }
    }
  }
  
  unsigned long syncDuration = millis() - startTime;
  
  if (syncSuccess) {
    // Vérification que l'année est raisonnable (après 2024)
    if (timeinfo.tm_year + 1900 < 2024) {
      LOG_NTP(LogConfig::LOG_WARN, "Année invalide %d après sync", timeinfo.tm_year + 1900);
      syncSuccess = false;
    }
  }
  
  if (syncSuccess) {
    // Sauvegarde de l'heure synchronisée
    smartSaveTime();

#if defined(USE_RTC_DS3231)
    if (RtcDS3231::isPresent()) {
      time_t ntpEpochForRtc = mktime(&timeinfo);
      if (RtcDS3231::write(ntpEpochForRtc)) {
        // #region agent log
        Serial.printf("{\"sessionId\":\"faa4e5\",\"location\":\"power.cpp:syncTimeFromNTP\",\"message\":\"rtc_ntp_write_ok\",\"data\":{\"epoch\":%lu},\"timestamp\":%lu,\"hypothesisId\":\"H3\"}\n",
                      (unsigned long)ntpEpochForRtc, (unsigned long)millis());
        // #endregion
        LOG_RTC(LogConfig::LOG_INFO, "DS3231 mis à jour après sync NTP");
      } else {
        // #region agent log
        Serial.printf("{\"sessionId\":\"faa4e5\",\"location\":\"power.cpp:syncTimeFromNTP\",\"message\":\"rtc_ntp_write_fail\",\"data\":{\"epoch\":%lu},\"timestamp\":%lu,\"hypothesisId\":\"H3\"}\n",
                      (unsigned long)ntpEpochForRtc, (unsigned long)millis());
        // #endregion
        LOG_RTC(LogConfig::LOG_WARN, "Échec écriture DS3231 après NTP");
      }
    }
#endif

    unsigned long syncMillis = millis();
    time_t ntpEpoch = mktime(&timeinfo);
    
    // Calcul de la dérive si on avait une heure locale valide (seuil = EPOCH_MIN_VALID)
    if (localBeforeEpoch > SleepConfig::EPOCH_MIN_VALID && syncMillis > localBeforeMillis) {
      time_t timeDiff = ntpEpoch - localBeforeEpoch;
      unsigned long millisDiff = syncMillis - localBeforeMillis;
      
      // Calcul de la dérive en PPM (parties par million)
      if (millisDiff > 0) {
        float expectedSeconds = millisDiff / 1000.0f;
        float actualSeconds = static_cast<float>(timeDiff);
        float driftSeconds = actualSeconds - expectedSeconds;
        
        // Éviter division par zéro et calculer PPM
        if (expectedSeconds > 0.0f) {
          _currentDriftPPM = (driftSeconds / expectedSeconds) * 1000000.0f;
          _currentDriftPPM = std::fmax(-SleepConfig::DRIFT_PPM_MAX, std::fmin(SleepConfig::DRIFT_PPM_MAX, _currentDriftPPM));
          _lastDriftSeconds = driftSeconds;
          
          LOG_DRIFT(LogConfig::LOG_INFO, "Dérive mesurée: %.2f PPM (%.2f s sur %.2f s)", 
                    _currentDriftPPM, driftSeconds, expectedSeconds);
        } else {
          _currentDriftPPM = 0.0f;
          _lastDriftSeconds = 0.0f;
        }
      } else {
        _currentDriftPPM = 0.0f;
        _lastDriftSeconds = 0.0f;
      }
    } else {
      // Réinitialiser la dérive si pas de mesure valide
      _currentDriftPPM = 0.0f;
      _lastDriftSeconds = 0.0f;
    }
    
    // Réinitialiser l'accumulateur de dérive après sync réussie
    _driftAccumulator = 0.0f;
    _lastDriftCorrection = syncMillis;

    LOG_NTP(LogConfig::LOG_INFO, "Synchronisation NTP réussie en %lu ms (%d tentatives)", 
            syncDuration, attempts);
    char timeBuf[64];
    getCurrentTimeString(timeBuf, sizeof(timeBuf));
    LOG_NTP(LogConfig::LOG_INFO, "Heure NTP: %s (epoch: %lu)", 
            timeBuf, ntpEpoch);

    _lastNtpSync = syncMillis;
  } else {
    LOG_NTP(LogConfig::LOG_ERROR, "Échec de synchronisation NTP après %lu ms (%d tentatives)", 
            syncDuration, attempts);
    // Restaurer l'heure valide précédente pour éviter régression (configTime peut corrompre le RTC)
    if (isValidEpoch(localBeforeEpoch)) {
      timeval tv = {localBeforeEpoch, 0};
      settimeofday(&tv, nullptr);
      LOG_NTP(LogConfig::LOG_WARN, "Heure restaurée (epoch: %lu)", (unsigned long)localBeforeEpoch);
    }
  }
}

// Wrappers pour compatibilité - délèguent vers les méthodes intelligentes
void PowerManager::saveTimeToFlash() {
  smartSaveTime();
}

void PowerManager::forceSaveTimeToFlash() {
  // Forcer la sauvegarde en réinitialisant les variables de suivi
  _lastTimeSave = 0;
  _lastSavedEpoch = 0;
  smartSaveTime();
}

void PowerManager::loadTimeFromFlash() {
  loadTimeWithFallback();
}

void PowerManager::applyExternalRTCIfPresent() {
#if !defined(USE_RTC_DS3231)
  return;
#endif
#if defined(USE_RTC_DS3231)
  if (!RtcDS3231::isPresent()) {
    // #region agent log
    _PWR_DS3231_LOG("power.cpp:applyExternalRTC", "rtc_boot_absent", "\"branch\":\"absent\"", "H1");
    // #endregion
    LOG_RTC(LogConfig::LOG_INFO, "DS3231 absent (optionnel)");
    return;
  }
  time_t rtcEpoch = 0;
  if (!RtcDS3231::read(&rtcEpoch)) {
    // #region agent log
    _PWR_DS3231_LOG("power.cpp:applyExternalRTC", "rtc_boot_read_fail", "\"branch\":\"read_invalid\"", "H2");
    // #endregion
    LOG_RTC(LogConfig::LOG_WARN, "DS3231 présent mais lecture heure invalide");
    return;
  }
  if (!isValidEpoch(rtcEpoch)) {
    // #region agent log
    Serial.printf("{\"sessionId\":\"faa4e5\",\"location\":\"power.cpp:applyExternalRTC\",\"message\":\"rtc_boot_epoch_invalid\",\"data\":{\"branch\":\"epoch_invalid\",\"epoch\":%lu},\"timestamp\":%lu,\"hypothesisId\":\"H4\"}\n",
                  (unsigned long)rtcEpoch, (unsigned long)millis());
    // #endregion
    LOG_RTC(LogConfig::LOG_WARN, "DS3231 epoch hors plage valide: %lu", (unsigned long)rtcEpoch);
    return;
  }
  timeval tv = {rtcEpoch, 0};
  settimeofday(&tv, nullptr);
  smartSaveTime();
  char timeBuf[64];
  getCurrentTimeString(timeBuf, sizeof(timeBuf));
  // #region agent log
  Serial.printf("{\"sessionId\":\"faa4e5\",\"location\":\"power.cpp:applyExternalRTC\",\"message\":\"rtc_boot_applied\",\"data\":{\"branch\":\"applied\",\"epoch\":%lu},\"timestamp\":%lu,\"hypothesisId\":\"H4\"}\n",
                (unsigned long)rtcEpoch, (unsigned long)millis());
  // #endregion
  LOG_RTC(LogConfig::LOG_INFO, "Heure appliquée depuis DS3231: %s", timeBuf);
#endif
}

void PowerManager::updateTime() {
  // Protection contre débordement de millis() : utiliser soustraction
  unsigned long now = millis();
  
  // Synchronisation périodique NTP si WiFi disponible
  // Protection débordement: (now - _lastNtpSync) < interval au lieu de (now > _lastNtpSync + interval)
  if (WiFi.status() == WL_CONNECTED) {
    if (_lastNtpSync == 0 || (now - _lastNtpSync) >= TimingConfig::NTP_SYNC_INTERVAL_MS) {
      syncTimeFromNTP();
    }
  }
  
  // Application de la correction de dérive
  applyDriftCorrection();
  
  // Sauvegarde intelligente périodique
  smartSaveTime();
} 

// ---------------------------------------------------------------------------
// Epoch validé pour affichage (évite régressions / valeurs aberrantes)
// ---------------------------------------------------------------------------
time_t PowerManager::getCurrentEpochSafe() {
  time_t t = time(nullptr);
  if (isValidEpoch(t)) {
    return t;
  }
  if (_lastSavedEpoch > 0 && isValidEpoch(_lastSavedEpoch)) {
    return _lastSavedEpoch;
  }
  return SleepConfig::EPOCH_DEFAULT_FALLBACK;
}

// ---------------------------------------------------------------------------
// Formatage de l'heure en chaîne locale (HH:MM:SS JJ/MM/AAAA)
// ---------------------------------------------------------------------------
void PowerManager::getCurrentTimeString(char* buffer, size_t bufferSize) {
  if (buffer == nullptr || bufferSize == 0) {
    return;
  }

  time_t epoch = getCurrentEpochSafe();
  struct tm timeinfo;
  
  if (!localtime_r(&epoch, &timeinfo)) {
    // Fallback en cas d'erreur
    strncpy(buffer, "00:00:00 01/01/1970", bufferSize - 1);
    buffer[bufferSize - 1] = '\0';
    return;
  }

  strftime(buffer, bufferSize, "%H:%M:%S %d/%m/%Y", &timeinfo);
}

// ---------------------------------------------------------------------------
// Format ISO pour emails (YYYY-MM-DD HH:MM), aligné avec mailer buildLightFooter
// ---------------------------------------------------------------------------
void PowerManager::getCurrentTimeStringForMail(char* buffer, size_t bufferSize) {
  if (buffer == nullptr || bufferSize == 0) {
    return;
  }

  time_t epoch = getCurrentEpochSafe();
  struct tm timeinfo;

  if (!localtime_r(&epoch, &timeinfo)) {
    strncpy(buffer, "1970-01-01 00:00", bufferSize - 1);
    buffer[bufferSize - 1] = '\0';
    return;
  }

  strftime(buffer, bufferSize, "%Y-%m-%d %H:%M", &timeinfo);
}

void PowerManager::saveCurrentWifiCredentials() {
  if (WiFi.status() == WL_CONNECTED) {
    bool firstSave = !_hasSavedCredentials;
    WiFiHelpers::getSSID(_lastSSID, sizeof(_lastSSID));
    // Lecture PSK via ESP-IDF (évite String Arduino → stabilité long uptime)
    wifi_config_t conf;
    if (esp_wifi_get_config(WIFI_IF_STA, &conf) == ESP_OK) {
      size_t plen = strnlen(reinterpret_cast<const char*>(conf.sta.password), 64);
      if (plen >= sizeof(_lastPassword)) plen = sizeof(_lastPassword) - 1;
      memcpy(_lastPassword, conf.sta.password, plen);
      _lastPassword[plen] = '\0';
    } else {
      _lastPassword[0] = '\0';
    }
    _hasSavedCredentials = true;
    if (firstSave) {
      Serial.printf("[Power] Identifiants WiFi sauvegardés: SSID=%s\n", _lastSSID);
    }
  } else {
    _lastSSID[0] = '\0';
    _lastPassword[0] = '\0';
    _hasSavedCredentials = false;
    Serial.println(F("[Power] Pas de connexion WiFi active - pas d'identifiants à sauvegarder"));
  }
}

bool PowerManager::reconnectWithSavedCredentials() {
  if (!_hasSavedCredentials || strlen(_lastSSID) == 0) {
    Serial.println(F("[Power] Aucun identifiant WiFi sauvegardé"));
    return false;
  }
  
  Serial.printf("[Power] Tentative de reconnexion WiFi avec SSID: %s\n", _lastSSID);
  
  // Mode station
  WiFi.mode(WIFI_STA);
  
  // Démarrage de la connexion WiFi avec les identifiants sauvegardés
  if (strlen(_lastPassword) == 0) {
    Serial.println(F("[Power] Connexion sans mot de passe"));
    WiFi.begin(_lastSSID);
  } else {
    Serial.println(F("[Power] Connexion avec mot de passe"));
    WiFi.begin(_lastSSID, _lastPassword);
  }
  
  // Attente de la connexion avec timeout (réseaux lents/faibles: 8s dérogation après réveil)
  uint32_t startTime = millis();
  const uint32_t timeoutMs = TimingConfig::WIFI_RECONNECT_AFTER_WAKE_MS;
  
  Serial.print(F("[Power] Attente de connexion"));
  while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < timeoutMs) {
    vTaskDelay(pdMS_TO_TICKS(100)); // Optimized delay - reduced from 250ms to 100ms for faster reconnection
    if ((millis() - startTime) % 1000 == 0) {
      Serial.print(".");
    }
  }
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    IPAddress ip = WiFi.localIP();
    char ipBuf[16];
    snprintf(ipBuf, sizeof(ipBuf), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    Serial.printf("[Power] Reconnexion WiFi réussie à %s (%s)\n", 
                  _lastSSID, ipBuf);
    WIFI_APPLY_MODEM_SLEEP(true);  // modem-sleep pour économie d'énergie
    
    // Attente stabilisation stack TCP/IP (évite "connection refused" après réveil)
    waitForNetworkReady();
    
    return true;
  } else {
    Serial.printf("[Power] Échec de reconnexion WiFi à %s (timeout après %u ms)\n", 
                  _lastSSID, timeoutMs);
    return false;
  }
}

// ========================================
// ATTENTE STABILISATION RÉSEAU
// ========================================
void PowerManager::waitForNetworkReady() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[Power] waitForNetworkReady: WiFi non connecté, abandon"));
    return;
  }
  
  // Dérogation réseaux lents: 5s max pour DNS/stabilisation (au lieu de 3s)
  const uint32_t STABILIZATION_DELAY_MS = 500;   // 0.5 seconde de stabilisation
  const uint32_t MAX_WAIT_MS = 5000;             // 5 s max (réseaux lents / DNS)
  uint32_t startMs = millis();
  
  Serial.println(F("[Power] Attente stabilisation réseau..."));
  
  // Phase 1: Délai minimum de stabilisation TCP/IP
  vTaskDelay(pdMS_TO_TICKS(STABILIZATION_DELAY_MS));
  
  // Phase 2: Vérifier que l'IP est toujours valide
  while ((millis() - startMs) < MAX_WAIT_MS) {
    if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
      // Test DNS rapide pour vérifier que le réseau est vraiment opérationnel
      IPAddress dnsResult;
      if (WiFi.hostByName("pool.ntp.org", dnsResult)) {
        IPAddress ip = WiFi.localIP();
        char ipBuf[16];
        snprintf(ipBuf, sizeof(ipBuf), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        Serial.printf("[Power] ✅ Réseau prêt (%s, DNS OK, %lu ms)\n", 
                      ipBuf, millis() - startMs);
        return;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
  
  // Timeout atteint mais WiFi connecté - on continue quand même
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[Power] ⚠️ Réseau partiellement prêt après %lu ms (DNS timeout)\n", 
                  millis() - startMs);
  } else {
    Serial.println(F("[Power] ❌ Réseau perdu pendant stabilisation"));
  }
}

// ========================================
// NOUVELLES MÉTHODES DE GESTION TEMPORELLE ROBUSTE
// ========================================

  bool PowerManager::isValidEpoch(time_t epoch) const {
  // Comparaison en unsigned pour éviter overflow 32-bit (EPOCH_MAX_VALID > INT32_MAX)
  unsigned long uepoch = (unsigned long)epoch;
  unsigned long umin = (unsigned long)SleepConfig::EPOCH_MIN_VALID;
  unsigned long umax = (unsigned long)SleepConfig::EPOCH_MAX_VALID;
  return epoch != 0 && uepoch >= umin && uepoch <= umax;
}

time_t PowerManager::loadTimeWithFallback() {
  // Liste des fallbacks dans l'ordre de priorité
  struct Fallback {
    time_t epoch;
    const char* name;
  };
  
  // Fallback 1: Epoch sauvegardé en NVS
  unsigned long savedEpochUL;
  g_nvsManager.loadULong(NVS_NAMESPACES::SYSTEM, NVSKeys::System::RTC_EPOCH, savedEpochUL, 0);
  time_t savedEpoch = static_cast<time_t>(savedEpochUL);
  
  Fallback fallbacks[] = {
    {savedEpoch, "sauvegardé"},
    {SleepConfig::EPOCH_COMPILE_TIME, "compilation"},
    {SleepConfig::EPOCH_DEFAULT_FALLBACK, "défaut"}
  };
  
  // Essayer chaque fallback dans l'ordre
  for (size_t i = 0; i < sizeof(fallbacks) / sizeof(fallbacks[0]); i++) {
    const Fallback& fb = fallbacks[i];
    if (isValidEpoch(fb.epoch)) {
      Serial.printf("[Power] Utilisation epoch %s: %lu\n", fb.name, fb.epoch);
      timeval tv = {fb.epoch, 0};
      settimeofday(&tv, nullptr);
      return fb.epoch;
    }
  }
  
  // Ne devrait jamais arriver (EPOCH_DEFAULT_FALLBACK devrait être valide)
  Serial.println(F("[Power] ERREUR: Aucun epoch valide trouvé"));
  return 0;
}

void PowerManager::applyDriftCorrection() {
  // Interrupteur global: désactive toute correction
  if (!SleepConfig::ENABLE_DRIFT_CORRECTION) {
    return;
  }
  
  // Protection contre débordement de millis()
  unsigned long now = millis();
  
  // Vérifier l'intervalle de correction (protection débordement)
  if (_lastDriftCorrection != 0) {
    if ((now - _lastDriftCorrection) < SleepConfig::DRIFT_CORRECTION_INTERVAL_MS) {
      return;
    }
  }

  // Calculer le temps écoulé depuis la dernière correction
  const unsigned long elapsedMs = (_lastDriftCorrection == 0) ? 0 : (now - _lastDriftCorrection);
  const float elapsedSeconds = elapsedMs / 1000.0f;

  if (elapsedSeconds <= 0.0f) {
    _lastDriftCorrection = now;
    return;
  }

  // Correction uniquement si on a une sync NTP réussie et une dérive mesurée significative
  if (_lastNtpSync == 0 || std::fabs(_currentDriftPPM) < SleepConfig::DRIFT_CORRECTION_THRESHOLD_PPM) {
    _lastDriftCorrection = now;
    return;
  }

  // Accumuler la dérive avec facteur d'amortissement
  _driftAccumulator += (_currentDriftPPM * elapsedSeconds * SleepConfig::DRIFT_CORRECTION_FACTOR) / 1000000.0f;
  float wholeSeconds = std::trunc(_driftAccumulator);
  
  if (wholeSeconds != 0.0f) {
    const int correctionSeconds = static_cast<int>(wholeSeconds);
    time_t currentEpoch = time(nullptr);
    time_t correctedEpoch = currentEpoch + correctionSeconds;

    if (isValidEpoch(correctedEpoch)) {
      timeval tv = {correctedEpoch, 0};
      settimeofday(&tv, nullptr);

      Serial.printf("[Power] Correction de dérive appliquée: %d s (dérive: %.2f PPM)\n",
                    correctionSeconds, _currentDriftPPM);
      smartSaveTime();
    }

    _driftAccumulator -= wholeSeconds;
  }

  _lastDriftCorrection = now;
}




void PowerManager::smartSaveTime() {
  time_t currentEpoch = time(nullptr);
  unsigned long currentMillis = millis();
  
  // Validation stricte de l'epoch
  if (!isValidEpoch(currentEpoch)) {
    Serial.printf("[Power] Epoch invalide: %lu (MIN=%lu MAX=%lu)\n",
                  (unsigned long)currentEpoch,
                  (unsigned long)SleepConfig::EPOCH_MIN_VALID,
                  (unsigned long)SleepConfig::EPOCH_MAX_VALID);
    return;
  }
  
  // Ne jamais persister une régression significative (évite corruption NVS)
  if (_lastSavedEpoch > 0 && (time_t)(_lastSavedEpoch - currentEpoch) > (time_t)SleepConfig::MAX_RTC_REGRESSION_SEC) {
    Serial.printf("[Power] Régression d'heure ignorée (actuel: %lu, sauvegardé: %lu)\n",
                  (unsigned long)currentEpoch, (unsigned long)_lastSavedEpoch);
    return;
  }
  
  // Logique de sauvegarde intelligente avec protection débordement millis()
  bool shouldSave = false;
  
  // 1. Première sauvegarde
  if (_lastTimeSave == 0) {
    shouldSave = true;
    Serial.println(F("[Power] Première sauvegarde d'heure"));
  }
  // 2. Sauvegarde forcée périodique (protection débordement: utiliser soustraction)
  else if ((currentMillis - _lastTimeSave) >= SleepConfig::MAX_SAVE_INTERVAL_MS) {
    shouldSave = true;
    Serial.println(F("[Power] Sauvegarde forcée périodique"));
  }
  // 3. Sauvegarde si différence significative (protection débordement)
  else if ((currentMillis - _lastTimeSave) >= SleepConfig::MIN_SAVE_INTERVAL_MS) {
    // Calcul sécurisé de la différence d'epoch (time_t peut être signé)
    time_t epochDiff = (currentEpoch > _lastSavedEpoch) ? 
                       (currentEpoch - _lastSavedEpoch) : 
                       (_lastSavedEpoch - currentEpoch);
    
    if (epochDiff > SleepConfig::MIN_TIME_DIFF_FOR_SAVE_SEC) {
      shouldSave = true;
      Serial.printf("[Power] Sauvegarde (diff: %ld s)\n", epochDiff);
    }
  }
  
  if (!shouldSave) {
    return;
  }
  
  // Sauvegarde en NVS
  g_nvsManager.saveULong(NVS_NAMESPACES::SYSTEM, NVSKeys::System::RTC_EPOCH, currentEpoch);

  _lastTimeSave = currentMillis;
  _lastSavedEpoch = currentEpoch;
  char timeBuf[64];
  getCurrentTimeString(timeBuf, sizeof(timeBuf));
  Serial.printf("[Power] Heure sauvegardée: %s (epoch: %lu)\n", 
                timeBuf, currentEpoch);
}

uint32_t PowerManager::getSleptTime(uint64_t startUs) {
  // Calcul effectif du temps passé en veille
  const uint64_t sleptUs = esp_timer_get_time() - startUs;
  _sleepRemainderUs += (uint32_t)(sleptUs % 1000000ULL);
  const uint32_t carrySec = _sleepRemainderUs / 1000000UL;
  _sleepRemainderUs = _sleepRemainderUs % 1000000UL;
  const uint32_t sleptSec = (uint32_t)(sleptUs / 1000000ULL) + carrySec;

  // Ajustement de l'horloge système pour refléter le temps réellement écoulé
  if (sleptSec > 0) {
    time_t currentEpoch = time(nullptr);
    time_t newEpoch = currentEpoch + sleptSec;
    timeval tv = {newEpoch, 0};
    settimeofday(&tv, nullptr);
    
    char timeBuf[64];
    getCurrentTimeString(timeBuf, sizeof(timeBuf));
    Serial.printf("[Power] Veille: %llu us (~%u s), reste=%u us – heure: %s\n",
                  (unsigned long long)sleptUs, sleptSec, _sleepRemainderUs, 
                  timeBuf);
  }
  
  return sleptSec;
}
