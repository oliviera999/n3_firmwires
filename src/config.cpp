#include "project_config.h"
#include "config_manager.h"
#include <ArduinoJson.h>

ConfigManager::ConfigManager() 
    : _bouffeMatinOk(false), _bouffeMidiOk(false), _bouffeSoirOk(false), 
      _lastJourBouf(-1), _pompeAquaLocked(false), _forceWakeUp(false), _otaUpdateFlag(true),
      _remoteSendEnabled(true), _remoteRecvEnabled(true),
      _cachedBouffeMatinOk(false), _cachedBouffeMidiOk(false), _cachedBouffeSoirOk(false),
      _cachedLastJourBouf(-1), _cachedPompeAquaLocked(false), _cachedForceWakeUp(false),
      _cachedOtaUpdateFlag(true), _flagsChanged(false),
      _cachedRemoteSendEnabled(true), _cachedRemoteRecvEnabled(true) {
}

void ConfigManager::loadBouffeFlags() {
  // v11.80: Utilisation du gestionnaire NVS centralisé
  Serial.println(F("[Config] 📥 Chargement flags depuis NVS centralisé"));
  
  // Chargement des flags de bouffe depuis le namespace CONFIG
  g_nvsManager.loadBool(NVS_NAMESPACES::CONFIG, "bouffe_matin", _bouffeMatinOk, false);
  g_nvsManager.loadBool(NVS_NAMESPACES::CONFIG, "bouffe_midi", _bouffeMidiOk, false);
  g_nvsManager.loadBool(NVS_NAMESPACES::CONFIG, "bouffe_soir", _bouffeSoirOk, false);
  g_nvsManager.loadInt(NVS_NAMESPACES::CONFIG, "bouffe_jour", _lastJourBouf, -1);
  g_nvsManager.loadBool(NVS_NAMESPACES::CONFIG, "bouffe_pompe_lock", _pompeAquaLocked, false);
  g_nvsManager.loadBool(NVS_NAMESPACES::CONFIG, "bouffe_force_wakeup", _forceWakeUp, false);
  
  // Chargement du flag OTA depuis le namespace SYSTEM
  g_nvsManager.loadBool(NVS_NAMESPACES::SYSTEM, "ota_update_flag", _otaUpdateFlag, true);
  
  // Initialisation du cache après chargement
  updateCache();
  _flagsChanged = false;
  
  Serial.println(F("[Config] ✅ Flags de bouffe chargés depuis NVS centralisé"));
  Serial.printf("[Config] Matin: %s, Midi: %s, Soir: %s, Jour: %d, Pompe lock: %s, ForceWakeUp: %s\n",
                _bouffeMatinOk ? "OK" : "KO", 
                _bouffeMidiOk ? "OK" : "KO", 
                _bouffeSoirOk ? "OK" : "KO", 
                _lastJourBouf,
                _pompeAquaLocked ? "OUI" : "NON",
                _forceWakeUp ? "OUI" : "NON");
  Serial.printf("[Config] Flag OTA update: %s\n", _otaUpdateFlag ? "true" : "false");
}

void ConfigManager::loadNetworkFlags() {
  // v11.80: Utilisation du gestionnaire NVS centralisé
  Serial.println(F("[Config] 📥 Chargement flags réseau depuis NVS centralisé"));
  
  // Chargement des flags réseau depuis le namespace SYSTEM
  g_nvsManager.loadBool(NVS_NAMESPACES::SYSTEM, "net_send_enabled", _remoteSendEnabled, true);
  g_nvsManager.loadBool(NVS_NAMESPACES::SYSTEM, "net_recv_enabled", _remoteRecvEnabled, true);
  
  _cachedRemoteSendEnabled = _remoteSendEnabled;
  _cachedRemoteRecvEnabled = _remoteRecvEnabled;
  
  Serial.printf("[Config] ✅ Net flags - send:%s recv:%s\n",
                _remoteSendEnabled?"ON":"OFF",
                _remoteRecvEnabled?"ON":"OFF");
}

