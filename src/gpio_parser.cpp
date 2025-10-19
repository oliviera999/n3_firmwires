#include "gpio_parser.h"
#include "automatism.h"
#include <Preferences.h>

void GPIOParser::parseAndApply(const JsonDocument& doc, Automatism& autoCtrl) {
    Serial.println(F("[GPIOParser] === PARSING JSON SERVEUR ==="));
  // Diagnostics additionnels v11.74
  size_t presentKeys = 0;
    
    // v11.77: Collecter les GPIO virtuels pour application immédiate
    StaticJsonDocument<512> configDoc;
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
    
    // v11.77: Appliquer immédiatement les configurations virtuelles
    if (hasVirtualConfig) {
        Serial.println(F("[GPIOParser] 🔧 Application immédiate des GPIO virtuels"));
        autoCtrl.applyConfigFromJson(configDoc);
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
        if (parseBool(value)) {
            autoCtrl.manualFeedSmall();
            Serial.println("Nourrissage petits");
        }
    }
    else if (gpio == GPIOMap::FEED_BIG.gpio) {
        if (parseBool(value)) {
            autoCtrl.manualFeedBig();
            Serial.println("Nourrissage gros");
        }
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
                configDoc[configKey.c_str()] = value;
                hasVirtualConfig = true;
                Serial.printf("[GPIOParser] GPIO virtuel %d -> %s = %s\n", gpio, configKey.c_str(), value.as<String>().c_str());
            }
        }
    }
}

String GPIOParser::mapGPIOToConfigKey(uint8_t gpio, JsonVariantConst value) {
    // Mapper les GPIO virtuels vers les clés attendues par applyConfigFromJson
    switch (gpio) {
        case 100: // EMAIL_ADDR
            return String("emailAddress");
        case 101: // EMAIL_EN  
            return String("emailEnabled");
        case 102: // AQ_THRESHOLD
            return String("aqThreshold");
        case 103: // TANK_THRESHOLD
            return String("tankThreshold");
        case 104: // HEAT_THRESHOLD
            return String("heaterThreshold");
        case 105: // FEED_MORNING
            return String("feedMorning");
        case 106: // FEED_NOON
            return String("feedNoon");
        case 107: // FEED_EVENING
            return String("feedEvening");
        case 111: // FEED_BIG_DUR
            return String("feedBigDur");
        case 112: // FEED_SMALL_DUR
            return String("feedSmallDur");
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
    Preferences prefs;
    prefs.begin("gpio", false);
    
    switch (mapping.type) {
        case GPIOType::ACTUATOR:
        case GPIOType::CONFIG_BOOL:
            prefs.putBool(mapping.nvsKey, parseBool(value));
            break;
        case GPIOType::CONFIG_INT:
            prefs.putInt(mapping.nvsKey, parseInt(value));
            break;
        case GPIOType::CONFIG_FLOAT:
            prefs.putFloat(mapping.nvsKey, parseFloat(value));
            break;
        case GPIOType::CONFIG_STRING:
            prefs.putString(mapping.nvsKey, parseString(value));
            break;
    }
    
    prefs.end();
    Serial.printf("[NVS] Sauvegardé: %s\n", mapping.nvsKey);
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
