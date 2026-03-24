#include "gpio_parser.h"
#include "automatism.h"
#include "nvs_manager.h"  // v11.108
#include "app_tasks.h"   // netRequestOtaCheck (déclenchement OTA depuis serveur distant)
#include "app_context.h"
#include "config.h"
#include <WiFi.h>
#include <cstring>
#include <cmath>   // v11.164: fabsf pour comparaison float
#include <cstdlib> // v11.183: atoi/atof pour valeurs serveur en string

// v11.179: Variables d'état de détection de front (module-level pour reset au boot)
static bool s_lastTankState = false;
static bool s_lastFeedSmallState = false;
static bool s_lastFeedBigState = false;
// v11.189: Reset ESP32 sur front montant uniquement (évite reboot en boucle si sync envoie 110:1)
static bool s_lastResetState = false;

static bool tryStartOtaBeforeResetFromRemote() {
    extern AppContext g_appContext;

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(F("[Reset] distant: WiFi non connecté, reset direct"));
        return false;
    }

    if (g_appContext.otaManager.isUpdating()) {
        Serial.println(F("[Reset] distant: OTA déjà en cours, reset différé"));
        return true;
    }

    g_appContext.otaManager.setCurrentVersion(ProjectConfig::VERSION);
    if (!g_appContext.otaManager.checkForUpdate()) {
        Serial.println(F("[Reset] distant: aucune OTA disponible, reset direct"));
        return false;
    }

    if (g_appContext.otaManager.performUpdate()) {
        Serial.println(F("[Reset] distant: OTA disponible, mise à jour lancée avant reset"));
        return true;
    }

    Serial.println(F("[Reset] distant: OTA détectée mais échec démarrage update, reset direct"));
    return false;
}

void GPIOParser::resetEdgeDetectionState() {
    s_lastTankState = false;
    s_lastFeedSmallState = false;
    s_lastFeedBigState = false;
    s_lastResetState = false;
    Serial.println(F("[GPIOParser] État détection de front réinitialisé"));
}

static bool parseBoolFromDoc(JsonVariantConst v) {
    if (v.is<bool>()) return v.as<bool>();
    if (v.is<int>()) return v.as<int>() == 1;
    if (v.is<const char*>()) {
        const char* s = v.as<const char*>();
        if (s && (strcasecmp(s, "1") == 0 || strcasecmp(s, "true") == 0 || strcasecmp(s, "on") == 0)) return true;
        if (s && (strcasecmp(s, "0") == 0 || strcasecmp(s, "false") == 0)) return false;
    }
    return false;
}

void GPIOParser::seedFeedStateFromDoc(const JsonDocument& doc) {
    // Reset (110): seed pour ne pas redémarrer si le sync envoie 110:1 comme état courant.
    if (doc.containsKey("110")) {
        s_lastResetState = parseBoolFromDoc(doc["110"]);
    }
    // Nourrissage 108/109: seed au 1er poll pour éviter faux front si le serveur envoie
    // 108:1 ou 109:1 comme état courant (ex. BDD ou cache). Seul un vrai changement 0→1
    // après ce seed déclenchera un nourrissage.
    if (doc.containsKey("108")) {
        s_lastFeedSmallState = parseBoolFromDoc(doc["108"]);
    }
    if (doc.containsKey("109")) {
        s_lastFeedBigState = parseBoolFromDoc(doc["109"]);
    }
    Serial.printf("[GPIOParser] État edge initialisé: reset=%d feedSmall=%d feedBig=%d\n",
                  s_lastResetState ? 1 : 0, s_lastFeedSmallState ? 1 : 0, s_lastFeedBigState ? 1 : 0);
    Serial.printf("[DBG] seedFeedStateFromDoc 108=%d 109=%d lastSmall=%d lastBig=%d hypothesis=D\n",
                  doc.containsKey("108") ? 1 : 0, doc.containsKey("109") ? 1 : 0,
                  s_lastFeedSmallState ? 1 : 0, s_lastFeedBigState ? 1 : 0);
}