void ConfigManager::saveBouffeFlags() {
  // Sauvegarde seulement si des changements ont été détectés
  if (!_flagsChanged) {
    Serial.println(F("[Config] Aucun changement détecté - pas de sauvegarde NVS"));
    return;
  }
  
  // v11.80: Utilisation du gestionnaire NVS centralisé
  Serial.println(F("[Config] 💾 Sauvegarde flags vers NVS centralisé"));
  
  // Sauvegarde des flags de bouffe dans le namespace CONFIG
  g_nvsManager.saveBool(NVS_NAMESPACES::CONFIG, "bouffe_matin", _bouffeMatinOk);
  g_nvsManager.saveBool(NVS_NAMESPACES::CONFIG, "bouffe_midi", _bouffeMidiOk);
  g_nvsManager.saveBool(NVS_NAMESPACES::CONFIG, "bouffe_soir", _bouffeSoirOk);
  g_nvsManager.saveInt(NVS_NAMESPACES::CONFIG, "bouffe_jour", _lastJourBouf);
  g_nvsManager.saveBool(NVS_NAMESPACES::CONFIG, "bouffe_pompe_lock", _pompeAquaLocked);
  g_nvsManager.saveBool(NVS_NAMESPACES::CONFIG, "bouffe_force_wakeup", _forceWakeUp);
  
  // Mise à jour du cache après sauvegarde
  updateCache();
  _flagsChanged = false;
  
  Serial.println(F("[Config] ✅ Flags de bouffe sauvegardés dans NVS centralisé"));
}

void ConfigManager::forceSaveBouffeFlags() {
  // v11.80: Utilisation du gestionnaire NVS centralisé
  Serial.println(F("[Config] 💾 Sauvegarde forcée flags vers NVS centralisé"));
  
  // Sauvegarde forcée des flags de bouffe dans le namespace CONFIG
  g_nvsManager.saveBool(NVS_NAMESPACES::CONFIG, "bouffe_matin", _bouffeMatinOk);
  g_nvsManager.saveBool(NVS_NAMESPACES::CONFIG, "bouffe_midi", _bouffeMidiOk);
  g_nvsManager.saveBool(NVS_NAMESPACES::CONFIG, "bouffe_soir", _bouffeSoirOk);
  g_nvsManager.saveInt(NVS_NAMESPACES::CONFIG, "bouffe_jour", _lastJourBouf);
  g_nvsManager.saveBool(NVS_NAMESPACES::CONFIG, "bouffe_pompe_lock", _pompeAquaLocked);
  g_nvsManager.saveBool(NVS_NAMESPACES::CONFIG, "bouffe_force_wakeup", _forceWakeUp);
  
  updateCache();
  _flagsChanged = false;
  
  Serial.println(F("[Config] ✅ Flags de bouffe sauvegardés de force dans NVS centralisé"));
}

void ConfigManager::resetBouffeFlags() {
  _bouffeMatinOk = false;
  _bouffeMidiOk = false;
  _bouffeSoirOk = false;
  // Ne pas reset _lastJourBouf ici, il sera mis à jour par la logique de bouffe
  // Ne pas reset _pompeAquaLocked non plus
  // Ne pas reset _forceWakeUp non plus
  
  _flagsChanged = true; // Marquer comme changé pour forcer la sauvegarde
  
  Serial.println(F("[Config] Flags de bouffe réinitialisés"));
} 

void ConfigManager::saveRemoteVars(const String& json) {
  // v11.80: Utilisation du gestionnaire NVS centralisé avec compression JSON
  Serial.println(F("[Config] 💾 Sauvegarde variables distantes vers NVS centralisé (compressé)"));
  
  // Vérifier si le JSON a changé avant de sauvegarder
  String cachedJson;
  g_nvsManager.loadJsonDecompressed(NVS_NAMESPACES::CONFIG, "remote_json", cachedJson, "");
  
  if (cachedJson == json) {
    Serial.println(F("[Config] Variables distantes inchangées - pas de sauvegarde NVS"));
    return;
  }
  
  // Sauvegarde compressée dans le namespace CONFIG
  g_nvsManager.saveJsonCompressed(NVS_NAMESPACES::CONFIG, "remote_json", json);
  Serial.println(F("[Config] ✅ Variables distantes sauvegardées dans NVS centralisé (compressé)"));
}

