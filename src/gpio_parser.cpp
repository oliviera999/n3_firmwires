#include "gpio_parser.h"
#include "automatism.h"
#include <Preferences.h>

void GPIOParser::parseAndApply(const JsonDocument& doc, Automatism& autoCtrl) {
    Serial.println(F("[GPIOParser] === PARSING JSON SERVEUR ==="));
    
    // Parcourir tous les GPIO définis dans le mapping
    for (size_t i = 0; i < GPIOMap::MAPPING_COUNT; i++) {
        const GPIOMapping& mapping = GPIOMap::ALL_MAPPINGS[i];
        String key = String(mapping.gpio);
        
        // Vérifier si la clé existe dans le document
        if (!doc.containsKey(key.c_str())) {
            continue; // Ce GPIO n'est pas dans la réponse du serveur
        }
        
        JsonVariantConst value = doc[key.c_str()];
        Serial.printf("[GPIOParser] GPIO %d (%s): ", mapping.gpio, mapping.description);
        
        // Appliquer selon type
        applyGPIO(mapping.gpio, value, autoCtrl);
        
        // Sauvegarder NVS
        saveToNVS(mapping, value);
    }
    
    Serial.println(F("[GPIOParser] === FIN PARSING ==="));
}

void GPIOParser::applyGPIO(uint8_t gpio, JsonVariantConst value, Automatism& autoCtrl) {
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
        // Les configurations sont directement sauvées en NVS et appliquées au prochain démarrage
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