void GPIOParser::parseAndApply(const JsonDocument& doc, Automatism& autoCtrl) {
    Serial.println(F("[GPIOParser] === PARSING JSON SERVEUR ==="));
    // Déclenchement OTA depuis serveur distant (page contrôle prod/test) : triggerOtaCheck = true
    if (doc.containsKey("triggerOtaCheck") && doc["triggerOtaCheck"].as<bool>()) {
        AppTasks::netRequestOtaCheck();
        Serial.println(F("[GPIOParser] triggerOtaCheck reçu: demande vérification OTA"));
    }
  size_t presentKeys = 0;
    // v11.189: Seed reset (110) géré uniquement par seedFeedStateFromDoc au 1er poll
    // v11.192: Réactiver reset via serveur distant - edge detection dans applyGPIO
    // v11.78: Collecter les GPIO virtuels pour application immédiate
    StaticJsonDocument<1024> configDoc;  // Augmenté de 512 à 1024 pour éviter les dépassements
    bool hasVirtualConfig = false;
    
    // Parcourir tous les GPIO définis dans le mapping
    // v11.191: Accepter clé numérique ("16") OU symbolique (serverPostName, ex: "etatPompeAqua")
    // pour prise en compte des commandes serveur distant quel que soit le format renvoyé
    for (size_t i = 0; i < GPIOMap::MAPPING_COUNT; i++) {
        const GPIOMapping& mapping = GPIOMap::ALL_MAPPINGS[i];
        char keyNum[16];
        snprintf(keyNum, sizeof(keyNum), "%d", mapping.gpio);

        const char* keyUsed = nullptr;
        if (doc.containsKey(keyNum)) {
            keyUsed = keyNum;
        } else if (mapping.serverPostName && doc.containsKey(mapping.serverPostName)) {
            keyUsed = mapping.serverPostName;
        }
        if (!keyUsed) {
            continue;
        }
        presentKeys++;

        JsonVariantConst value = doc[keyUsed];
        Serial.printf("[GPIOParser] GPIO %d (%s): ", mapping.gpio, mapping.description);
        if (value.is<int>()) {
            Serial.printf("[GPIO] key=%s raw=%d\n", keyUsed, value.as<int>());
        } else if (value.is<float>()) {
            Serial.printf("[GPIO] key=%s raw=%.2f\n", keyUsed, value.as<float>());
        } else if (value.is<const char*>()) {
            const char* valueStr = value.as<const char*>();
            Serial.printf("[GPIO] key=%s raw=%s\n", keyUsed, valueStr ? valueStr : "");
        } else {
            Serial.printf("[GPIO] key=%s raw=\n", keyUsed);
        }
        // v11.192: Reset (110) réactivé via serveur - edge detection 0→1 dans applyGPIO
        applyGPIO(mapping.gpio, value, autoCtrl, configDoc, hasVirtualConfig);
        saveToNVS(mapping, value);
    }
    // #region agent log
    Serial.printf("[DBG] parseAndApply presentKeys=%u hypothesis=H6\n", (unsigned)presentKeys);
    // #endregion

    // v11.98: Traiter aussi les clés textuelles (compatibilité avec /dbvars/update et autres sources)
    // Mapping des clés textuelles vers les clés de configuration
    const char* textKeys[] = {
        "aqThreshold", "tankThreshold", "chauffageThreshold", 
        "limFlood", "tempsRemplissageSec", "refillDuration",
        "tempsGros", "tempsPetits",
        "bouffeMatin", "bouffeMidi", "bouffeSoir",
        "mail", "mailNotif", "FreqWakeUp", "WakeUp", "forceWakeUp"
    };
    
    for (size_t i = 0; i < sizeof(textKeys) / sizeof(textKeys[0]); i++) {
        const char* textKey = textKeys[i];
        if (doc.containsKey(textKey)) {
            // Vérifier que cette clé n'a pas déjà été traitée (numérique ou symbolique)
            bool alreadyProcessed = false;
            for (size_t j = 0; j < GPIOMap::MAPPING_COUNT; j++) {
                const GPIOMapping& mapping = GPIOMap::ALL_MAPPINGS[j];
                char gpioKey[16];
                snprintf(gpioKey, sizeof(gpioKey), "%d", mapping.gpio);
                bool hasNum = doc.containsKey(gpioKey);
                bool hasSym = mapping.serverPostName && doc.containsKey(mapping.serverPostName);
                if (hasNum || hasSym) {
                    JsonVariantConst v = hasNum ? doc[gpioKey] : doc[mapping.serverPostName];
                    const char* mappedKey = mapGPIOToConfigKey(mapping.gpio, v);
                    if (mappedKey && strcmp(mappedKey, textKey) == 0) {
                        alreadyProcessed = true;
                        break;
                    }
                }
            }
            
            if (!alreadyProcessed) {
                // Ajouter à configDoc pour application
                JsonVariantConst value = doc[textKey];
                // v11.176: Limites augmentées (20 clés, 950 bytes) pour config complète serveur distant
                if (configDoc.size() < 20 && configDoc.memoryUsage() < 950) {
                    configDoc[textKey] = value;
                    hasVirtualConfig = true;
                    Serial.printf("[GPIOParser] Clé textuelle '%s' ajoutée au document de config\n", textKey);
                }
            }
        }
    }
    
    // v11.78: Appliquer immédiatement les configurations virtuelles avec protection
    if (hasVirtualConfig) {
        Serial.println(F("[GPIOParser] 🔧 Application immédiate des GPIO virtuels"));
        Serial.printf("[GPIOParser] Document JSON: %u éléments, taille: %u bytes\n", 
                     configDoc.size(), configDoc.memoryUsage());
        
        // v11.176: Limites augmentées (25 clés, 1000 bytes) pour config complète serveur distant
        if (configDoc.size() > 0 && configDoc.size() < 25 && configDoc.memoryUsage() < 1000) {
            // Vérification supplémentaire de la mémoire disponible
            size_t freeHeap = ESP.getFreeHeap();
            if (freeHeap > 40000) { // Minimum 40KB de heap libre (réduit de 50KB)
                Serial.printf("[GPIOParser] Heap libre: %u bytes - Application sécurisée\n", freeHeap);
                autoCtrl.applyConfigFromJson(configDoc);
                Serial.println(F("[GPIOParser] ✅ Configurations virtuelles appliquées"));
            } else {
                Serial.printf("[GPIOParser] ⚠️ Heap insuffisant (%u bytes), application reportée\n", freeHeap);
            }
        } else {
            Serial.printf("[GPIOParser] ⚠️ Document JSON invalide (size:%u, mem:%u bytes), ignoré\n", 
                         configDoc.size(), configDoc.memoryUsage());
        }
    }
    
  Serial.printf("[GPIOParser] Présents: %u / %u\n", (unsigned)presentKeys, (unsigned)GPIOMap::MAPPING_COUNT);
  if (presentKeys > 0 || hasVirtualConfig) {
    Serial.printf("[GPIOParser] Config appliquée (RAM+NVS), clés: %u\n", (unsigned)presentKeys);
  }
  Serial.println(F("[GPIOParser] === FIN PARSING ==="));
}

