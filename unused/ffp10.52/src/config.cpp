#include "project_config.h"
#include "config_manager.h"

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