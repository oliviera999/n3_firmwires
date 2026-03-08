/**
 * Ratata — 5_Tracking (Kit ZYC0108-EN)
 * Suivi de ligne. Capteurs A0, A1, A2.
 * Carte : Arduino UNO.
 */

#include <Arduino.h>

void setup() {
    Serial.begin(9600);
    Serial.println(F("[5_Tracking] Ratata ZYC0108-EN — placeholder"));
}

void loop() {
    // TODO : lecture capteurs ligne, correction trajectoire
    delay(50);
}
