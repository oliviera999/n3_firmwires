#include "gpio_notifier.h"
#include "web_client.h"
#include "config.h"
#include "secrets.h"

bool GPIONotifier::notifyChange(uint8_t gpio, const char* value) {
    // Vérifier mapping existe
    const GPIOMapping* mapping = GPIOMap::findByGPIO(gpio);
    if (!mapping) {
        Serial.printf("[GPIONotifier] ⚠️ GPIO %d inconnu\n", gpio);
        return false;
    }
    
    if (value == nullptr) {
        value = "";
    }
    
    // Construire payload partiel
    char payload[256];
    snprintf(payload, sizeof(payload), "api_key=%s&sensor=esp32-wroom&version=%s&%d=%s",
             ApiConfig::API_KEY, ProjectConfig::VERSION, gpio, value);
    
    Serial.printf("[GPIONotifier] POST instantané: GPIO %d = %s\n", gpio, value);
    
    // Envoi POST via WebClient public method
    WebClient client;
    bool success = client.postRaw(payload);
    
    if (success) {
        Serial.printf("[GPIONotifier] ✓ Changement envoyé: %s\n", mapping->description);
    } else {
        Serial.printf("[GPIONotifier] ❌ Échec envoi GPIO %d\n", gpio);
    }
    
    return success;
}

bool GPIONotifier::notifyActuator(uint8_t gpio, bool state) {
    return notifyChange(gpio, state ? "1" : "0");
}

bool GPIONotifier::notifyConfig(uint8_t gpio, int value) {
    char valueStr[16];
    snprintf(valueStr, sizeof(valueStr), "%d", value);
    return notifyChange(gpio, valueStr);
}

bool GPIONotifier::notifyConfig(uint8_t gpio, float value) {
    char valueStr[16];
    snprintf(valueStr, sizeof(valueStr), "%.2f", value);
    return notifyChange(gpio, valueStr);
}

bool GPIONotifier::notifyConfig(uint8_t gpio, const char* value) {
    return notifyChange(gpio, value);
}
