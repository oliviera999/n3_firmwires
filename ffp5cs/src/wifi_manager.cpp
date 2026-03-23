#include "wifi_manager.h"
#include "display_view.h"
#include "config.h"
#include "esp_wifi.h"  // Pour esp_wifi_scan_get_ap_records (éviter String Arduino)
#include "esp_mac.h"   // Pour esp_base_mac_addr_set (override MAC avant WiFi init)
#include <WiFiGeneric.h>
#include <algorithm>
#include <cstring>
#include <esp_task_wdt.h>
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
#include "rom/ets_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

// Dernière raison de déconnexion STA (pour diagnostic quand connexion échoue)
static volatile int s_lastStaDisconnectReason = -1;
static void onStaDisconnected(arduino_event_id_t event, arduino_event_info_t info) {
  (void)event;
  s_lastStaDisconnectReason = (int)info.wifi_sta_disconnected.reason;
}

// Renvoie une chaîne explicite pour les raisons de déconnexion courantes (diagnostic WiFi)
static const char* wifiDisconnectReasonStr(int reason) {
  switch (reason) {
    case 1:  return "UNSPECIFIED";
    case 2:  return "AUTH_EXPIRE";
    case 3:  return "AUTH_LEAVE";
    case 4:  return "ASSOC_EXPIRE";
    case 5:  return "ASSOC_TOOMANY";
    case 6:  return "NOT_AUTHED";
    case 7:  return "NOT_ASSOCED";
    case 8:  return "ASSOC_LEAVE";
    case 9:  return "ASSOC_NOT_AUTHED";
    case 15: return "4WAY_HANDSHAKE_TIMEOUT";
    case 201: return "NO_AP_FOUND";
    case 202: return "AUTH_FAIL";
    case 204: return "HANDSHAKE_TIMEOUT";
    case 205: return "CONNECTION_FAIL";
    default: return nullptr;
  }
}

// DNS personnalisé : l'API Arduino-ESP32 n'expose pas setDNS() ; garder DHCP.
// Pour forcer un DNS (ex. 8.8.8.8), configurer le routeur (DHCP option 6) ou utiliser
// une IP statique + WiFi.config(ip, gw, mask, dns1, dns2). Ici on ne fait rien.
static void applyCustomDns() {
#if defined(WIFI_USE_CUSTOM_DNS) && (WIFI_USE_CUSTOM_DNS)
  (void)0;  // Réservé pour évolution (IDF API ou config statique)
#endif
}

// Scan WiFi synchrone via ESP-IDF (évite String Arduino et fragmentation heap).
// Retourne le nombre total d'APs détectés (peut être > outCount quand tronqué à outMax).
static int scanNetworksEspIdf(wifi_ap_record_t* outRecords, uint16_t outMax, uint16_t* outCount, bool showHidden) {
  if (!outRecords || outMax == 0 || !outCount) return -1;
  *outCount = 0;

  wifi_scan_config_t cfg = {};
  cfg.ssid = nullptr;
  cfg.bssid = nullptr;
  cfg.channel = 0;
  cfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;
  cfg.show_hidden = showHidden;

  esp_err_t err = esp_wifi_scan_start(&cfg, true);  // block=true
  if (err != ESP_OK) {
    return -1;
  }

  uint16_t total = 0;
  err = esp_wifi_scan_get_ap_num(&total);
  if (err != ESP_OK) {
    return -1;
  }
  if (total == 0) {
    return 0;
  }

  uint16_t toRead = (total < outMax) ? total : outMax;
  err = esp_wifi_scan_get_ap_records(&toRead, outRecords);
  if (err != ESP_OK) {
    *outCount = 0;
    return -1;
  }
  *outCount = toRead;
  return (int)total;
}

// Wrapper WiFi.begin() gérant les combinaisons password/channel/bssid
static void wifiBeginSafe(const char* ssid, const char* pass,
                          int32_t chan = 0, const uint8_t* bssid = nullptr) {
  if (pass && strlen(pass) > 0) {
    WiFi.begin(ssid, pass, chan, bssid);
  } else {
    if (chan > 0 || bssid) WiFi.begin(ssid, nullptr, chan, bssid);
    else WiFi.begin(ssid);
  }
}