bool ConfigManager::loadRemoteVars(String& json) {
  // v11.80: Utilisation du gestionnaire NVS centralisé avec décompression JSON
  Serial.println(F("[Config] 📥 Chargement variables distantes depuis NVS centralisé (décompressé)"));
  
  g_nvsManager.loadJsonDecompressed(NVS_NAMESPACES::CONFIG, "remote_json", json, "");
  
  if (json.length() > 0) {
    Serial.println(F("[Config] ✅ Variables distantes chargées depuis NVS centralisé (décompressé)"));
    return true;
  }
  
  Serial.println(F("[Config] ⚠️ Aucune variable distante trouvée"));
  return false;
}

void ConfigManager::setOtaUpdateFlag(bool value) {
  if (_otaUpdateFlag == value) {
    Serial.printf("[Config] Flag OTA inchangé (%s) - pas de sauvegarde NVS\n", value ? "true" : "false");
    return;
  }
  
  _otaUpdateFlag = value;
  
  // v11.80: Utilisation du gestionnaire NVS centralisé
  g_nvsManager.saveBool(NVS_NAMESPACES::SYSTEM, "ota_update_flag", value);
  _cachedOtaUpdateFlag = value;
  
  Serial.printf("[Config] ✅ Flag OTA update mis à jour: %s\n", value ? "true" : "false");
}

bool ConfigManager::getOtaUpdateFlag() const {
  return _otaUpdateFlag;
}

// Flags réseau (persistants immédiatement)
void ConfigManager::setRemoteSendEnabled(bool value){
  if (_remoteSendEnabled == value) return;
  _remoteSendEnabled = value;
  
  // v11.80: Utilisation du gestionnaire NVS centralisé
  g_nvsManager.saveBool(NVS_NAMESPACES::SYSTEM, "net_send_enabled", value);
  _cachedRemoteSendEnabled = value;
  Serial.printf("[Config] ✅ Net sendEnabled=%s\n", value?"true":"false");
}

void ConfigManager::setRemoteRecvEnabled(bool value){
  if (_remoteRecvEnabled == value) return;
  _remoteRecvEnabled = value;
  
  // v11.80: Utilisation du gestionnaire NVS centralisé
  g_nvsManager.saveBool(NVS_NAMESPACES::SYSTEM, "net_recv_enabled", value);
  _cachedRemoteRecvEnabled = value;
  Serial.printf("[Config] ✅ Net recvEnabled=%s\n", value?"true":"false");
}

// Setters optimisés avec détection de changements
void ConfigManager::setBouffeMatinOk(bool value) {
  if (_bouffeMatinOk != value) {
    _bouffeMatinOk = value;
    _flagsChanged = true;
    Serial.printf("[Config] Flag bouffe matin changé: %s\n", value ? "true" : "false");
  }
}

void ConfigManager::setBouffeMidiOk(bool value) {
  if (_bouffeMidiOk != value) {
    _bouffeMidiOk = value;
    _flagsChanged = true;
    Serial.printf("[Config] Flag bouffe midi changé: %s\n", value ? "true" : "false");
  }
}

void ConfigManager::setBouffeSoirOk(bool value) {
  if (_bouffeSoirOk != value) {
    _bouffeSoirOk = value;
    _flagsChanged = true;
    Serial.printf("[Config] Flag bouffe soir changé: %s\n", value ? "true" : "false");
  }
}

void ConfigManager::setLastJourBouf(int value) {
  if (_lastJourBouf != value) {
    _lastJourBouf = value;
    _flagsChanged = true;
    Serial.printf("[Config] Jour bouffe changé: %d\n", value);
  }
}