void GPIOParser::applyGPIO(uint8_t gpio, JsonVariantConst value, Automatism& autoCtrl, 
                           JsonDocument& configDoc, bool& hasVirtualConfig) {
    // Actionneurs physiques
    if (gpio == GPIOMap::PUMP_AQUA.gpio) {
        bool state = parseBool(value);
        state ? autoCtrl.startAquaPumpManualLocal() : autoCtrl.stopAquaPumpManualLocal();
        Serial.printf("Pompe aqua %s\n", state ? "ON" : "OFF");
    }
    else if (gpio == GPIOMap::PUMP_TANK.gpio) {
        // v11.179: Utilise variable module-level pour reset au boot
        bool state = parseBool(value);
        if (state && !s_lastTankState) {
            autoCtrl.startTankPumpManual();
            Serial.println("Pompe tank ON (front montant)");
        } else if (!state && s_lastTankState) {
            autoCtrl.stopTankPumpManual();
            Serial.println("Pompe tank OFF (front descendant)");
        } else {
            Serial.printf("Pompe tank %s (commande redondante)\n", state ? "ON" : "OFF");
        }
        s_lastTankState = state;
    }
    else if (gpio == GPIOMap::HEATER.gpio) {
        bool state = parseBool(value);
        state ? autoCtrl.startHeaterManualLocal() : autoCtrl.stopHeaterManualLocal();
        Serial.printf("Chauffage %s\n", state ? "ON" : "OFF");
    }
    else if (gpio == GPIOMap::LIGHT.gpio) {
        bool state = parseBool(value);
        state ? autoCtrl.startLightManualLocal() : autoCtrl.stopLightManualLocal();
        Serial.printf("Lumière %s\n", state ? "ON" : "OFF");
    }
    // Nourrissage
    else if (gpio == GPIOMap::FEED_SMALL.gpio) {
        // Déclenchement sur front montant uniquement (one-shot)
        // v11.179: Utilise variable module-level pour reset au boot
        bool state = parseBool(value);
        int triggered = (state && !s_lastFeedSmallState) ? 1 : 0;
        Serial.printf("[DBG] FEED_SMALL state=%d last=%d triggered=%d hypothesis=D\n", state ? 1 : 0, s_lastFeedSmallState ? 1 : 0, triggered);
        if (state && !s_lastFeedSmallState) {
            if (autoCtrl.isFeedingInProgress()) {
                Serial.println(F("[GPIOParser] Nourrissage petits ignoré - cycle en cours"));
            } else {
                autoCtrl.manualFeedSmall();
                autoCtrl.notifyRemoteFeedExecuted(true);
                autoCtrl.markCurrentFeedingSlotAsDone();
                Serial.println("Nourrissage petits (rising edge)");
            }
        }
        s_lastFeedSmallState = state;
    }
    else if (gpio == GPIOMap::FEED_BIG.gpio) {
        // Déclenchement sur front montant uniquement (one-shot)
        // v11.179: Utilise variable module-level pour reset au boot
        bool state = parseBool(value);
        int triggered = (state && !s_lastFeedBigState) ? 1 : 0;
        Serial.printf("[DBG] FEED_BIG state=%d last=%d triggered=%d hypothesis=D\n", state ? 1 : 0, s_lastFeedBigState ? 1 : 0, triggered);
        if (state && !s_lastFeedBigState) {
            if (autoCtrl.isFeedingInProgress()) {
                Serial.println(F("[GPIOParser] Nourrissage gros ignoré - cycle en cours"));
            } else {
                autoCtrl.manualFeedBig();
                autoCtrl.notifyRemoteFeedExecuted(false);
                autoCtrl.markCurrentFeedingSlotAsDone();
                Serial.println("Nourrissage gros (rising edge)");
            }
        }
        s_lastFeedBigState = state;
    }
    // Reset (GPIO 110): front montant uniquement - serveur distant ou seed 1er poll évite reboot en boucle
    else if (gpio == GPIOMap::RESET_CMD.gpio) {
        bool state = parseBool(value);
        if (state && !s_lastResetState) {
            s_lastResetState = true;
            Serial.println(F("Reset demandé"));
            Serial.println(F("[Sync] Reboot distant exécuté (GPIO 110=1)"));
            if (!tryStartOtaBeforeResetFromRemote()) {
                ESP.restart();
            }
        } else if (!state) {
            s_lastResetState = false;
        }
    }
    // Configuration - appliquée via setters Automatism
    else {
        const char* valueStr = value.is<const char*>() ? value.as<const char*>() : "";
        Serial.printf("Config GPIO %d = %s\n", gpio, valueStr);
        
        // v11.77: Appliquer immédiatement les configurations virtuelles
        const GPIOMapping* mapping = GPIOMap::findByGPIO(gpio);
        if (mapping && (mapping->type == GPIOType::CONFIG_INT || 
                        mapping->type == GPIOType::CONFIG_FLOAT || 
                        mapping->type == GPIOType::CONFIG_STRING || 
                        mapping->type == GPIOType::CONFIG_BOOL)) {
            
            // Mapper GPIO vers clés attendues par applyConfigFromJson
            const char* configKey = mapGPIOToConfigKey(gpio, value);
            if (configKey && strlen(configKey) > 0) {
                // v11.176: Limites augmentées (20 clés, 950 bytes) pour config complète serveur distant
                // Vérifier que le document n'est pas plein avant d'ajouter
                if (configDoc.size() < 20 && configDoc.memoryUsage() < 950) {
                    // Utiliser directement la clé const char*
                    const char* keyStr = configKey;
                    
                    // v11.80: Conversion selon le type attendu du GPIO (pas le type reçu)
                    // Cela permet de gérer les cas où le serveur envoie des strings au lieu de nombres
                    bool assignmentOk = false;
                    
                    // Convertir selon le type attendu du GPIO, pas le type reçu
                    if (mapping->type == GPIOType::CONFIG_INT) {
                        // v11.183: Serveur ffp3 envoie souvent des strings ("200", "9"); as<int>() retourne 0 sur string
                        int intVal = value.is<const char*>()
                            ? atoi(value.as<const char*>())
                            : value.as<int>();
                        
                        // Validation spécifique pour les heures de nourrissage (GPIO 105, 106, 107)
                        if (gpio == 105 || gpio == 106 || gpio == 107) {
                            if (intVal < 0 || intVal > 23) {
                                Serial.printf("[GPIOParser] ⚠️ Heure invalide GPIO %d: %d (doit être 0-23), ignorée\n", 
                                             gpio, intVal);
                                return; // Ignorer la valeur invalide
                            }
                        }
                        
                        configDoc[keyStr] = intVal;
                        assignmentOk = true;
                        Serial.printf("[GPIOParser] GPIO %d (INT) -> %s = %d (converti depuis %s)\n", 
                                     gpio, keyStr, intVal, valueStr);
                    }
                    else if (mapping->type == GPIOType::CONFIG_FLOAT) {
                        // v11.183: Serveur envoie parfois string ("20"); as<float>() retourne 0 sur string
                        float floatVal = value.is<const char*>()
                            ? static_cast<float>(atof(value.as<const char*>()))
                            : value.as<float>();
                        configDoc[keyStr] = floatVal;
                        assignmentOk = true;
                        Serial.printf("[GPIOParser] GPIO %d (FLOAT) -> %s = %.2f (converti depuis %s)\n", 
                                     gpio, keyStr, floatVal, valueStr);
                    }
                    else if (mapping->type == GPIOType::CONFIG_BOOL) {
                        // Pour CONFIG_BOOL, utiliser parseBool qui gère les strings
                        bool boolVal = parseBool(value);
                        configDoc[keyStr] = boolVal;
                        assignmentOk = true;
                        Serial.printf("[GPIOParser] GPIO %d (BOOL) -> %s = %s (converti depuis %s)\n", 
                                     gpio, keyStr, boolVal ? "true" : "false", valueStr);
                    }
                    else if (mapping->type == GPIOType::CONFIG_STRING) {
                        // Pour CONFIG_STRING, vérifier que c'est bien une string valide
                        if (value.is<const char*>()) {
                            const char* valPtr = value.as<const char*>();
                            if (valPtr != nullptr && strlen(valPtr) < 100) { // Limite de taille
                                configDoc[keyStr] = valPtr;
                                assignmentOk = true;
                                Serial.printf("[GPIOParser] GPIO %d (STRING) -> %s = '%s'\n", 
                                             gpio, keyStr, valPtr);
                            } else {
                                Serial.printf("[GPIOParser] ⚠️ String invalide GPIO %d, ignoré\n", gpio);
                            }
                        } else {
                            // Si ce n'est pas une string, convertir en string via buffer temporaire
                            char tempStr[256];
                            if (value.is<const char*>()) {
                                const char* str = value.as<const char*>();
                                if (str) {
                                    strncpy(tempStr, str, sizeof(tempStr) - 1);
                                    tempStr[sizeof(tempStr) - 1] = '\0';
                                } else {
                                    tempStr[0] = '\0';
                                }
                            } else {
                                // Conversion via snprintf pour les types numériques
                                if (value.is<int>()) {
                                    snprintf(tempStr, sizeof(tempStr), "%d", value.as<int>());
                                } else if (value.is<float>()) {
                                    snprintf(tempStr, sizeof(tempStr), "%.2f", value.as<float>());
                                } else if (value.is<bool>()) {
                                    strncpy(tempStr, value.as<bool>() ? "true" : "false", sizeof(tempStr) - 1);
                                    tempStr[sizeof(tempStr) - 1] = '\0';
                                } else {
                                    tempStr[0] = '\0';
                                }
                            }
                            configDoc[keyStr] = tempStr;
                            assignmentOk = true;
                                Serial.printf("[GPIOParser] GPIO %d (STRING) -> %s = '%s' (converti)\n", 
                                         gpio, keyStr, tempStr);
                        }
                    }
                    else {
                        Serial.printf("[GPIOParser] ⚠️ Type GPIO non géré: %d pour GPIO %d\n", 
                                     (int)mapping->type, gpio);
                    }
                    
                    if (assignmentOk) {
                        hasVirtualConfig = true;
                    }
                } else {
                    Serial.printf("[GPIOParser] ⚠️ Document JSON plein (size:%u, mem:%u), GPIO virtuel %d ignoré\n", 
                                 configDoc.size(), configDoc.memoryUsage(), gpio);
                }
            }
        }
    }
}