// Certains routeurs 4G (ex. inwi Home 4G / Huawei) rejettent les MACs Espressif.
// Override le MAC système AVANT esp_wifi_init() via esp_base_mac_addr_set().
// Toutes les initialisations WiFi ultérieures utiliseront ce MAC.
// Doit être appelé AVANT le premier WiFi.mode().
static void overrideBaseMac() {
  static bool done = false;
  if (done) return;
  uint8_t base[6];
  uint64_t efuse = ESP.getEfuseMac();
  memcpy(base, &efuse, 6);
  uint8_t custom[6];
  custom[0] = 0x02;
  custom[1] = base[1] ^ 0x5A;
  custom[2] = base[2] ^ 0xA3;
  custom[3] = base[3] ^ 0x7E;
  custom[4] = base[4] ^ 0xAB;
  custom[5] = base[5] ^ 0xCD;
  esp_err_t err = esp_base_mac_addr_set(custom);
  if (err == ESP_OK) {
    Serial.printf("[WiFi] MAC base: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  custom[0], custom[1], custom[2], custom[3], custom[4], custom[5]);
    done = true;
  }
}

WifiManager::WifiManager(const Credential* list, size_t count, uint32_t timeoutMs, uint32_t retryIntervalMs)
    : _list(list), _count(count), _timeoutMs(timeoutMs), _retryIntervalMs(retryIntervalMs), _lastAttemptMs(0) {}

bool WifiManager::connect(DisplayView* disp, const char* hostname) {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
  ets_printf("[BOOT] connectWifi entry\n");
#endif
  if (WiFi.status() == WL_CONNECTED) return true;
  if (_count == 0) {
    Serial.println(F("[WiFi] Aucun reseau configure (WIFI_LIST vide dans secrets.h) - configurer via AP secours puis inclure SSID/mdp dans include/secrets.h"));
    Serial.println(F("[Event] WiFi no credentials; starting AP"));
    if (disp && disp->isPresent()) disp->showDiagnostic("Pas de reseau config");
    startFallbackAP();
    return false;
  }
  if (_connecting) {
    Serial.println(F("[WiFi] Connexion déjà en cours - skip"));
    return false;
  }
  _connecting = true;

  // Enregistrer une fois les handlers de diagnostic WiFi
  {
    static bool once = false;
    if (!once) {
      WiFi.onEvent(onStaDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
      once = true;
    }
  }
  s_lastStaDisconnectReason = -1;

  // Assurer une gestion manuelle de la reconnexion (évite des états internes bloquants)
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);

  auto show = [disp](const char* msg){ if(disp && disp->isPresent()){ disp->showDiagnostic(msg);} };

#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
  ets_printf("[BOOT] before show Scan\n");
#endif
  show("Scan WiFi...");
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
  ets_printf("[BOOT] after show Scan\n");
#endif
  overrideBaseMac();  // MAC système avant WiFi.mode() → appliqué au hardware
  // Init WiFi : réactivée pour S3 PSRAM (Serial après NVS). En cas de blocage esp_wifi_init, voir ESP32S3_HARDWARE_REFERENCE.md et option FFP5CS_S3_PSRAM_WIFI_DISABLED.
  if (FEATURE_WIFI_APSTA) {
    WiFi.mode(WIFI_AP_STA);
  } else {
    WiFi.mode(WIFI_STA);
  }
  if (hostname && hostname[0] != '\0') {
    WiFi.setHostname(hostname);
  }
  // TX réduit : routeurs 4G (ex. inwi) rejettent les clients avec TX > ~15 dBm
  esp_wifi_set_max_tx_power(50);  // 12.5 dBm (unité 0.25 dBm)
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
  ets_printf("[DBG H-B] after set_max_tx_power\n");
#endif
  // N'appeler disconnect() que si la STA a déjà été démarrée (begin() appelé au moins une fois).
  // Sinon sur ESP32-S3 : "STA not started! You must call begin first" et le scan peut renvoyer 0 réseau.
  if (WiFi.status() != WL_IDLE_STATUS) {
    WiFi.disconnect(false, true);
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
  ets_printf("[DBG H-C] after disconnect\n");
#endif
    vTaskDelay(pdMS_TO_TICKS(TimingConfig::WIFI_POST_DISCONNECT_DELAY_MS));
  } else {
    // Premier scan au boot : court délai pour stabilisation RF avant le scan
    vTaskDelay(pdMS_TO_TICKS(TimingConfig::WIFI_PRE_SCAN_DELAY_MS));
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
  ets_printf("[DBG H-D] after vTaskDelay(pre/disc)\n");
#endif
  }

  // S3 USB CDC: Serial.println peut bloquer si aucun client CDC sur COM9 (utiliser UART via ets_printf)
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
  ets_printf("[DBG H-E] S3 branch\n");
  ets_printf("[WiFi] scan\n");
#else
  Serial.println(F("[WiFi] 🔍 Balayage des réseaux..."));
#endif
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
  ets_printf("[BOOT] wifi scan start\n");
#endif
  // v11.176: Watchdog reset avant scan bloquant (2-5s) - audit robustesse
  if (esp_task_wdt_status(NULL) == ESP_OK) {
    esp_task_wdt_reset();
  }
  static wifi_ap_record_t s_apRecords[NetworkConfig::WIFI_SCAN_MAX_RECORDS];
  uint16_t num = 0;
  int n = scanNetworksEspIdf(s_apRecords, NetworkConfig::WIFI_SCAN_MAX_RECORDS, &num, true);
  // v11.176: Watchdog reset après scan bloquant
  if (esp_task_wdt_status(NULL) == ESP_OK) {
    esp_task_wdt_reset();
  }
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
  ets_printf("[BOOT] wifi scan done n=%d\n", n);
#endif
  if(n<=0){ show("Aucun reseau"); } else { show("Scan OK"); }
  if (n <= 0 || num == 0) {
    Serial.println(F("[WiFi] ❌ Aucun réseau détecté (ou scan erreur)"));
    Serial.println("[Event] WiFi scan: no networks");
  } else {
    Serial.printf("[WiFi] 📡 %d réseaux trouvés:\n", (int)num);
    for (int j = 0; j < num; ++j) {
      char scanSSIDBuf[33];
      memcpy(scanSSIDBuf, s_apRecords[j].ssid, 32);
      scanSSIDBuf[32] = '\0';
      size_t slen = strnlen(scanSSIDBuf, 32);
      scanSSIDBuf[slen] = '\0';
      const char* authStr = "UNKNOWN";
      switch (s_apRecords[j].authmode) {
        case WIFI_AUTH_OPEN: authStr = "OPEN"; break;
        case WIFI_AUTH_WEP: authStr = "WEP"; break;
        case WIFI_AUTH_WPA_PSK: authStr = "WPA_PSK"; break;
        case WIFI_AUTH_WPA2_PSK: authStr = "WPA2_PSK"; break;
        case WIFI_AUTH_WPA_WPA2_PSK: authStr = "WPA_WPA2_PSK"; break;
        case WIFI_AUTH_WPA2_ENTERPRISE: authStr = "WPA2_ENT"; break;
        case WIFI_AUTH_WPA3_PSK: authStr = "WPA3_PSK"; break;
        case WIFI_AUTH_WPA2_WPA3_PSK: authStr = "WPA2_WPA3_PSK"; break;
        default: break;
      }
      Serial.printf("  - [%d] '%s' RSSI=%d ch=%d auth=%s(%d)\n",
                    j, scanSSIDBuf, s_apRecords[j].rssi, s_apRecords[j].primary,
                    authStr, (int)s_apRecords[j].authmode);
    }
  }

  // Associer chaque credential à la meilleure valeur RSSI détectée + infos BSSID/chan
  struct Cand { int8_t rssi; uint8_t bssid[6]; uint8_t chan; wifi_auth_mode_t enc; bool present; };
  Cand initC{ -128,{0},0, WIFI_AUTH_OPEN, false};
  Cand cand[NVSConfig::MAX_WIFI_SAVED_NETWORKS];
  size_t credsCount = _count;
  if (credsCount > NVSConfig::MAX_WIFI_SAVED_NETWORKS) {
    Serial.printf("[WiFi] ⚠️ WIFI_COUNT=%u > max local=%u, bornage appliqué\n",
                  (unsigned)credsCount, (unsigned)NVSConfig::MAX_WIFI_SAVED_NETWORKS);
    credsCount = NVSConfig::MAX_WIFI_SAVED_NETWORKS;
  }
  for (size_t i = 0; i < credsCount; ++i) cand[i] = initC;
  for (int j = 0; j < num; ++j) {
    char scanSSIDBuf2[33];
    memcpy(scanSSIDBuf2, s_apRecords[j].ssid, 32);
    scanSSIDBuf2[32] = '\0';
    size_t slen2 = strnlen(scanSSIDBuf2, 32);
    scanSSIDBuf2[slen2] = '\0';
    const char* ss = scanSSIDBuf2;
    int8_t r = s_apRecords[j].rssi;
    const uint8_t* bssid = s_apRecords[j].bssid;
    uint8_t ch = s_apRecords[j].primary;
    wifi_auth_mode_t auth = s_apRecords[j].authmode;
    for (size_t i = 0; i < credsCount; ++i) {
      if (strcmp(ss, _list[i].ssid) == 0 && r > cand[i].rssi) {
        cand[i].rssi = r;
        memcpy(cand[i].bssid, bssid, 6);
        cand[i].chan = ch;
        cand[i].enc = auth;
        cand[i].present = true;
      }
    }
  }

  // --- 1) Réseaux visibles: tous sont tentés (priorité au meilleur RSSI). Plus de rejet sur seuil. ----
  size_t order[NVSConfig::MAX_WIFI_SAVED_NETWORKS];
  size_t orderCount = 0;
  for (size_t i = 0; i < credsCount; ++i) {
    if (cand[i].present) {
      order[orderCount++] = i;
      if (cand[i].rssi >= ::SleepConfig::WIFI_RSSI_MINIMUM) {
        Serial.printf("[WiFi] ✅ Réseau %s (RSSI: %d dBm)\n", _list[i].ssid, cand[i].rssi);
      } else {
        Serial.printf("[WiFi] ⚠️ Réseau %s signal faible (RSSI: %d dBm) - tentative quand même\n",
                      _list[i].ssid, cand[i].rssi);
      }
    }
  }
  std::sort(order, order + orderCount, [&](size_t a, size_t b) { return cand[a].rssi > cand[b].rssi; });

  // Ajoute ensuite les credentials non détectés au scan (tenter quand même)
  for (size_t i = 0; i < credsCount; ++i) if (!cand[i].present) order[orderCount++] = i;

  for (size_t idx = 0; idx < orderCount; ++idx) {
    size_t i = order[idx];
    if(cand[i].present){
      char buf[40]; snprintf(buf,sizeof(buf),"Conn %s", _list[i].ssid);
      show(buf);
      Serial.printf("[WiFi] Try %s RSSI=%d ch=%d\n", _list[i].ssid, cand[i].rssi, cand[i].chan);
      // Utilise directement le BSSID et le canal détectés pour fiabiliser la connexion
      // Signature: WiFi.begin(ssid, pass, channel, bssid, connect)
      if (cand[i].enc == WIFI_AUTH_OPEN || strlen(_list[i].password) == 0) {
        wifiBeginSafe(_list[i].ssid, nullptr, cand[i].chan, cand[i].bssid);
      } else {
        wifiBeginSafe(_list[i].ssid, _list[i].password, cand[i].chan, cand[i].bssid);
      }
    }else{
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
      ets_printf("[WiFi] try inv %s (no rescan S3)\n", _list[i].ssid);
#else
      Serial.printf("[WiFi] 🔍 Tentative (invisible) %s ...\n", _list[i].ssid);
      Serial.printf("[Event] WiFi try invisible %s\n", _list[i].ssid);
#endif
      // Court délai avant rescan pour AP caché (laisse le RF/router répondre)
      vTaskDelay(pdMS_TO_TICKS(TimingConfig::WIFI_DELAY_BETWEEN_NETWORKS_MS));
      // Rescan avant tentative invisible : le réseau peut apparaître au 2e scan (S3 PSRAM: skip, bloque)
      int n2 = -1;
      bool foundInRescan = false;
      static wifi_ap_record_t s_rescanRecords[NetworkConfig::WIFI_SCAN_MAX_RECORDS];
      uint16_t num2 = 0;
#if !defined(BOARD_S3) || !defined(BOARD_HAS_PSRAM)
      if (esp_task_wdt_status(NULL) == ESP_OK) esp_task_wdt_reset();
      n2 = scanNetworksEspIdf(s_rescanRecords, NetworkConfig::WIFI_SCAN_MAX_RECORDS, &num2, true);
      if (esp_task_wdt_status(NULL) == ESP_OK) esp_task_wdt_reset();
#endif
      int8_t rescanRssi = -128;
      uint8_t rescanBssid[6] = {0};
      uint8_t rescanChan = 0;
      wifi_auth_mode_t rescanAuth = WIFI_AUTH_OPEN;
      if (n2 > 0) {
        for (uint16_t j = 0; j < num2; ++j) {
          char ssidRescan[33];
          memcpy(ssidRescan, s_rescanRecords[j].ssid, 32);
          ssidRescan[32] = '\0';
          size_t len = strnlen(ssidRescan, 32);
          ssidRescan[len] = '\0';
          int8_t rssi2 = s_rescanRecords[j].rssi;
          if (strcmp(ssidRescan, _list[i].ssid) == 0 && rssi2 > rescanRssi) {
            rescanRssi = rssi2;
            memcpy(rescanBssid, s_rescanRecords[j].bssid, 6);
            rescanChan = s_rescanRecords[j].primary;
            rescanAuth = s_rescanRecords[j].authmode;
            foundInRescan = true;
          }
        }
      }
      if (foundInRescan) {
        Serial.printf("[WiFi] Rescan: %s trouvé RSSI=%d ch=%d\n", _list[i].ssid, rescanRssi, rescanChan);
        if (rescanAuth == WIFI_AUTH_OPEN || strlen(_list[i].password) == 0) {
          wifiBeginSafe(_list[i].ssid, nullptr, rescanChan, rescanBssid);
        } else {
          wifiBeginSafe(_list[i].ssid, _list[i].password, rescanChan, rescanBssid);
        }
      } else {
        wifiBeginSafe(_list[i].ssid, _list[i].password);
      }
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
      ets_printf("[DBG] after begin, wait conn\n");
#endif
    }
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < _timeoutMs) {
      if (esp_task_wdt_status(NULL) == ESP_OK) {
        esp_task_wdt_reset();
      }
      vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (WiFi.status() == WL_CONNECTED) {
      show("WiFi OK");
      applyCustomDns();
      IPAddress ip = WiFi.localIP();
      char ipBuf[16];
      snprintf(ipBuf, sizeof(ipBuf), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
      Serial.printf("[WiFi] OK %s %s RSSI=%d\n", _list[i].ssid, ipBuf, WiFi.RSSI());
      WIFI_APPLY_MODEM_SLEEP(true);   // modem-sleep pour économie d'énergie
      _connecting = false;
      return true;
    }
    // Second tentative sans BSSID si la première (avec BSSID) a échoué
    if (cand[i].present) {
      vTaskDelay(pdMS_TO_TICKS(TimingConfig::WIFI_SECOND_ATTEMPT_DELAY_MS));
      Serial.printf("[WiFi] 🔄 Second tentative (générique) %s ...\n", _list[i].ssid);
      WiFi.disconnect(false, true);  // purge état précédent
      wifiBeginSafe(_list[i].ssid, _list[i].password);
      uint32_t start2 = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - start2 < _timeoutMs) {
        if (esp_task_wdt_status(NULL) == ESP_OK) {
          esp_task_wdt_reset();  // Reset watchdog pendant attente WiFi
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // Optimized delay - reduced from 250ms to 100ms for faster connection
      }
      if (WiFi.status() == WL_CONNECTED) {
        show("WiFi OK");
        applyCustomDns();
        IPAddress ip = WiFi.localIP();
        char ipBuf[16];
        snprintf(ipBuf, sizeof(ipBuf), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        Serial.printf("[WiFi] OK(2nd) %s %s RSSI=%d\n", _list[i].ssid, ipBuf, WiFi.RSSI());
        WIFI_APPLY_MODEM_SLEEP(true);   // modem-sleep pour économie d'énergie
        _connecting = false;
        return true;
      }
      // 3e tentative après court délai (routeur peut avoir besoin d'un moment après échec)
      Serial.printf("[WiFi] 🔄 3e tentative %s (délai 500 ms)...\n", _list[i].ssid);
      vTaskDelay(pdMS_TO_TICKS(500));
      WiFi.disconnect(false, true);
      wifiBeginSafe(_list[i].ssid, _list[i].password);
      uint32_t start3 = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - start3 < _timeoutMs) {
        if (esp_task_wdt_status(NULL) == ESP_OK) {
          esp_task_wdt_reset();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
      }
      if (WiFi.status() == WL_CONNECTED) {
        show("WiFi OK");
        applyCustomDns();
        IPAddress ip = WiFi.localIP();
        char ipBuf[16];
        snprintf(ipBuf, sizeof(ipBuf), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        Serial.printf("[WiFi] OK(3e) %s %s RSSI=%d\n", _list[i].ssid, ipBuf, WiFi.RSSI());
        WIFI_APPLY_MODEM_SLEEP(true);
        _connecting = false;
        return true;
      }
      // 4e tentative après délai plus long (routeur instable)
      Serial.printf("[WiFi] 🔄 4e tentative %s (délai 1 s)...\n", _list[i].ssid);
      vTaskDelay(pdMS_TO_TICKS(TimingConfig::WIFI_FOURTH_ATTEMPT_DELAY_MS));
      if (esp_task_wdt_status(NULL) == ESP_OK) esp_task_wdt_reset();
      WiFi.disconnect(false, true);
      wifiBeginSafe(_list[i].ssid, _list[i].password);
      uint32_t start4 = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - start4 < _timeoutMs) {
        if (esp_task_wdt_status(NULL) == ESP_OK) esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(100));
      }
      if (WiFi.status() == WL_CONNECTED) {
        show("WiFi OK");
        applyCustomDns();
        IPAddress ip = WiFi.localIP();
        char ipBuf[16];
        snprintf(ipBuf, sizeof(ipBuf), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        Serial.printf("[WiFi] OK(4e) %s %s RSSI=%d\n", _list[i].ssid, ipBuf, WiFi.RSSI());
        WIFI_APPLY_MODEM_SLEEP(true);
        _connecting = false;
        return true;
      }
    }
    show("Echec\nSuivant...");
    if (s_lastStaDisconnectReason >= 0) {
      const char* reasonStr = wifiDisconnectReasonStr(s_lastStaDisconnectReason);
      if (reasonStr) {
        Serial.printf("[WiFi] ❌ Connexion à %s impossible (raison %d: %s)\n", _list[i].ssid, s_lastStaDisconnectReason, reasonStr);
      } else {
        Serial.printf("[WiFi] ❌ Connexion à %s impossible (raison: %d)\n", _list[i].ssid, s_lastStaDisconnectReason);
      }
      s_lastStaDisconnectReason = -1;
    } else {
      Serial.printf("[WiFi] ❌ Connexion à %s impossible\n", _list[i].ssid);
    }
    // Délai entre réseaux pour éviter états intermédiaires du chip
    if (esp_task_wdt_status(NULL) == ESP_OK) esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(TimingConfig::WIFI_DELAY_BETWEEN_NETWORKS_MS));
  }
  // Aucun réseau connu disponible ou connexion échouée => AP secours
  Serial.println(F("[WiFi] ❌ Échec de connexion - démarrage AP de secours"));
  Serial.println("[Event] WiFi connect failed; starting AP");
  show("Echec totale");
  startFallbackAP();
  _connecting = false;
  return false;
}

bool WifiManager::connectTo(const char* ssid, const char* password, DisplayView* disp) {
  if (!ssid || ssid[0] == '\0') {
    Serial.println(F("[WiFi] SSID vide - connexion manuelle annulée"));
    return false;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(F("[WiFi] Déjà connecté - connexion manuelle ignorée"));
    return true;
  }
  if (_connecting) {
    Serial.println(F("[WiFi] Connexion déjà en cours - manuel ignoré"));
    return false;
  }
  _connecting = true;

  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);

  auto show = [disp](const char* msg){ if(disp && disp->isPresent()){ disp->showDiagnostic(msg);} };

  show("WiFi...");
  overrideBaseMac();
  if (FEATURE_WIFI_APSTA) {
    WiFi.mode(WIFI_AP_STA);
  } else {
    WiFi.mode(WIFI_STA);
  }
  esp_wifi_set_max_tx_power(50);  // 12.5 dBm (compatibilité routeurs 4G)
  if (WiFi.status() != WL_IDLE_STATUS) {
    WiFi.disconnect(false, true);
    vTaskDelay(pdMS_TO_TICKS(50));
  }

  Serial.printf("[WiFi] 🔗 Connexion manuelle à %s...\n", ssid);
  wifiBeginSafe(ssid, password);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < _timeoutMs) {
    if (esp_task_wdt_status(NULL) == ESP_OK) {
      esp_task_wdt_reset();
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  if (WiFi.status() == WL_CONNECTED) {
    show("WiFi OK");
    applyCustomDns();
    IPAddress ip = WiFi.localIP();
    char ipBuf[16];
    snprintf(ipBuf, sizeof(ipBuf), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    Serial.printf("[WiFi] ✅ Connecté à %s (%s, RSSI %d dBm)\n", ssid, ipBuf, WiFi.RSSI());
    Serial.printf("[Event] WiFi manual connected %s IP=%s RSSI=%d\n", ssid, ipBuf, WiFi.RSSI());
    WIFI_APPLY_MODEM_SLEEP(true);
    _lastAttemptMs = millis();
    _connecting = false;
    return true;
  }

  show("Echec");
  Serial.printf("[WiFi] ❌ Connexion manuelle à %s impossible\n", ssid);
  Serial.printf("[Event] WiFi manual connect failed %s\n", ssid);
  _lastAttemptMs = millis();
  _connecting = false;
  return false;
}

bool WifiManager::startFallbackAP(){
  // Crée un SSID unique basé sur l'adresse MAC
  uint64_t chipId = ESP.getEfuseMac();
  char ssid[32];
  snprintf(ssid, sizeof(ssid), "FFP3_%02X%02X", (uint8_t)(chipId>>8), (uint8_t)chipId);

  Serial.printf("[WiFi] 🚨 Démarrage AP de secours: %s\n", ssid);
  Serial.printf("[Event] WiFi AP start %s\n", ssid);
  if (FEATURE_WIFI_APSTA) {
    WiFi.mode(WIFI_AP_STA);
  } else {
    WiFi.mode(WIFI_AP);
  }
  // Choix de canal auto: scan rapide et sélection du canal le moins utilisé (1/6/11)
  int counts[12] = {0};
  // v11.176: Watchdog reset avant scan bloquant (2-5s) - audit robustesse
  if (esp_task_wdt_status(NULL) == ESP_OK) {
    esp_task_wdt_reset();
  }
  int n = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/false);
  // v11.176: Watchdog reset après scan bloquant
  if (esp_task_wdt_status(NULL) == ESP_OK) {
    esp_task_wdt_reset();
  }
  for (int i = 0; i < n; ++i) {
    int ch = WiFi.channel(i);
    if (ch >= 1 && ch <= 11) counts[ch-1]++;
  }
  int candidateChannels[3] = {1,6,11};
  int bestCh = 6; int bestCount = INT_MAX;
  for (int k=0;k<3;++k){ int ch=candidateChannels[k]; if (counts[ch-1] < bestCount){ bestCount=counts[ch-1]; bestCh=ch; } }
#if defined(BOARD_S3)
  WiFi.softAPConfig(
    IPAddress(NetworkConfig::AP_IP_B0, NetworkConfig::AP_IP_B1, NetworkConfig::AP_IP_B2, NetworkConfig::AP_IP_B3),
    IPAddress(NetworkConfig::AP_GW_B0, NetworkConfig::AP_GW_B1, NetworkConfig::AP_GW_B2, NetworkConfig::AP_GW_B3),
    IPAddress(NetworkConfig::AP_SUBNET_B0, NetworkConfig::AP_SUBNET_B1, NetworkConfig::AP_SUBNET_B2, NetworkConfig::AP_SUBNET_B3));
#endif
  bool ok = WiFi.softAP(ssid, nullptr, bestCh, false, 4); // AP ouvert, max 4 clients
  Serial.printf("[WiFi] 📡 softAP %s\n", ok?"✅ OK":"❌ ECHEC");
  if(ok){
    IPAddress apIP = WiFi.softAPIP();
    char apIPBuf[16];
    snprintf(apIPBuf, sizeof(apIPBuf), "%d.%d.%d.%d", apIP[0], apIP[1], apIP[2], apIP[3]);
    Serial.printf("[WiFi] 🌐 AP IP: %s\n", apIPBuf);
#if defined(BOARD_S3)
    if (ESP.getFreeHeap() >= NetworkConfig::MIN_HEAP_AP_DNS && !_apDnsStarted) {
      if (_apDnsServer.start(53, "*", WiFi.softAPIP())) {
        _apDnsStarted = true;
        Serial.println(F("[WiFi] DNS captive portal demarre"));
      }
    } else if (ESP.getFreeHeap() < NetworkConfig::MIN_HEAP_AP_DNS) {
      Serial.println(F("[WiFi] DNS AP skip heap low"));
    }
#endif
  }
  // En mode AP fallback: backoff long (45 s) pour éviter AUTH_EXPIRE et laisser la box 4G respirer
  // et limiter starvation core 0 par scans/connexions répétés (évite TWDT).
  _baseRetryMs = TimingConfig::WIFI_AP_FALLBACK_RETRY_INTERVAL_MS;
  _retryIntervalMs = _baseRetryMs;
  return ok;
}

void WifiManager::currentSSID(char* buffer, size_t bufferSize) const {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiHelpers::getSSID(buffer, bufferSize);
  } else {
    buffer[0] = '\0';
  }
}

void WifiManager::tryDelayedModeInit() {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
  // Désactivé: esp_wifi_init() bloque même depuis loop() après 10 s (stack actuelle ~3.0.x).
  // Pas de tentative → pas de freeze à 10 s ; firmware reste opérationnel (offline, pas d'AP).
  // Réactiver une tentative (ex. init après 10 s) quand platformio/espressif32 fournira
  // arduino-esp32 3.3.x (voir ESP32S3_HARDWARE_REFERENCE.md § WiFi S3 PSRAM).
  (void)0;
#else
  (void)0;
#endif
}

void WifiManager::startDelayedModeInitTask() {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
  // Désactivé: on utilise tryDelayedModeInit() depuis loop() après 10 s (évite blocage dans la tâche).
  (void)0;
#else
  (void)0;
#endif
}

void WifiManager::loop(DisplayView* disp){
  uint32_t now = millis();
  if (WiFi.status() == WL_CONNECTED) {
#if defined(BOARD_S3)
    if (_apDnsStarted) {
      _apDnsServer.stop();
      _apDnsStarted = false;
    }
#endif
    // Cadence plus douce quand connecté
    _baseRetryMs = max<uint32_t>(_baseRetryMs, 15000); // >=15 s
    _retryIntervalMs = _baseRetryMs;
    // Signal faible: désactiver modem-sleep pour stabiliser la liaison (évite déconnexions)
    int8_t rssi = getCurrentRSSI();
    if (rssi < ::SleepConfig::WIFI_RSSI_MODEM_SLEEP_THRESHOLD) {
      WIFI_APPLY_MODEM_SLEEP(false);
    } else {
      WIFI_APPLY_MODEM_SLEEP(true);
    }
    return;
  }
  if (_connecting) return; // éviter les conflits
  if(now - _lastAttemptMs < _retryIntervalMs) return;

#if defined(BOARD_S3)
  if (_apDnsStarted) {
    _apDnsServer.processNextRequest();
  }
#endif

  // Mémorise le moment pour éviter les rafales d’essais
  _lastAttemptMs = now;

  // Sinon on essaye à nouveau de se connecter.
  Serial.println(F("[WiFi] Tentative périodique de reconnexion"));
  bool ok = connect(disp);
  if (!ok && WiFi.status() != WL_CONNECTED) {
    // Backoff long en mode AP pour éviter AUTH_EXPIRE et TWDT (scans répétés)
    _retryIntervalMs = TimingConfig::WIFI_AP_FALLBACK_RETRY_INTERVAL_MS;
    _baseRetryMs = _retryIntervalMs;
  } else {
    _retryIntervalMs = _baseRetryMs; // reset en cas de succès
  }
}

// ========================================
// NOUVELLES MÉTHODES DE GESTION INTELLIGENTE
// ========================================

int8_t WifiManager::getCurrentRSSI() const {
  if (WiFi.status() != WL_CONNECTED) return -100; // Signal inexistant
  return WiFi.RSSI();
}

void WifiManager::getSignalQuality(char* buffer, size_t bufferSize) const {
  int8_t rssi = getCurrentRSSI();
  const char* quality;
  
  if (rssi >= ::SleepConfig::WIFI_RSSI_EXCELLENT) quality = "Excellent";
  else if (rssi >= ::SleepConfig::WIFI_RSSI_GOOD) quality = "Bon";
  else if (rssi >= ::SleepConfig::WIFI_RSSI_FAIR) quality = "Acceptable";
  else if (rssi >= ::SleepConfig::WIFI_RSSI_POOR) quality = "Faible";
  else if (rssi >= ::SleepConfig::WIFI_RSSI_MINIMUM) quality = "Très faible";
  else quality = "Critique";
  
  strncpy(buffer, quality, bufferSize - 1);
  buffer[bufferSize - 1] = '\0';
}

bool WifiManager::shouldReconnect() {
  if (WiFi.status() != WL_CONNECTED) return true; // Pas connecté = reconnexion nécessaire
  
  int8_t currentRSSI = getCurrentRSSI();
  uint32_t now = millis();
  
  // Signal critique = reconnexion immédiate
  if (currentRSSI < ::SleepConfig::WIFI_RSSI_CRITICAL) {
    Serial.printf("[WiFi] Signal critique (%d dBm) - reconnexion recommandée\n", currentRSSI);
    return true;
  }
  
  // Signal faible depuis trop longtemps = reconnexion
  if (currentRSSI < ::SleepConfig::WIFI_RSSI_POOR) {
    if (_weakSignalStartTime == 0) {
      _weakSignalStartTime = now;
    } else if (now - _weakSignalStartTime > ::SleepConfig::WIFI_WEAK_SIGNAL_THRESHOLD_MS) {
      Serial.printf("[WiFi] Signal faible (%d dBm) depuis %u ms - reconnexion\n", 
                    currentRSSI, now - _weakSignalStartTime);
      return true;
    }
  } else {
    // Signal acceptable, reset du compteur
    _weakSignalStartTime = 0;
  }
  
  // Limite de tentatives de reconnexion
  if (_reconnectAttempts >= ::SleepConfig::WIFI_MAX_RECONNECT_ATTEMPTS) {
    uint32_t timeSinceLastAttempt = now - _lastReconnectAttempt;
    if (timeSinceLastAttempt > 300000) { // 5 minutes de pause
      _reconnectAttempts = 0; // Reset compteur
      return true;
    }
    return false; // Trop de tentatives récentes
  }
  
  return false;
}

void WifiManager::checkConnectionStability() {
  uint32_t now = millis();
  
  // Vérification périodique de la stabilité
  if (now - _lastRSSICheck < ::SleepConfig::WIFI_STABILITY_CHECK_INTERVAL_MS) {
    return; // Pas encore temps
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    int8_t rssi = getCurrentRSSI();
    char quality[32];
    getSignalQuality(quality, sizeof(quality));
    
    Serial.printf("[WiFi] Vérification stabilité - RSSI: %d dBm (%s)\n", rssi, quality);
    
    if (shouldReconnect()) {
      Serial.println("[WiFi] Reconnexion intelligente déclenchée");
      Serial.printf("[Event] WiFi smart reconnect triggered RSSI=%d\n", rssi);
      
      // Incrémenter compteur de tentatives
      _reconnectAttempts++;
      _lastReconnectAttempt = now;
      
      // Déclencher reconnexion
      WiFi.disconnect(false, true);
      vTaskDelay(pdMS_TO_TICKS(::SleepConfig::WIFI_RECONNECT_DELAY_MS));
      // La reconnexion sera gérée par la boucle principale
    }
  }
  
  _lastRSSICheck = now;
}

// ========================================
// MÉTHODES POUR CONTRÔLE MANUEL WIFI
// ========================================

bool WifiManager::disconnect() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[WiFi] Pas de connexion active à déconnecter"));
    return false;
  }
  
  char currentSSIDBuf[33];
  WiFiHelpers::getSSID(currentSSIDBuf, sizeof(currentSSIDBuf));
  Serial.printf("[WiFi] Déconnexion manuelle de %s\n", currentSSIDBuf);
  Serial.printf("[Event] WiFi manual disconnect from %s\n", currentSSIDBuf);
  
  // Déconnexion propre
  WiFi.disconnect(false, true);
  _connecting = false;
  
  // Reset des compteurs de reconnexion automatique
  _lastAttemptMs = millis();
  _reconnectAttempts = 0;
  _weakSignalStartTime = 0;
  
  Serial.println(F("[WiFi] Déconnexion effectuée"));
  return true;
}

bool WifiManager::reconnect(DisplayView* disp) {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(F("[WiFi] Déjà connecté"));
    return true;
  }
  
  Serial.println(F("[WiFi] Reconnexion manuelle demandée"));
  Serial.println("[Event] WiFi manual reconnect requested");
  
  // Reset des compteurs pour permettre une reconnexion immédiate
  _lastAttemptMs = 0;
  _reconnectAttempts = 0;
  _weakSignalStartTime = 0;
  
  // Tenter la reconnexion
  bool success = connect(disp);
  
  if (success) {
    Serial.println(F("[WiFi] Reconnexion manuelle réussie"));
    Serial.println("[Event] WiFi manual reconnect successful");
  } else {
    Serial.println(F("[WiFi] Reconnexion manuelle échouée"));
    Serial.println("[Event] WiFi manual reconnect failed");
  }
  
  return success;
}