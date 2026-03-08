/**
 * Ratata — 6.2_Arduino_UNO (Kit ZYC0108-EN)
 * Variante « deux cartes » : UNO pour les moteurs (commandes reçues par série).
 * Carte : Arduino UNO.
 */

#include <Arduino.h>

void setup() {
    Serial.begin(9600);
    Serial.println(F("[6.2_Arduino_UNO] Ratata ZYC0108-EN — placeholder"));
}

void loop() {
    // TODO : écoute série, commandes moteurs (cf. ANM-P4F ESP32CamRobotMotors)
    if (Serial.available()) {
        Serial.read();
    }
    delay(10);
}
