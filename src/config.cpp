#include "project_config.h"
#include "config_manager.h"
#include <ArduinoJson.h>

ConfigManager::ConfigManager() 
    : _bouffeMatinOk(false), _bouffeMidiOk(false), _bouffeSoirOk(false), 
      _lastJourBouf(-1), _pompeAquaLocked(false), _forceWakeUp(false), _otaUpdateFlag(true),
      _cachedBouffeMatinOk(false), _cachedBouffeMidiOk(false), _cachedBouffeSoirOk(false),
      _cachedLastJourBouf(-1), _cachedPompeAquaLocked(false), _cachedForceWakeUp(false),
      _cachedOtaUpdateFlag(true), _flagsChanged(false) {
}

void ConfigManager::loadBouffeFlags() {
  _preferences.begin("bouffe", true);
  _bouffeMatinOk = _preferences.getBool("bouffeMatinOk", false);
  _bouffeMidiOk = _preferences.getBool("bouffeMidiOk", false);
  _bouffeSoirOk = _preferences.getBool("bouffeSoirOk", false);
  _lastJourBouf = _preferences.getInt("lastJourBouf", -1);
  _pompeAquaLocked = _preferences.getBool("pompeAquaLocked", false);
  _forceWakeUp = _preferences.getBool("forceWakeUp", false);
  _preferences.end();
  
  // Chargement du flag OTA depuis la flash
  _preferences.begin("ota", true);
  _otaUpdateFlag = _preferences.getBool("updateFlag", true); // Valeur par défaut true
  _preferences.end();
  
  // Initialisation du cache après chargement
  updateCache();
  _flagsChanged = false;
  
  Serial.println(F("[Config] Flags de bouffe chargés depuis la flash"));
  Serial.printf("[Config] Matin: %s, Midi: %s, Soir: %s, Jour: %d, Pompe lock: %s, ForceWakeUp: %s\n",
                _bouffeMatinOk ? "OK" : "KO", 
                _bouffeMidiOk ? "OK" : "KO", 
                _bouffeSoirOk ? "OK" : "KO", 
                _lastJourBouf,
                _pompeAquaLocked ? "OUI" : "NON",
                _forceWakeUp ? "OUI" : "NON");
  Serial.printf("[Config] Flag OTA update: %s\n", _otaUpdateFlag ? "true" : "false");
}

void ConfigManager::saveBouffeFlags() {
  // Sauvegarde seulement si des changements ont été détectés
  if (!_flagsChanged) {
    Serial.println(F("[Config] Aucun changement détecté - pas de sauvegarde NVS"));
    return;
  }
  
  _preferences.begin("bouffe", false);
  _preferences.putBool("bouffeMatinOk", _bouffeMatinOk);
  _preferences.putBool("bouffeMidiOk", _bouffeMidiOk);
  _preferences.putBool("bouffeSoirOk", _bouffeSoirOk);
  _preferences.putInt("lastJourBouf", _lastJourBouf);
  _preferences.putBool("pompeAquaLocked", _pompeAquaLocked);
  _preferences.putBool("forceWakeUp", _forceWakeUp);
  _preferences.end();
  
  // Mise à jour du cache après sauvegarde
  updateCache();
  _flagsChanged = false;
  
  Serial.println(F("[Config] Flags de bouffe sauvegardés dans la flash (changements détectés)"));
}

void ConfigManager::forceSaveBouffeFlags() {
  _preferences.begin("bouffe", false);
  _preferences.putBool("bouffeMatinOk", _bouffeMatinOk);
  _preferences.putBool("bouffeMidiOk", _bouffeMidiOk);
  _preferences.putBool("bouffeSoirOk", _bouffeSoirOk);
  _preferences.putInt("lastJourBouf", _lastJourBouf);
  _preferences.putBool("pompeAquaLocked", _pompeAquaLocked);
  _preferences.putBool("forceWakeUp", _forceWakeUp);
  _preferences.end();
  
  updateCache();
  _flagsChanged = false;
  
  Serial.println(F("[Config] Flags de bouffe sauvegardés de force dans la flash"));
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
  // Vérifier si le JSON a changé avant de sauvegarder
  String cachedJson;
  _preferences.begin("remoteVars", true);
  cachedJson = _preferences.getString("json", "");
  _preferences.end();
  
  if (cachedJson == json) {
    Serial.println(F("[Config] Variables distantes inchangées - pas de sauvegarde NVS"));
    return;
  }
  
  _preferences.begin("remoteVars", false);
  _preferences.putString("json", json);
  _preferences.end();
  Serial.println(F("[Config] Variables distantes sauvegardées dans la flash (changements détectés)"));
}

