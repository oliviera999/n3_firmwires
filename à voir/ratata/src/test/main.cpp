/**
 * Ratata — test (Kit ZYC0108-EN)
 * Exemple de test / debug.
 * Carte : Arduino UNO.
 */

#include <Arduino.h>

void setup() {
    Serial.begin(9600);
    Serial.println(F("[test] Ratata ZYC0108-EN — placeholder"));
}

void loop() {
    Serial.println(millis());
    delay(1000);
}