void ConfigManager::setPompeAquaLocked(bool value) {
  if (_pompeAquaLocked != value) {
    _pompeAquaLocked = value;
    _flagsChanged = true;
    Serial.printf("[Config] Pompe aqua lock changé: %s\n", value ? "true" : "false");
  }
}

void ConfigManager::setForceWakeUp(bool value) {
  if (_forceWakeUp != value) {
    _forceWakeUp = value;
    _flagsChanged = true;
    Serial.printf("[Config] Force wake up changé: %s\n", value ? "true" : "false");
  }
}

// Méthodes privées
bool ConfigManager::hasChanges() const {
  return _flagsChanged || 
         _bouffeMatinOk != _cachedBouffeMatinOk ||
         _bouffeMidiOk != _cachedBouffeMidiOk ||
         _bouffeSoirOk != _cachedBouffeSoirOk ||
         _lastJourBouf != _cachedLastJourBouf ||
         _pompeAquaLocked != _cachedPompeAquaLocked ||
         _forceWakeUp != _cachedForceWakeUp;
}

void ConfigManager::updateCache() {
  _cachedBouffeMatinOk = _bouffeMatinOk;
  _cachedBouffeMidiOk = _bouffeMidiOk;
  _cachedBouffeSoirOk = _bouffeSoirOk;
  _cachedLastJourBouf = _lastJourBouf;
  _cachedPompeAquaLocked = _pompeAquaLocked;
  _cachedForceWakeUp = _forceWakeUp;
  _cachedOtaUpdateFlag = _otaUpdateFlag;
  _cachedRemoteSendEnabled = _remoteSendEnabled;
  _cachedRemoteRecvEnabled = _remoteRecvEnabled;
}

