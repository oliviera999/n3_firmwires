/**
 * Ratata — 3_Ultrasonic_follow (Kit ZYC0108-EN)
 * Suivi d'obstacle avec capteur ultrason. Broches 12 (Trig), 13 (Echo).
 * Carte : Arduino UNO.
 */

#include <Arduino.h>

void setup() {
    Serial.begin(9600);
    Serial.println(F("[3_Ultrasonic_follow] Ratata ZYC0108-EN — placeholder"));
}

void loop() {
    // TODO : lecture HC-SR04, suivi obstacle
    delay(100);
}
