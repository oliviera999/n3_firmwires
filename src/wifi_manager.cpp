#include "wifi_manager.h"
#include "display_view.h"
#include "config.h"
#include "esp_wifi.h"  // Pour esp_wifi_scan_get_ap_records (éviter String Arduino)
#include <algorithm>
#include <cstring>
#include <esp_task_wdt.h>
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
#include "rom/ets_sys.h"
#endif

// DNS personnalisé : l'API Arduino-ESP32 n'expose pas setDNS() ; garder DHCP.
// Pour forcer un DNS (ex. 8.8.8.8), configurer le routeur (DHCP option 6) ou utiliser
// une IP statique + WiFi.config(ip, gw, mask, dns1, dns2). Ici on ne fait rien.
static void applyCustomDns() {
#if defined(WIFI_USE_CUSTOM_DNS) && (WIFI_USE_CUSTOM_DNS)
  (void)0;  // Réservé pour évolution (IDF API ou config statique)
#endif
}

WifiManager::WifiManager(const Credential* list, size_t count, uint32_t timeoutMs, uint32_t retryIntervalMs)
    : _list(list), _count(count), _timeoutMs(timeoutMs), _retryIntervalMs(retryIntervalMs), _lastAttemptMs(0) {}

bool WifiManager::connect(DisplayView* disp) {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
  ets_printf("[BOOT] connectWifi entry\n");
#endif
  if (WiFi.status() == WL_CONNECTED) return true;
  if (_connecting) {
    Serial.println(F("[WiFi] Connexion déjà en cours - skip"));
    return false;
  }
  _connecting = true;

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
  // #region agent log (H-A: WiFi.mode)
  // S3 PSRAM: WiFi.mode() bloque durablement sur ce board/driver ; on skip l'init WiFi au boot pour que le setup termine (firmware opérationnel en offline + AP secours)
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
  if (WiFi.getMode() != (FEATURE_WIFI_APSTA ? (wifi_mode_t)WIFI_AP_STA : (wifi_mode_t)WIFI_STA)) {
    ets_printf("[BOOT] S3 PSRAM: skip WiFi.mode at boot (evite blocage)\n");
    _connecting = false;
    return false;
  }
  ets_printf("[DBG H-A] after WiFi.mode\n");
#else
  if (FEATURE_WIFI_APSTA) {
    WiFi.mode(WIFI_AP_STA);
  } else {
    WiFi.mode(WIFI_STA);
  }
#endif
  // #endregion
  // Maximiser la puissance TX pour améliorer la connexion aux réseaux faibles/instables
  esp_wifi_set_max_tx_power(82);  // 20.5 dBm (max 2.4 GHz), unité 0.25 dBm
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
  int n = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/true);
  // v11.176: Watchdog reset après scan bloquant
  if (esp_task_wdt_status(NULL) == ESP_OK) {
    esp_task_wdt_reset();
  }
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
  ets_printf("[BOOT] wifi scan done n=%d\n", n);