bool ConfigManager::loadConfigFromNVS() {
  Serial.println(F("========================================"));
  Serial.println(F("[Config] 📥 Chargement COMPLET depuis NVS"));
  Serial.println(F("========================================"));
  
  // 1. Charger les flags de bouffe (déjà implémenté)
  loadBouffeFlags();
  // 1b. Charger les flags réseau (send/recv)
  loadNetworkFlags();
  
  // 2. Charger les variables distantes depuis remoteVars
  String cachedJson;
  bool hasRemoteVars = loadRemoteVars(cachedJson);
  
  if (!hasRemoteVars || cachedJson.length() == 0) {
    Serial.println(F("[Config] ⚠️ Aucune config en NVS - Valeurs par défaut utilisées"));
    Serial.println(F("[Config] 📋 Config par défaut:"));
    Serial.println(F("[Config]   - Email: (non défini)"));
    Serial.println(F("[Config]   - Email notifications: désactivées"));
    Serial.println(F("[Config]   - Seuil aquarium: valeur code"));
    Serial.println(F("[Config]   - Seuil réservoir: valeur code"));
    Serial.println(F("[Config]   - Seuil chauffage: valeur code"));
    Serial.println(F("========================================"));
    return false; // Valeurs par défaut utilisées
  }
  
  // 3. Parser le JSON et extraire les variables
  Serial.println(F("[Config] 📄 Config trouvée en NVS, parsing..."));
  
  // Note: Le JSON sera appliqué par AutomatismNetwork::applyConfigFromJson()
  // On se contente de logger ce qu'on a trouvé
  
  Serial.printf("[Config] 📦 JSON NVS: %u bytes\n", cachedJson.length());
  
  // Parser pour logging (ne pas appliquer ici, c'est fait dans AutomatismNetwork)
  ArduinoJson::DynamicJsonDocument doc(1024);
  auto err = deserializeJson(doc, cachedJson);
  
  if (!err) {
    Serial.println(F("[Config] ✅ JSON valide, contenu:"));
    
    // Logger les principales variables trouvées
    if (doc.containsKey("mail")) {
      const char* email = doc["mail"].as<const char*>();
      Serial.printf("[Config]   - Email: %s\n", email ? email : "(vide)");
    }
    
    if (doc.containsKey("mailNotif")) {
      Serial.printf("[Config]   - Email notifications: %s\n",
                    String(doc["mailNotif"].as<const char*>()) == "checked" ? "activées" : "désactivées");
    }
    
    if (doc.containsKey("bouffeMatin") || doc.containsKey("105")) {
      int val = doc.containsKey("bouffeMatin") ? doc["bouffeMatin"].as<int>() : doc["105"].as<int>();
      Serial.printf("[Config]   - Heure matin: %dh\n", val);
    }
    
    if (doc.containsKey("bouffeMidi") || doc.containsKey("106")) {
      int val = doc.containsKey("bouffeMidi") ? doc["bouffeMidi"].as<int>() : doc["106"].as<int>();
      Serial.printf("[Config]   - Heure midi: %dh\n", val);
    }
    
    if (doc.containsKey("bouffeSoir") || doc.containsKey("107")) {
      int val = doc.containsKey("bouffeSoir") ? doc["bouffeSoir"].as<int>() : doc["107"].as<int>();
      Serial.printf("[Config]   - Heure soir: %dh\n", val);
    }
    
    if (doc.containsKey("tempsGros") || doc.containsKey("111")) {
      int val = doc.containsKey("tempsGros") ? doc["tempsGros"].as<int>() : doc["111"].as<int>();
      Serial.printf("[Config]   - Durée gros: %ds\n", val);
    }
    
    if (doc.containsKey("tempsPetits") || doc.containsKey("112")) {
      int val = doc.containsKey("tempsPetits") ? doc["tempsPetits"].as<int>() : doc["112"].as<int>();
      Serial.printf("[Config]   - Durée petits: %ds\n", val);
    }
    
    if (doc.containsKey("aqThreshold") || doc.containsKey("102")) {
      int val = doc.containsKey("aqThreshold") ? doc["aqThreshold"].as<int>() : doc["102"].as<int>();
      Serial.printf("[Config]   - Seuil aquarium: %d cm\n", val);
    }
    
    if (doc.containsKey("tankThreshold") || doc.containsKey("103")) {
      int val = doc.containsKey("tankThreshold") ? doc["tankThreshold"].as<int>() : doc["103"].as<int>();
      Serial.printf("[Config]   - Seuil réservoir: %d cm\n", val);
    }
    
    if (doc.containsKey("heaterThreshold") || doc.containsKey("chauffageThreshold") || doc.containsKey("104")) {
      float val = doc.containsKey("heaterThreshold") ? doc["heaterThreshold"].as<float>() : 
                  (doc.containsKey("chauffageThreshold") ? doc["chauffageThreshold"].as<float>() : doc["104"].as<float>());
      Serial.printf("[Config]   - Seuil chauffage: %.1f°C\n", val);
    }
    
    if (doc.containsKey("refillDuration") || doc.containsKey("tempsRemplissageSec") || doc.containsKey("113")) {
      int val = doc.containsKey("refillDuration") ? doc["refillDuration"].as<int>() : 
                (doc.containsKey("tempsRemplissageSec") ? doc["tempsRemplissageSec"].as<int>() : doc["113"].as<int>());
      Serial.printf("[Config]   - Durée remplissage: %ds\n", val);
    }
    
    if (doc.containsKey("limFlood") || doc.containsKey("114")) {
      int val = doc.containsKey("limFlood") ? doc["limFlood"].as<int>() : doc["114"].as<int>();
      Serial.printf("[Config]   - Limite inondation: %d cm\n", val);
    }
    
    Serial.println(F("[Config] ✅ Configuration chargée avec succès depuis NVS"));
  } else {
    Serial.printf("[Config] ⚠️ Erreur parsing JSON: %s\n", err.c_str());
    Serial.println(F("[Config] Valeurs par défaut seront utilisées"));
  }
  
  Serial.println(F("========================================"));
  return true; // Config chargée depuis NVS
} 