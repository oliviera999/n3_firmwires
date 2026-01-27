#include "gpio_parser.h"
#include "automatism.h"
#include "nvs_manager.h" // v11.108
#include <cstring>

void GPIOParser::parseAndApply(const JsonDocument& doc, Automatism& autoCtrl) {
    Serial.println(F("[GPIOParser] === PARSING JSON SERVEUR ==="));
  // Diagnostics additionnels v11.74
  size_t presentKeys = 0;
    
    // v11.78: Collecter les GPIO virtuels pour application immédiate
    StaticJsonDocument<1024> configDoc;  // Augmenté de 512 à 1024 pour éviter les dépassements
    bool hasVirtualConfig = false;
    
    // Parcourir tous les GPIO définis dans le mapping
    for (size_t i = 0; i < GPIOMap::MAPPING_COUNT; i++) {
        const GPIOMapping& mapping = GPIOMap::ALL_MAPPINGS[i];
        char key[16];
        snprintf(key, sizeof(key), "%d", mapping.gpio);
        
        // Vérifier si la clé existe dans le document
        if (!doc.containsKey(key)) {
            continue; // Ce GPIO n'est pas dans la réponse du serveur
        }
    presentKeys++;
        
        JsonVariantConst value = doc[key];
    Serial.printf("[GPIOParser] GPIO %d (%s): ", mapping.gpio, mapping.description);
    const char* valueStr = value.is<const char*>() ? value.as<const char*>() : (value.is<int>() ? "" : "");
    Serial.printf("[GPIO] key=%s raw=%s\n", key, valueStr);
        
        // Appliquer selon type
        applyGPIO(mapping.gpio, value, autoCtrl, configDoc, hasVirtualConfig);
        
        // Sauvegarder NVS
        saveToNVS(mapping, value);
    }
    
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
            // Vérifier que cette clé n'a pas déjà été traitée comme GPIO numérique
            bool alreadyProcessed = false;
            for (size_t j = 0; j < GPIOMap::MAPPING_COUNT; j++) {
                const GPIOMapping& mapping = GPIOMap::ALL_MAPPINGS[j];
                char gpioKey[16];
                snprintf(gpioKey, sizeof(gpioKey), "%d", mapping.gpio);
                if (doc.containsKey(gpioKey)) {
                    // Vérifier si ce GPIO correspond à cette clé textuelle
                    const char* mappedKey = mapGPIOToConfigKey(mapping.gpio, doc[gpioKey]);
                    if (mappedKey && strcmp(mappedKey, textKey) == 0) {
                        alreadyProcessed = true;
                        break;
                    }
                }
            }
            
            if (!alreadyProcessed) {
                // Ajouter à configDoc pour application
                JsonVariantConst value = doc[textKey];
                if (configDoc.size() < 15 && configDoc.memoryUsage() < 900) {
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
        
        // v11.79: Vérification renforcée avant application
        if (configDoc.size() > 0 && configDoc.size() < 20 && configDoc.memoryUsage() < 950) {
            // Vérification supplémentaire de la mémoire disponible
            size_t freeHeap = ESP.getFreeHeap();
            if (freeHeap > 50000) { // Minimum 50KB de heap libre
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
        static bool lastTankState = false;
        bool state = parseBool(value);
        if (state && !lastTankState) {
            autoCtrl.startTankPumpManual();
            Serial.println("Pompe tank ON (front montant)");
        } else if (!state && lastTankState) {
            autoCtrl.stopTankPumpManual();
            Serial.println("Pompe tank OFF (front descendant)");
        } else {
            Serial.printf("Pompe tank %s (commande redondante)\n", state ? "ON" : "OFF");
        }
        lastTankState = state;
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
        static bool lastFeedSmallState = false;
        bool state = parseBool(value);
        if (state && !lastFeedSmallState) {
            autoCtrl.manualFeedSmall();
            Serial.println("Nourrissage petits (rising edge)");
        }
        lastFeedSmallState = state;
    }
    else if (gpio == GPIOMap::FEED_BIG.gpio) {
        // Déclenchement sur front montant uniquement (one-shot)
        static bool lastFeedBigState = false;
        bool state = parseBool(value);
        if (state && !lastFeedBigState) {
            autoCtrl.manualFeedBig();
            Serial.println("Nourrissage gros (rising edge)");
        }
        lastFeedBigState = state;
    }
    // Reset
    else if (gpio == GPIOMap::RESET_CMD.gpio) {
        if (parseBool(value)) {
            // Protection anti-boucle (déjà implémentée)
            Serial.println("Reset demandé");
            ESP.restart();
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
                // v11.79: Sécurisation renforcée de l'assignation JSON
                // Vérifier que le document n'est pas plein avant d'ajouter
                if (configDoc.size() < 15 && configDoc.memoryUsage() < 900) { // Limites de sécurité renforcées
                    // Utiliser directement la clé const char*
                    const char* keyStr = configKey;
                    
                    // v11.80: Conversion selon le type attendu du GPIO (pas le type reçu)
                    // Cela permet de gérer les cas où le serveur envoie des strings au lieu de nombres
                    bool assignmentOk = false;
                    
                    // Convertir selon le type attendu du GPIO, pas le type reçu
                    if (mapping->type == GPIOType::CONFIG_INT) {
                        // Pour CONFIG_INT, toujours convertir en int (même si reçu comme string)
                        int intVal = value.as<int>();
                        
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
                        // Pour CONFIG_FLOAT, toujours convertir en float (même si reçu comme string)
                        float floatVal = value.as<float>();
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
    // v11.108: Utilisation de g_nvsManager
    
    // Créer la clé préfixée
    char key[32];
    snprintf(key, sizeof(key), "gpio_%s", mapping.nvsKey);

    // Vérifier que la clé NVS n'est pas trop longue ou invalide
    if (mapping.nvsKey == nullptr || strlen(mapping.nvsKey) == 0 || strlen(mapping.nvsKey) > 15) {
        Serial.printf("[NVS] ⚠️ Clé NVS invalide, ignorée: %s\n", mapping.nvsKey ? mapping.nvsKey : "NULL");
        return;
    }
    
    switch (mapping.type) {
        case GPIOType::ACTUATOR:
        case GPIOType::CONFIG_BOOL: {
            bool boolVal = parseBool(value);
            g_nvsManager.saveBool(NVS_NAMESPACES::CONFIG, key, boolVal);
            Serial.printf("[NVS] Sauvegardé bool %s = %s\n", key, boolVal ? "true" : "false");
            break;
        }
        case GPIOType::CONFIG_INT: {
            int intVal = value.as<int>();
            g_nvsManager.saveInt(NVS_NAMESPACES::CONFIG, key, intVal);
            Serial.printf("[NVS] Sauvegardé int %s = %d\n", key, intVal);
            break;
        }
        case GPIOType::CONFIG_FLOAT: {
            float floatVal = value.as<float>();
            g_nvsManager.saveFloat(NVS_NAMESPACES::CONFIG, key, floatVal);
            Serial.printf("[NVS] Sauvegardé float %s = %.2f\n", key, floatVal);
            break;
        }
        case GPIOType::CONFIG_STRING: {
            char stringVal[128];
            const char* str = value.as<const char*>();
            if (str) {
                size_t len = strlen(str);
                size_t copyLen = (len < sizeof(stringVal) - 1) ? len : (sizeof(stringVal) - 1);
                strncpy(stringVal, str, copyLen);
                stringVal[copyLen] = '\0';
                if (len > 100) {
                    stringVal[100] = '\0';
                    Serial.printf("[NVS] ⚠️ String trop longue pour %s (%zu chars), tronquée\n", key, len);
                }
                g_nvsManager.saveString(NVS_NAMESPACES::CONFIG, key, stringVal);
                Serial.printf("[NVS] Sauvegardé string %s = '%s'\n", key, stringVal);
            } else {
                // Conversion depuis autres types si nécessaire
                if (value.is<int>()) {
                    snprintf(stringVal, sizeof(stringVal), "%d", value.as<int>());
                } else if (value.is<float>()) {
                    snprintf(stringVal, sizeof(stringVal), "%.2f", value.as<float>());
                } else if (value.is<bool>()) {
                    strncpy(stringVal, value.as<bool>() ? "true" : "false", sizeof(stringVal) - 1);
                    stringVal[sizeof(stringVal) - 1] = '\0';
                } else {
                    stringVal[0] = '\0';
                }
                g_nvsManager.saveString(NVS_NAMESPACES::CONFIG, key, stringVal);
                Serial.printf("[NVS] Sauvegardé string %s = '%s' (converti)\n", key, stringVal);
            }
            break;
        }
        default:
            Serial.printf("[NVS] ⚠️ Type non géré pour %s\n", key);
            break;
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
        
        // Vérifier que la clé NVS est valide
        if (mapping.nvsKey == nullptr || strlen(mapping.nvsKey) == 0 || strlen(mapping.nvsKey) > 15) {
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