#endif
  if(n<=0){ show("Aucun reseau"); } else { show("Scan OK"); }
  // Lecture résultats via API Arduino (scanNetworks a déjà consommé les résultats côté driver;
  // esp_wifi_scan_get_ap_records() renvoyait 0 en double lecture → on lit depuis la couche Arduino).
  static wifi_ap_record_t s_apRecords[NetworkConfig::WIFI_SCAN_MAX_RECORDS];
  uint16_t num = 0;
  if (n > 0) {
    num = (n <= (int)NetworkConfig::WIFI_SCAN_MAX_RECORDS) ? (uint16_t)n : NetworkConfig::WIFI_SCAN_MAX_RECORDS;
    for (int j = 0; j < num; ++j) {
      String ssidStr = WiFi.SSID(j);
      memset(s_apRecords[j].ssid, 0, 32);
      size_t sl = (ssidStr.length() < 32u) ? (size_t)ssidStr.length() : 32u;
      if (sl > 0 && ssidStr.c_str()) memcpy(s_apRecords[j].ssid, ssidStr.c_str(), sl);
      s_apRecords[j].rssi = (int8_t)WiFi.RSSI(j);
      const uint8_t* bssidPtr = WiFi.BSSID(j);
      if (bssidPtr) memcpy(s_apRecords[j].bssid, bssidPtr, 6);
      else memset(s_apRecords[j].bssid, 0, 6);
      s_apRecords[j].primary = (uint8_t)WiFi.channel(j);
      s_apRecords[j].authmode = (wifi_auth_mode_t)WiFi.encryptionType(j);
    }
  }
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
      Serial.printf("  - %s (%ddBm)%s\n", scanSSIDBuf, s_apRecords[j].rssi,
                    s_apRecords[j].authmode == WIFI_AUTH_OPEN ? " [OPEN]" : "");
    }
  }

  // Associer chaque credential à la meilleure valeur RSSI détectée + infos BSSID/chan
  struct Cand { int8_t rssi; uint8_t bssid[6]; uint8_t chan; wifi_auth_mode_t enc; bool present; };
  Cand initC{ -128,{0},0, WIFI_AUTH_OPEN, false};
  Cand cand[NVSConfig::MAX_WIFI_SAVED_NETWORKS];
  for (size_t i = 0; i < _count; ++i) cand[i] = initC;
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
    for (size_t i = 0; i < _count; ++i) {
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
  for (size_t i = 0; i < _count; ++i) {
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
  for (size_t i = 0; i < _count; ++i) if (!cand[i].present) order[orderCount++] = i;

  for (size_t idx = 0; idx < orderCount; ++idx) {
    size_t i = order[idx];
    if(cand[i].present){
      char buf[40]; snprintf(buf,sizeof(buf),"Conn %s", _list[i].ssid);
      show(buf);
      Serial.printf("[WiFi] Try %s RSSI=%d ch=%d\n", _list[i].ssid, cand[i].rssi, cand[i].chan);
      // Utilise directement le BSSID et le canal détectés pour fiabiliser la connexion
      // Signature: WiFi.begin(ssid, pass, channel, bssid, connect)
      if (cand[i].enc == WIFI_AUTH_OPEN || strlen(_list[i].password) == 0) {
        WiFi.begin(_list[i].ssid, nullptr /*pass*/, cand[i].chan, cand[i].bssid);
      } else {
        WiFi.begin(_list[i].ssid, _list[i].password, cand[i].chan, cand[i].bssid);
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
#if !defined(BOARD_S3) || !defined(BOARD_HAS_PSRAM)
      if (esp_task_wdt_status(NULL) == ESP_OK) esp_task_wdt_reset();
      n2 = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/true);
      if (esp_task_wdt_status(NULL) == ESP_OK) esp_task_wdt_reset();
#endif
      int8_t rescanRssi = -128;
      uint8_t rescanBssid[6] = {0};
      uint8_t rescanChan = 0;
      wifi_auth_mode_t rescanAuth = WIFI_AUTH_OPEN;
      if (n2 > 0) {
        uint16_t num2 = (n2 <= (int)NetworkConfig::WIFI_SCAN_MAX_RECORDS) ? (uint16_t)n2 : NetworkConfig::WIFI_SCAN_MAX_RECORDS;
        for (int j = 0; j < num2; ++j) {
          String ssidStr2 = WiFi.SSID(j);
          int8_t rssi2 = (int8_t)WiFi.RSSI(j);
          if (ssidStr2.equals(_list[i].ssid) && rssi2 > rescanRssi) {
            rescanRssi = rssi2;
            const uint8_t* bssidPtr2 = WiFi.BSSID(j);
            if (bssidPtr2) memcpy(rescanBssid, bssidPtr2, 6);
            rescanChan = (uint8_t)WiFi.channel(j);
            rescanAuth = (wifi_auth_mode_t)WiFi.encryptionType(j);
            foundInRescan = true;
          }
        }
      }
      if (foundInRescan) {
        Serial.printf("[WiFi] Rescan: %s trouvé RSSI=%d ch=%d\n", _list[i].ssid, rescanRssi, rescanChan);
        if (rescanAuth == WIFI_AUTH_OPEN || strlen(_list[i].password) == 0) {
          WiFi.begin(_list[i].ssid, nullptr, rescanChan, rescanBssid);
        } else {
          WiFi.begin(_list[i].ssid, _list[i].password, rescanChan, rescanBssid);
        }
      } else {
        if (strlen(_list[i].password) == 0) { WiFi.begin(_list[i].ssid); }
        else { WiFi.begin(_list[i].ssid, _list[i].password); }
      }
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
      ets_printf("[DBG] after begin, wait conn\n");
#endif
    }
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < _timeoutMs) {
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
      Serial.printf("[WiFi] OK %s %s RSSI=%d\n", _list[i].ssid, ipBuf, WiFi.RSSI());
      WIFI_APPLY_MODEM_SLEEP(true);   // modem-sleep pour économie d'énergie
      _connecting = false;
      return true;
    }
    // Second tentative sans BSSID si la première (avec BSSID) a échoué
    if (cand[i].present) {
      Serial.printf("[WiFi] 🔄 Second tentative (générique) %s ...\n", _list[i].ssid);
      WiFi.disconnect(false, true);  // purge état précédent
      if(strlen(_list[i].password)==0){
        WiFi.begin(_list[i].ssid);
      }else{
        WiFi.begin(_list[i].ssid, _list[i].password);
      }
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
      if (strlen(_list[i].password) == 0) {
        WiFi.begin(_list[i].ssid);
      } else {
        WiFi.begin(_list[i].ssid, _list[i].password);
      }
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
      if (strlen(_list[i].password) == 0) {
        WiFi.begin(_list[i].ssid);
      } else {
        WiFi.begin(_list[i].ssid, _list[i].password);
      }
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
    Serial.printf("[WiFi] ❌ Connexion à %s impossible\n", _list[i].ssid);
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
  if (FEATURE_WIFI_APSTA) {
    WiFi.mode(WIFI_AP_STA);
  } else {
    WiFi.mode(WIFI_STA);
  }
  esp_wifi_set_max_tx_power(82);  // 20.5 dBm pour liens faibles
  if (WiFi.status() != WL_IDLE_STATUS) {
    WiFi.disconnect(false, true);
    vTaskDelay(pdMS_TO_TICKS(50));
  }

  Serial.printf("[WiFi] 🔗 Connexion manuelle à %s...\n", ssid);
  if (password && strlen(password) > 0) {
    WiFi.begin(ssid, password);
  } else {
    WiFi.begin(ssid);
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
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
  ets_printf("[WiFi] S3 PSRAM: skip AP (WiFi.mode bloquant)\n");
  return false;
#endif
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
  bool ok = WiFi.softAP(ssid, nullptr, bestCh, false, 4); // AP ouvert, max 4 clients
  Serial.printf("[WiFi] 📡 softAP %s\n", ok?"✅ OK":"❌ ECHEC");
  if(ok){
    IPAddress apIP = WiFi.softAPIP();
    char apIPBuf[16];
    snprintf(apIPBuf, sizeof(apIPBuf), "%d.%d.%d.%d", apIP[0], apIP[1], apIP[2], apIP[3]);
    Serial.printf("[WiFi] 🌐 AP IP: %s\n", apIPBuf);
  }
  // En mode AP fallback: intervalle allongé (20 s) pour éviter starvation core 0
  // par la tâche wifi lors des scans/connexions répétés (évite TWDT).
  _baseRetryMs = 20000;         // scan toutes les 20 s
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
  static bool s_done = false;
  if (s_done) return;
  if (millis() < 3000) return;
  s_done = true;
  wifi_mode_t target = (FEATURE_WIFI_APSTA && (FEATURE_WIFI_APSTA != 0))
      ? (wifi_mode_t)WIFI_AP_STA
      : (wifi_mode_t)WIFI_STA;
  WiFi.mode(target);
  ets_printf("[WiFi] S3 PSRAM: WiFi.mode done (delayed 3s)\n");
#endif
  (void)0;
}

void WifiManager::loop(DisplayView* disp){
  uint32_t now = millis();
  if (WiFi.status() == WL_CONNECTED) {
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

  // Mémorise le moment pour éviter les rafales d’essais
  _lastAttemptMs = now;

  // Sinon on essaye à nouveau de se connecter.
  Serial.println(F("[WiFi] Tentative périodique de reconnexion"));
  bool ok = connect(disp);
  if (!ok && WiFi.status() != WL_CONNECTED) {
    // Cadence allongée pour éviter starvation core 0 (évite TWDT en mode AP)
    _retryIntervalMs = 20000;
    _baseRetryMs = 20000;
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