bool ConfigManager::loadRemoteVars(String& json) {
  _preferences.begin("remoteVars", true);
  json = _preferences.getString("json", "");
  _preferences.end();
  if (json.length() > 0) {
    Serial.println(F("[Config] Variables distantes chargées depuis la flash"));
    return true;
  }
  return false;
}

void ConfigManager::setOtaUpdateFlag(bool value) {
  if (_otaUpdateFlag == value) {
    Serial.printf("[Config] Flag OTA inchangé (%s) - pas de sauvegarde NVS\n", value ? "true" : "false");
    return;
  }
  
  _otaUpdateFlag = value;
  _preferences.begin("ota", false);
  _preferences.putBool("updateFlag", value);
  _preferences.end();
  _cachedOtaUpdateFlag = value;
  
  Serial.printf("[Config] Flag OTA update mis à jour: %s\n", value ? "true" : "false");
}

bool ConfigManager::getOtaUpdateFlag() const {
  return _otaUpdateFlag;
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
}

bool ConfigManager::loadConfigFromNVS() {
  Serial.println(F("========================================"));
  Serial.println(F("[Config] 📥 Chargement COMPLET depuis NVS"));
  Serial.println(F("========================================"));
  
  // 1. Charger les flags de bouffe (déjà implémenté)
  loadBouffeFlags();
  
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
    if (doc.containsKey("emailAddress") || doc.containsKey("mail")) {
      const char* email = doc.containsKey("emailAddress") ? 
                          doc["emailAddress"].as<const char*>() : 
                          doc["mail"].as<const char*>();
      Serial.printf("[Config]   - Email: %s\n", email ? email : "(vide)");
    }
    
    if (doc.containsKey("emailEnabled") || doc.containsKey("mailNotif")) {
      Serial.printf("[Config]   - Email notifications: %s\n",
                    doc.containsKey("emailEnabled") ? 
                    (doc["emailEnabled"].as<bool>() ? "activées" : "désactivées") :
                    (String(doc["mailNotif"].as<const char*>()) == "checked" ? "activées" : "désactivées"));
    }
    
    if (doc.containsKey("feedMorning") || doc.containsKey("105")) {
      int val = doc.containsKey("feedMorning") ? doc["feedMorning"].as<int>() : doc["105"].as<int>();
      Serial.printf("[Config]   - Heure matin: %dh\n", val);
    }
    
    if (doc.containsKey("feedNoon") || doc.containsKey("106")) {
      int val = doc.containsKey("feedNoon") ? doc["feedNoon"].as<int>() : doc["106"].as<int>();
      Serial.printf("[Config]   - Heure midi: %dh\n", val);
    }
    
    if (doc.containsKey("feedEvening") || doc.containsKey("107")) {
      int val = doc.containsKey("feedEvening") ? doc["feedEvening"].as<int>() : doc["107"].as<int>();
      Serial.printf("[Config]   - Heure soir: %dh\n", val);
    }
    
    if (doc.containsKey("feedBigDur") || doc.containsKey("tempsGros") || doc.containsKey("111")) {
      int val = doc.containsKey("feedBigDur") ? doc["feedBigDur"].as<int>() : 
                (doc.containsKey("tempsGros") ? doc["tempsGros"].as<int>() : doc["111"].as<int>());
      Serial.printf("[Config]   - Durée gros: %ds\n", val);
    }
    
    if (doc.containsKey("feedSmallDur") || doc.containsKey("tempsPetits") || doc.containsKey("112")) {
      int val = doc.containsKey("feedSmallDur") ? doc["feedSmallDur"].as<int>() : 
                (doc.containsKey("tempsPetits") ? doc["tempsPetits"].as<int>() : doc["112"].as<int>());
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