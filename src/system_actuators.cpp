#include "system_actuators.h"
#include "event_log.h"

void SystemActuators::begin() {
  feederBig.begin();
  feederSmall.begin();
  // Pompe aquarium ON par défaut
  pumpAqua.on();
  EventLog::add("Actuators initialized; Aqua pump set ON by default");
}

// Méthodes pour la gestion des pompes avec logs détaillés
void SystemActuators::startTankPump() { 
  pumpTank.on(); 
  tankPumpTotalStops++;
  uint32_t startTime = millis();
  LOG(LOG_INFO, "[POMPE_RESERV] DÉMARRAGE - Timestamp: %u ms", (unsigned)startTime);
  Serial.printf("[POMPE_RESERV] DÉMARRAGE - Timestamp: %u ms\n", (unsigned)startTime);
  EventLog::addf("Tank pump START; ts=%u", (unsigned)startTime);
}

void SystemActuators::stopTankPump(uint32_t startTime) { 
  // Vérification de l'état réel avant arrêt
  bool wasRunning = pumpTank.state();
  
  pumpTank.off(); 
  
  if (startTime > 0) {
    uint32_t runtime = (uint32_t)(millis() - startTime);
    tankPumpTotalRuntime += runtime;
    tankPumpTotalStops++; // Incrémenter le compteur d'arrêts
    tankPumpLastStopTime = millis();
    LOG(LOG_INFO, "[POMPE_RESERV] ARRÊT - Durée: %u ms, Total runtime: %u ms, Arrêts: %u", 
        (unsigned)runtime, (unsigned)tankPumpTotalRuntime, (unsigned)tankPumpTotalStops);
    Serial.printf("[POMPE_RESERV] ARRÊT - Durée: %u ms, Total runtime: %u ms, Arrêts: %u\n", 
                 (unsigned)runtime, (unsigned)tankPumpTotalRuntime, (unsigned)tankPumpTotalStops);
    EventLog::addf("Tank pump STOP; run=%u ms, totalRun=%u ms, stops=%u",
                   (unsigned)runtime,
                   (unsigned)tankPumpTotalRuntime,
                   (unsigned)tankPumpTotalStops);
  } else if (wasRunning) {
    // Cas où la pompe était en marche mais sans timestamp (commande distante de synchronisation)
    LOG(LOG_INFO, "[POMPE_RESERV] ARRÊT - Synchronisation distante (pompe était active)");
    Serial.println(F("[POMPE_RESERV] ARRÊT - Synchronisation distante (pompe était active)"));
    EventLog::add("Tank pump STOP; remote sync (was active)");
  } else {
    // Cas où la pompe n'était pas en marche et pas de timestamp (commande redondante)
    LOG(LOG_DEBUG, "[POMPE_RESERV] ARRÊT - Commande redondante (pompe déjà arrêtée)");
    Serial.println(F("[POMPE_RESERV] ARRÊT - Commande redondante (pompe déjà arrêtée)"));
    // Pas d'ajout dans EventLog pour éviter le spam
  }
}

void SystemActuators::startAquaPump() { pumpAqua.on(); LOG(LOG_INFO, "Aqua pump ON"); EventLog::add("Aqua pump ON"); }
void SystemActuators::stopAquaPump()  { pumpAqua.off(); LOG(LOG_INFO, "Aqua pump OFF"); EventLog::add("Aqua pump OFF"); }

// Méthodes pour le chauffage
void SystemActuators::startHeater() { heater.on(); LOG(LOG_INFO, "Heater ON"); EventLog::add("Heater ON"); }
void SystemActuators::stopHeater()  { heater.off(); LOG(LOG_INFO, "Heater OFF"); EventLog::add("Heater OFF"); }

// Méthodes pour la lumière
void SystemActuators::startLight() { light.on(); LOG(LOG_INFO, "Light ON"); EventLog::add("Light ON"); }
void SystemActuators::stopLight()  { light.off(); LOG(LOG_INFO, "Light OFF"); EventLog::add("Light OFF"); }

// Méthodes pour la nourriture
// durationSec : durée pendant laquelle le servo reste dans la position de distribution.
// Valeur par défaut : 10 s pour conserver la compatibilité avec les appels existants.
void SystemActuators::feedBigFish(uint16_t durationSec)   { LOG(LOG_INFO, "Feed big fish (%u s)", durationSec); EventLog::addf("Feed big fish %u s", durationSec); feederBig.dispenseWithIntermediate(140, 45, durationSec); }
void SystemActuators::feedSmallFish(uint16_t durationSec) { LOG(LOG_INFO, "Feed small fish (%u s)", durationSec); EventLog::addf("Feed small fish %u s", durationSec); feederSmall.dispenseWithIntermediate(140, 45, durationSec); }

// Nourrissage séquentiel pour éviter les conflits de puissance
void SystemActuators::feedSequential(uint16_t bigDurationSec, uint16_t smallDurationSec, uint16_t delayBetweenSec) {
  LOG(LOG_INFO, "=== NOURRISSAGE SÉQUENTIEL ===");
  LOG(LOG_INFO, "Gros poissons: %u s, Petits poissons: %u s, Délai: %u s", bigDurationSec, smallDurationSec, delayBetweenSec);
  EventLog::addf("Sequential feed: big=%u s, small=%u s, delay=%u s", bigDurationSec, smallDurationSec, delayBetweenSec);
  
  // 1. Nourrissage des gros poissons d'abord
  LOG(LOG_INFO, "Phase 1: Nourrissage gros poissons");
  EventLog::add("Feed phase 1: big fish");
  feederBig.dispenseWithIntermediate(140, 45, bigDurationSec);
  
  // 2. Attendre la fin du cycle + délai de sécurité
  unsigned long totalBigTime = (bigDurationSec + bigDurationSec/2) * 1000UL; // Temps total du cycle
  unsigned long delayMs = delayBetweenSec * 1000UL;
  unsigned long totalWait = totalBigTime + delayMs;
  
  LOG(LOG_INFO, "Attente: %lu ms (cycle: %lu ms + délai: %lu ms)", totalWait, totalBigTime, delayMs);
  delay(totalWait);
  
  // 3. Nourrissage des petits poissons
  LOG(LOG_INFO, "Phase 2: Nourrissage petits poissons");
  EventLog::add("Feed phase 2: small fish");
  feederSmall.dispenseWithIntermediate(140, 45, smallDurationSec);
  
  LOG(LOG_INFO, "=== FIN NOURRISSAGE SÉQUENTIEL ===");
  EventLog::add("Sequential feed done");
}

// Getters pour l'état
bool SystemActuators::isTankPumpRunning() const { return pumpTank.state(); }
bool SystemActuators::isAquaPumpRunning() const { return pumpAqua.state(); }
bool SystemActuators::isHeaterOn() const { return heater.state(); }
bool SystemActuators::isLightOn() const { return light.state(); }

// Getters pour les statistiques de la pompe réservoir
unsigned long SystemActuators::getTankPumpCurrentRuntime() const {
  // Cette méthode nécessite maintenant un timestamp externe
  // Elle sera appelée depuis Automatism avec _pumpStartMs
  return 0; // Retourne 0 car on n'a plus accès au timestamp local
}

unsigned long SystemActuators::getTankPumpTotalRuntime() const { return tankPumpTotalRuntime; }
unsigned long SystemActuators::getTankPumpTotalStops() const { return tankPumpTotalStops; }
unsigned long SystemActuators::getTankPumpLastStopTime() const { return tankPumpLastStopTime; }