const char* GPIOParser::mapGPIOToConfigKey(uint8_t gpio, JsonVariantConst value) {
    // Mapper les GPIO virtuels vers les clés attendues par applyConfigFromJson
    switch (gpio) {
        case 100: // EMAIL_ADDR
            return "mail";
        case 101: // EMAIL_EN  
            return "mailNotif";
        case 102: // AQ_THRESHOLD
            return "aqThreshold";
        case 103: // TANK_THRESHOLD
            return "tankThreshold";
        case 104: // HEAT_THRESHOLD
            return "chauffageThreshold";
        case 105: // FEED_MORNING
            return "bouffeMatin";
        case 106: // FEED_NOON
            return "bouffeMidi";
        case 107: // FEED_EVENING
            return "bouffeSoir";
        case 111: // FEED_BIG_DUR
            return "tempsGros";
        case 112: // FEED_SMALL_DUR
            return "tempsPetits";
        case 113: // REFILL_DUR
            return "refillDuration";
        case 114: // LIM_FLOOD
            return "limFlood";
        case 115: // WAKEUP
            return "forceWakeUp";
        case 116: // FREQ_WAKEUP
            return "FreqWakeUp";
        default:
            return nullptr;
    }
}

void GPIOParser::saveToNVS(const GPIOMapping& mapping, JsonVariantConst value) {
    // v11.172: Seuls les ACTUATORS sont sauvegardés individuellement en NVS
    // Les CONFIG_* sont centralisés dans remote_json (source de vérité unique)
    if (mapping.type != GPIOType::ACTUATOR) {
        // Skip - config values are in remote_json
        return;
    }
    
    // Créer la clé préfixée
    char key[32];
    snprintf(key, sizeof(key), "gpio_%s", mapping.nvsKey);

    // Clé complète = "gpio_" (5 car.) + nvsKey ; limite NVS = 15 car.
    if (mapping.nvsKey == nullptr || strlen(mapping.nvsKey) == 0 || strlen(mapping.nvsKey) > 10) {
        Serial.printf("[NVS] ⚠️ Clé NVS invalide, ignorée: %s\n", mapping.nvsKey ? mapping.nvsKey : "NULL");
        return;
    }
    
    // v11.172: Seulement les actionneurs (bool) sont sauvegardés ici
    bool newVal = parseBool(value);
    bool currentVal = false;
    g_nvsManager.loadBool(NVS_NAMESPACES::CONFIG, key, currentVal, newVal);
    if (currentVal != newVal) {
        g_nvsManager.saveBool(NVS_NAMESPACES::CONFIG, key, newVal);
        Serial.printf("[NVS] ✏️ Actuateur %s = %s\n", key, newVal ? "ON" : "OFF");
    }
}


