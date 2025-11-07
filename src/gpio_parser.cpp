#include "gpio_parser.h"
#include "automatism.h"
#include <Preferences.h>

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
        String key = String(mapping.gpio);
        
        // Vérifier si la clé existe dans le document
        if (!doc.containsKey(key.c_str())) {
            continue; // Ce GPIO n'est pas dans la réponse du serveur
        }
    presentKeys++;
        
        JsonVariantConst value = doc[key.c_str()];
    Serial.printf("[GPIOParser] GPIO %d (%s): ", mapping.gpio, mapping.description);
    Serial.printf("[GPIO] key=%s raw=%s\n", key.c_str(), value.as<String>().c_str());
        
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
                String gpioKey = String(mapping.gpio);
                if (doc.containsKey(gpioKey.c_str())) {
                    // Vérifier si ce GPIO correspond à cette clé textuelle
                    String mappedKey = mapGPIOToConfigKey(mapping.gpio, doc[gpioKey.c_str()]);
                    if (mappedKey == textKey) {
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
        bool state = parseBool(value);
        state ? autoCtrl.startTankPumpManual() : autoCtrl.stopTankPumpManual();
        Serial.printf("Pompe tank %s\n", state ? "ON" : "OFF");
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
        Serial.printf("Config GPIO %d = %s\n", gpio, value.as<String>().c_str());
        
        // v11.77: Appliquer immédiatement les configurations virtuelles
        const GPIOMapping* mapping = GPIOMap::findByGPIO(gpio);
        if (mapping && (mapping->type == GPIOType::CONFIG_INT || 
                        mapping->type == GPIOType::CONFIG_FLOAT || 
                        mapping->type == GPIOType::CONFIG_STRING || 
                        mapping->type == GPIOType::CONFIG_BOOL)) {
            
            // Mapper GPIO vers clés attendues par applyConfigFromJson
            String configKey = mapGPIOToConfigKey(gpio, value);
            if (configKey.length() > 0) {
                // v11.79: Sécurisation renforcée de l'assignation JSON
                // Vérifier que le document n'est pas plein avant d'ajouter
                if (configDoc.size() < 15 && configDoc.memoryUsage() < 900) { // Limites de sécurité renforcées
                    // Copie sécurisée en évitant les pointeurs temporaires
                    String keyStr = configKey; // Copie pour éviter les problèmes de pointeur
                    
                    // v11.80: Conversion selon le type attendu du GPIO (pas le type reçu)
                    // Cela permet de gérer les cas où le serveur envoie des strings au lieu de nombres
                    bool assignmentOk = false;
                    
                    // Convertir selon le type attendu du GPIO, pas le type reçu
                    if (mapping->type == GPIOType::CONFIG_INT) {
                        // Pour CONFIG_INT, toujours convertir en int (même si reçu comme string)
                        int intVal = parseInt(value);
                        configDoc[keyStr] = intVal;
                        assignmentOk = true;
                        Serial.printf("[GPIOParser] GPIO %d (INT) -> %s = %d (converti depuis %s)\n", 
                                     gpio, keyStr.c_str(), intVal, value.as<String>().c_str());
                    }
                    else if (mapping->type == GPIOType::CONFIG_FLOAT) {
                        // Pour CONFIG_FLOAT, toujours convertir en float (même si reçu comme string)
                        float floatVal = parseFloat(value);
                        configDoc[keyStr] = floatVal;
                        assignmentOk = true;
                        Serial.printf("[GPIOParser] GPIO %d (FLOAT) -> %s = %.2f (converti depuis %s)\n", 
                                     gpio, keyStr.c_str(), floatVal, value.as<String>().c_str());
                    }
                    else if (mapping->type == GPIOType::CONFIG_BOOL) {
                        // Pour CONFIG_BOOL, utiliser parseBool qui gère les strings
                        bool boolVal = parseBool(value);
                        configDoc[keyStr] = boolVal;
                        assignmentOk = true;
                        Serial.printf("[GPIOParser] GPIO %d (BOOL) -> %s = %s (converti depuis %s)\n", 
                                     gpio, keyStr.c_str(), boolVal ? "true" : "false", value.as<String>().c_str());
                    }
                    else if (mapping->type == GPIOType::CONFIG_STRING) {
                        // Pour CONFIG_STRING, vérifier que c'est bien une string valide
                        if (value.is<const char*>()) {
                            const char* valPtr = value.as<const char*>();
                            if (valPtr != nullptr && strlen(valPtr) < 100) { // Limite de taille
                                configDoc[keyStr] = String(valPtr);
                                assignmentOk = true;
                                Serial.printf("[GPIOParser] GPIO %d (STRING) -> %s = '%s'\n", 
                                             gpio, keyStr.c_str(), valPtr);
                            } else {
                                Serial.printf("[GPIOParser] ⚠️ String invalide GPIO %d, ignoré\n", gpio);
                            }
                        } else {
                            // Si ce n'est pas une string, convertir en string
                            configDoc[keyStr] = value.as<String>();
                            assignmentOk = true;
                            Serial.printf("[GPIOParser] GPIO %d (STRING) -> %s = '%s' (converti)\n", 
                                         gpio, keyStr.c_str(), value.as<String>().c_str());
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

String GPIOParser::mapGPIOToConfigKey(uint8_t gpio, JsonVariantConst value) {
    // Mapper les GPIO virtuels vers les clés attendues par applyConfigFromJson
    switch (gpio) {
        case 100: // EMAIL_ADDR
            return String("mail");
        case 101: // EMAIL_EN  
            return String("mailNotif");
        case 102: // AQ_THRESHOLD
            return String("aqThreshold");
        case 103: // TANK_THRESHOLD
            return String("tankThreshold");
        case 104: // HEAT_THRESHOLD
            return String("chauffageThreshold");
        case 105: // FEED_MORNING
            return String("bouffeMatin");
        case 106: // FEED_NOON
            return String("bouffeMidi");
        case 107: // FEED_EVENING
            return String("bouffeSoir");
        case 111: // FEED_BIG_DUR
            return String("tempsGros");
        case 112: // FEED_SMALL_DUR
            return String("tempsPetits");
        case 113: // REFILL_DUR
            return String("refillDuration");
        case 114: // LIM_FLOOD
            return String("limFlood");
        case 115: // WAKEUP
            return String("forceWakeUp");
        case 116: // FREQ_WAKEUP
            return String("FreqWakeUp");
        default:
            return String();
    }
}

void GPIOParser::saveToNVS(const GPIOMapping& mapping, JsonVariantConst value) {
    // v11.79: Sécurisation de l'accès NVS
    Preferences prefs;
    
    // Vérifier que la clé NVS n'est pas trop longue ou invalide
    if (mapping.nvsKey == nullptr || strlen(mapping.nvsKey) == 0 || strlen(mapping.nvsKey) > 15) {
        Serial.printf("[NVS] ⚠️ Clé NVS invalide, ignorée: %s\n", mapping.nvsKey ? mapping.nvsKey : "NULL");
        return;
    }
    
    bool nvsOpened = prefs.begin("gpio", false);
    if (!nvsOpened) {
        Serial.printf("[NVS] ❌ Impossible d'ouvrir namespace gpio pour %s\n", mapping.nvsKey);
        return;
    }
    
    // v11.79: Gestion sécurisée sans try-catch (ESP32 Arduino)
    switch (mapping.type) {
        case GPIOType::ACTUATOR:
        case GPIOType::CONFIG_BOOL: {
            bool boolVal = parseBool(value);
            bool success = prefs.putBool(mapping.nvsKey, boolVal);
            Serial.printf("[NVS] Sauvegardé bool %s = %s (%s)\n", mapping.nvsKey, boolVal ? "true" : "false", success ? "OK" : "FAIL");
            break;
        }
        case GPIOType::CONFIG_INT: {
            int intVal = parseInt(value);
            bool success = prefs.putInt(mapping.nvsKey, intVal);
            Serial.printf("[NVS] Sauvegardé int %s = %d (%s)\n", mapping.nvsKey, intVal, success ? "OK" : "FAIL");
            break;
        }
        case GPIOType::CONFIG_FLOAT: {
            float floatVal = parseFloat(value);
            bool success = prefs.putFloat(mapping.nvsKey, floatVal);
            Serial.printf("[NVS] Sauvegardé float %s = %.2f (%s)\n", mapping.nvsKey, floatVal, success ? "OK" : "FAIL");
            break;
        }
        case GPIOType::CONFIG_STRING: {
            String stringVal = parseString(value);
            // Vérifier la taille de la string avant sauvegarde
            if (stringVal.length() > 100) {
                Serial.printf("[NVS] ⚠️ String trop longue pour %s (%u chars), tronquée\n", mapping.nvsKey, stringVal.length());
                stringVal = stringVal.substring(0, 100);
            }
            bool success = prefs.putString(mapping.nvsKey, stringVal);
            Serial.printf("[NVS] Sauvegardé string %s = '%s' (%s)\n", mapping.nvsKey, stringVal.c_str(), success ? "OK" : "FAIL");
            break;
        }
        default:
            Serial.printf("[NVS] ⚠️ Type non géré pour %s\n", mapping.nvsKey);
            break;
    }
    
    prefs.end();
}

bool GPIOParser::parseBool(JsonVariantConst v) {
    if (v.is<bool>()) return v.as<bool>();
    if (v.is<int>()) return v.as<int>() == 1;
    if (v.is<const char*>()) {
        String s = String(v.as<const char*>());
        s.toLowerCase();
        s.trim();
        return s == "1" || s == "true" || s == "on" || s == "checked";
    }
    return false;
}

int GPIOParser::parseInt(JsonVariantConst v) {
    if (v.is<int>()) return v.as<int>();
    if (v.is<const char*>()) return atoi(v.as<const char*>());
    return 0;
}

float GPIOParser::parseFloat(JsonVariantConst v) {
    if (v.is<float>()) return v.as<float>();
    if (v.is<const char*>()) return atof(v.as<const char*>());
    return 0.0f;
}

String GPIOParser::parseString(JsonVariantConst v) {
    return v.as<String>();
}

void GPIOParser::loadGPIOStatesFromNVS(Automatism& autoCtrl) {
    Serial.println(F("[GPIOParser] Chargement des états GPIO depuis NVS..."));
    Preferences prefs;
    
    bool nvsOpened = prefs.begin("gpio", true);
    if (!nvsOpened) {
        Serial.println(F("[GPIOParser] ⚠️ Impossible d'ouvrir namespace gpio"));
        return;
    }
    
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
        
        bool state = prefs.getBool(mapping.nvsKey, false);
        
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
    
    prefs.end();
    Serial.println(F("[GPIOParser] ✅ États GPIO chargés depuis NVS"));
}
