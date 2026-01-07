#include <Arduino.h>
#include "power.h"
#include "config.h"

// Test du système Modem Sleep + Light Sleep
void testModemSleepSystem() {
  Serial.println("\n🧪 === TEST SYSTÈME MODEM SLEEP + LIGHT SLEEP ===");
  
  PowerManager powerManager;
  powerManager.initModemSleep();
  
  // Test 1: Vérification de l'état WiFi
  Serial.println("\n1️⃣ Test état WiFi:");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("✅ WiFi connecté: %s (IP: %s, RSSI: %d dBm)\n", 
                  WiFi.SSID().c_str(), 
                  WiFi.localIP().toString().c_str(), 
                  WiFi.RSSI());
  } else {
    Serial.println("❌ WiFi non connecté - Test impossible");
    return;
  }
  
  // Test 2: Compatibilité DTIM
  Serial.println("\n2️⃣ Test compatibilité DTIM:");
  bool dtimOk = powerManager.testDTIMCompatibility();
  if (dtimOk) {
    Serial.println("✅ Compatible avec modem sleep");
  } else {
    Serial.println("❌ Incompatible avec modem sleep");
  }
  
  // Test 3: Disponibilité du réveil WiFi
  Serial.println("\n3️⃣ Test disponibilité réveil WiFi:");
  bool wakeupAvailable = powerManager.isWifiWakeupAvailable();
  if (wakeupAvailable) {
    Serial.println("✅ Réveil WiFi disponible");
  } else {
    Serial.println("❌ Réveil WiFi non disponible");
  }
  
  // Test 4: Sleep court avec modem sleep
  if (dtimOk && wakeupAvailable) {
    Serial.println("\n4️⃣ Test sleep court (10 secondes) avec modem sleep:");
    Serial.println("💤 Entrée en modem sleep + light sleep...");
    
    uint32_t sleptTime = powerManager.goToModemSleepWithLightSleep(10);
    
    Serial.printf("✅ Réveil après %u secondes\n", sleptTime);
    Serial.printf("📊 WiFi toujours connecté: %s\n", 
                  (WiFi.status() == WL_CONNECTED) ? "Oui" : "Non");
  }
  
  Serial.println("\n🎉 === FIN DES TESTS ===");
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("🚀 Test Modem Sleep + Light Sleep ESP32");
  
  // Attendre la connexion WiFi
  Serial.println("⏳ Attente connexion WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\n✅ WiFi connecté !");
  
  testModemSleepSystem();
}

void loop() {
  // Test périodique toutes les 5 minutes
  static unsigned long lastTest = 0;
  unsigned long now = millis();
  
  if (now - lastTest > 300000) { // 5 minutes
    lastTest = now;
    Serial.println("\n🔄 Test périodique du système...");
    testModemSleepSystem();
  }
  
  delay(10000); // 10 secondes
}