void GPIOParser::loadGPIOStatesFromNVS(Automatism& autoCtrl) {
    Serial.println(F("[GPIOParser] Chargement des états GPIO depuis NVS..."));
    
    // Charger les états des actionneurs depuis NVS
    for (size_t i = 0; i < GPIOMap::MAPPING_COUNT; i++) {
        const GPIOMapping& mapping = GPIOMap::ALL_MAPPINGS[i];
        
        // Seulement les actionneurs (type ACTUATOR)
        if (mapping.type != GPIOType::ACTUATOR) {
            continue;
        }
        
        // Clé complète = "gpio_" (5 car.) + nvsKey ; limite NVS = 15 car.
        if (mapping.nvsKey == nullptr || strlen(mapping.nvsKey) == 0 || strlen(mapping.nvsKey) > 10) {
            continue;
        }
        
        char key[32];
        snprintf(key, sizeof(key), "gpio_%s", mapping.nvsKey);

        bool state;
        g_nvsManager.loadBool(NVS_NAMESPACES::CONFIG, key, state, false);
        
        // Appliquer l'état selon le GPIO
        if (mapping.gpio == GPIOMap::HEATER.gpio) {
            if (state) {
                autoCtrl.startHeaterManualLocal();
                Serial.printf("[GPIOParser] 🔥 Chauffage restauré: ON\n");
            } else {
                autoCtrl.stopHeaterManualLocal();
                Serial.printf("[GPIOParser] 🔥 Chauffage restauré: OFF\n");
            }
        }
        else if (mapping.gpio == GPIOMap::LIGHT.gpio) {
            if (state) {
                autoCtrl.startLightManualLocal();
                Serial.printf("[GPIOParser] 💡 Lumière restaurée: ON\n");
            } else {
                autoCtrl.stopLightManualLocal();
                Serial.printf("[GPIOParser] 💡 Lumière restaurée: OFF\n");
            }
        }
        else if (mapping.gpio == GPIOMap::PUMP_AQUA.gpio) {
            if (state) {
                autoCtrl.startAquaPumpManualLocal();
                Serial.printf("[GPIOParser] 💧 Pompe aqua restaurée: ON\n");
            } else {
                autoCtrl.stopAquaPumpManualLocal();
                Serial.printf("[GPIOParser] 💧 Pompe aqua restaurée: OFF\n");
            }
        }
        // Note: PUMP_TANK volontairement exclue pour sécurité
    }
    
    Serial.println(F("[GPIOParser] ✅ États GPIO chargés depuis NVS"));
}
