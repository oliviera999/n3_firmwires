#include "wifi_manager.h"
#include "display_view.h"
#include "config.h"
#include <algorithm>
#include <cstring>

WifiManager::WifiManager(const Credential* list, size_t count, uint32_t timeoutMs, uint32_t retryIntervalMs)
    : _list(list), _count(count), _timeoutMs(timeoutMs), _retryIntervalMs(retryIntervalMs), _lastAttemptMs(0) {}

bool WifiManager::connect(DisplayView* disp) {
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

  show("Scan WiFi...");
  if (FEATURE_WIFI_APSTA) {
    WiFi.mode(WIFI_AP_STA);
  } else {
    WiFi.mode(WIFI_STA);
  }
  // Tente un disconnect propre avant scan, puis petite attente
  WiFi.disconnect(false, true);
  vTaskDelay(pdMS_TO_TICKS(50));

  Serial.println(F("[WiFi] 🔍 Balayage des réseaux..."));
  int n = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/true);
  if(n<=0){ show("Aucun reseau"); } else { show("Scan OK"); }
  if (n <= 0) {
    Serial.println(F("[WiFi] ❌ Aucun réseau détecté (ou scan erreur)"));
    Serial.println("[Event] WiFi scan: no networks");
  } else {
    Serial.printf("[WiFi] 📡 %d réseaux trouvés:\n", n);
    for (int j = 0; j < n; ++j) {
      char scanSSIDBuf[33];
      strncpy(scanSSIDBuf, WiFi.SSID(j).c_str(), sizeof(scanSSIDBuf) - 1);
      scanSSIDBuf[sizeof(scanSSIDBuf) - 1] = '\0';
      Serial.printf("  - %s (%ddBm)%s\n", scanSSIDBuf, WiFi.RSSI(j), WiFi.encryptionType(j)==WIFI_AUTH_OPEN?" [OPEN]":"");
    }
  }

  // Associer chaque credential à la meilleure valeur RSSI détectée + infos BSSID/chan
  // Buffers fixes (pas de heap) - _count borné par NVSConfig::MAX_WIFI_SAVED_NETWORKS
  struct Cand { int8_t rssi; uint8_t bssid[6]; uint8_t chan; wifi_auth_mode_t enc; bool present; };
  Cand initC{ -128,{0},0, WIFI_AUTH_OPEN, false};
  Cand cand[NVSConfig::MAX_WIFI_SAVED_NETWORKS];
  for (size_t i = 0; i < _count; ++i) cand[i] = initC;
  for (int j = 0; j < n; ++j) {
    char scanSSIDBuf2[33];
    strncpy(scanSSIDBuf2, WiFi.SSID(j).c_str(), sizeof(scanSSIDBuf2) - 1);
    scanSSIDBuf2[sizeof(scanSSIDBuf2) - 1] = '\0';
    const char* ss = scanSSIDBuf2;
    int8_t r = WiFi.RSSI(j);
    uint8_t bssid[6];
    WiFi.BSSID(j, bssid);
    uint8_t ch = WiFi.channel(j);
    wifi_auth_mode_t auth = WiFi.encryptionType(j);
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

  // --- 1) Réseaux visibles, triés par RSSI décroissant (avec filtrage minimum) ----
  size_t order[NVSConfig::MAX_WIFI_SAVED_NETWORKS];
  size_t orderCount = 0;
  for (size_t i = 0; i < _count; ++i) {
    if (cand[i].present && cand[i].rssi >= ::SleepConfig::WIFI_RSSI_MINIMUM) {
      order[orderCount++] = i;
      Serial.printf("[WiFi] ✅ Réseau %s accepté (RSSI: %d dBm)\n", _list[i].ssid, cand[i].rssi);
    } else if (cand[i].present) {
      Serial.printf("[WiFi] ⚠️ Réseau %s rejeté (RSSI trop faible: %d dBm < %d dBm)\n",
                    _list[i].ssid, cand[i].rssi, ::SleepConfig::WIFI_RSSI_MINIMUM);
    }
  }
  std::sort(order, order + orderCount, [&](size_t a, size_t b) { return cand[a].rssi > cand[b].rssi; });

  // Ajoute ensuite les credentials non détectés (afin de tenter quand même)
  for (size_t i = 0; i < _count; ++i) if (!cand[i].present) order[orderCount++] = i;

  for (size_t idx = 0; idx < orderCount; ++idx) {
    size_t i = order[idx];
    if(cand[i].present){
      char buf[40]; snprintf(buf,sizeof(buf),"Conn %s", _list[i].ssid);
      show(buf);
      Serial.printf("[WiFi] 🔗 Tentative (vu) %s (RSSI %d dBm, ch %d)...\n", _list[i].ssid, cand[i].rssi, cand[i].chan);
      Serial.printf("[Event] WiFi try seen %s RSSI=%d ch=%d\n", _list[i].ssid, cand[i].rssi, cand[i].chan);
      // Utilise directement le BSSID et le canal détectés pour fiabiliser la connexion
      // Signature: WiFi.begin(ssid, pass, channel, bssid, connect)
      if (cand[i].enc == WIFI_AUTH_OPEN || strlen(_list[i].password) == 0) {
        WiFi.begin(_list[i].ssid, nullptr /*pass*/, cand[i].chan, cand[i].bssid);
      } else {
        WiFi.begin(_list[i].ssid, _list[i].password, cand[i].chan, cand[i].bssid);
      }
    }else{
      Serial.printf("[WiFi] 🔍 Tentative (invisible) %s ...\n", _list[i].ssid);
      Serial.printf("[Event] WiFi try invisible %s\n", _list[i].ssid);
      if(strlen(_list[i].password)==0){ WiFi.begin(_list[i].ssid); }
      else { WiFi.begin(_list[i].ssid, _list[i].password); }
    }
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < _timeoutMs) {
      vTaskDelay(pdMS_TO_TICKS(100)); // Optimized delay - reduced from 250ms to 100ms for faster connection
    }
    if (WiFi.status() == WL_CONNECTED) {
      show("WiFi OK");
      IPAddress ip = WiFi.localIP();
      char ipBuf[16];
      snprintf(ipBuf, sizeof(ipBuf), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
      Serial.printf("[WiFi] ✅ Connecté à %s (%s, RSSI %d dBm)\n", _list[i].ssid, ipBuf, WiFi.RSSI());
      Serial.printf("[Event] WiFi connected %s IP=%s RSSI=%d\n", _list[i].ssid, ipBuf, WiFi.RSSI());
      WiFi.setSleep(true);   // active le modem-sleep pour économie d'énergie
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
        vTaskDelay(pdMS_TO_TICKS(100)); // Optimized delay - reduced from 250ms to 100ms for faster connection
      }
      if (WiFi.status() == WL_CONNECTED) {
        show("WiFi OK");
        IPAddress ip = WiFi.localIP();
        char ipBuf[16];
        snprintf(ipBuf, sizeof(ipBuf), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        Serial.printf("[WiFi] ✅ Connecté à %s (%s, RSSI %d dBm) - 2ème tentative\n", _list[i].ssid, ipBuf, WiFi.RSSI());
        Serial.printf("[Event] WiFi connected (2nd) %s IP=%s RSSI=%d\n", _list[i].ssid, ipBuf, WiFi.RSSI());
        WiFi.setSleep(true);   // active le modem-sleep pour économie d'énergie
        _connecting = false;
        return true;
      }
    }
    show("Echec\nSuivant...");
    Serial.printf("[WiFi] ❌ Connexion à %s impossible\n", _list[i].ssid);
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
  WiFi.disconnect(false, true);
  vTaskDelay(pdMS_TO_TICKS(50));

  Serial.printf("[WiFi] 🔗 Connexion manuelle à %s...\n", ssid);
  if (password && strlen(password) > 0) {
    WiFi.begin(ssid, password);
  } else {
    WiFi.begin(ssid);
  }

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < _timeoutMs) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  if (WiFi.status() == WL_CONNECTED) {
    show("WiFi OK");
    IPAddress ip = WiFi.localIP();
    char ipBuf[16];
    snprintf(ipBuf, sizeof(ipBuf), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    Serial.printf("[WiFi] ✅ Connecté à %s (%s, RSSI %d dBm)\n", ssid, ipBuf, WiFi.RSSI());
    Serial.printf("[Event] WiFi manual connected %s IP=%s RSSI=%d\n", ssid, ipBuf, WiFi.RSSI());
    WiFi.setSleep(true);
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
  int n = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/false);
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
  // En mode APSTA, active un intervalle de retry court pour capter rapidement un réseau connu
  // Objectif: connexion en <= 30 s lorsqu'un SSID connu arrive à portée
  _baseRetryMs = 5000;          // scan toutes les 5 s
  _retryIntervalMs = _baseRetryMs;
  return ok;
}

void WifiManager::currentSSID(char* buffer, size_t bufferSize) const {
  if (WiFi.status() == WL_CONNECTED) {
    char ssidBuf[33];
    strncpy(ssidBuf, WiFi.SSID().c_str(), sizeof(ssidBuf) - 1);
    ssidBuf[sizeof(ssidBuf) - 1] = '\0';
    strncpy(buffer, ssidBuf, bufferSize - 1);
    buffer[bufferSize - 1] = '\0';
  } else {
    buffer[0] = '\0';
  }
}

void WifiManager::loop(DisplayView* disp){
  uint32_t now = millis();
  if (WiFi.status() == WL_CONNECTED) {
    // Cadence plus douce quand connecté
    _baseRetryMs = max<uint32_t>(_baseRetryMs, 15000); // >=15 s
    _retryIntervalMs = _baseRetryMs;
    // ✅ SUPPRIMÉ: WiFi.setSleep(false) - Garde le modem-sleep activé pour économie d'énergie
    return;
  }
  if (_connecting) return; // éviter les conflits
  if(now - _lastAttemptMs < _retryIntervalMs) return;

  // Mémorise le moment pour éviter les rafales d’essais
  _lastAttemptMs = now;

  // Si déjà connecté en STA -> rien à faire
  if(WiFi.status() == WL_CONNECTED) return;

  // Sinon on essaye à nouveau de se connecter.
  Serial.println(F("[WiFi] Tentative périodique de reconnexion"));
  bool ok = connect(disp);
  if (!ok && WiFi.status() != WL_CONNECTED) {
    // Ajuster cadence pour compromis stabilité/conso: retry ~7-10 s
    _retryIntervalMs = 7000;
    _baseRetryMs = 7000;
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

bool WifiManager::isSignalStable() {
  if (WiFi.status() != WL_CONNECTED) return false;
  
  int8_t currentRSSI = getCurrentRSSI();
  
  // Vérifier si le signal est suffisant
  if (currentRSSI < ::SleepConfig::WIFI_RSSI_MINIMUM) return false;
  
  // Vérifier la stabilité temporelle (pas de variations trop importantes)
  uint32_t now = millis();
  if (_lastRSSICheck == 0) {
    _lastRSSICheck = now;
    _lastRSSI = currentRSSI;
    return true; // Premier check, considéré comme stable
  }
  
  if (now - _lastRSSICheck < ::SleepConfig::WIFI_STABILITY_CHECK_INTERVAL_MS) {
    return true; // Pas encore temps de vérifier
  }
  
  // Vérifier la variation du signal
  int8_t rssiVariation = abs(currentRSSI - _lastRSSI);
  bool stable = (rssiVariation <= 5); // Variation max de 5 dBm
  
  _lastRSSICheck = now;
  _lastRSSI = currentRSSI;
  
  return stable;
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
  strncpy(currentSSIDBuf, WiFi.SSID().c_str(), sizeof(currentSSIDBuf) - 1);
  currentSSIDBuf[sizeof(currentSSIDBuf) - 1] = '\0';
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

void WifiManager::getConnectionStatus(char* buffer, size_t bufferSize) const {
  if (WiFi.status() == WL_CONNECTED) {
    char ssidBuf[33];
    strncpy(ssidBuf, WiFi.SSID().c_str(), sizeof(ssidBuf) - 1);
    ssidBuf[sizeof(ssidBuf) - 1] = '\0';
    const char* ssid = ssidBuf;
    char ipBuf[16];
    IPAddress ip = WiFi.localIP();
    snprintf(ipBuf, sizeof(ipBuf), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    int8_t rssi = WiFi.RSSI();
    char quality[32];
    getSignalQuality(quality, sizeof(quality));
    
    snprintf(buffer, bufferSize, "Connecté à %s (%s) - %s (%d dBm)", ssid, ipBuf, quality, rssi);
  } else if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
    char apIPBuf[16];
    IPAddress apIP = WiFi.softAPIP();
    snprintf(apIPBuf, sizeof(apIPBuf), "%d.%d.%d.%d", apIP[0], apIP[1], apIP[2], apIP[3]);
    snprintf(buffer, bufferSize, "Mode AP - IP: %s", apIPBuf);
  } else {
    strncpy(buffer, "Déconnecté", bufferSize - 1);
    buffer[bufferSize - 1] = '\0';
  }
}