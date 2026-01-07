#include "gpio_notifier.h"
#include "web_client.h"
#include "config.h"
#include "secrets.h"

bool GPIONotifier::notifyChange(uint8_t gpio, const String& value) {
    // Vérifier mapping existe
    const GPIOMapping* mapping = GPIOMap::findByGPIO(gpio);
    if (!mapping) {
        Serial.printf("[GPIONotifier] ⚠️ GPIO %d inconnu\n", gpio);
        return false;
    }
    
    // Construire payload partiel
    String payload;
    payload.reserve(128);
    payload = "api_key=" + String(ApiConfig::API_KEY);
    payload += "&sensor=esp32-wroom";
    payload += "&version=" + String(ProjectConfig::VERSION);
    payload += "&" + String(gpio) + "=" + value;
    
    Serial.printf("[GPIONotifier] POST instantané: GPIO %d = %s\n", gpio, value.c_str());
    
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
    return notifyChange(gpio, String(value));
}

bool GPIONotifier::notifyConfig(uint8_t gpio, float value) {
    return notifyChange(gpio, String(value, 2));
}

bool GPIONotifier::notifyConfig(uint8_t gpio, const String& value) {
    return notifyChange(gpio, value);
}
