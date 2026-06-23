#include "system_actuators.h"
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>

namespace {
TimerHandle_t g_smallFeedTimer = nullptr;
SystemActuators* g_smallFeedInstance = nullptr;
uint16_t g_smallFeedDurationSec = 0;

void smallFeedTimerCallback(TimerHandle_t timer) {
  (void)timer;
  if (!g_smallFeedInstance) {
    return;
  }

  g_smallFeedInstance->feedSmallFish(g_smallFeedDurationSec);

  g_smallFeedInstance = nullptr;
  g_smallFeedDurationSec = 0;
}
}  // namespace

void SystemActuators::begin() {
  applyServoConfigToFeeders();
  feederBig.begin();
  feederSmall.begin();
  // Pompe aquarium ON par défaut
  pumpAqua.on();
  Serial.println("[Event] Actuators initialized; Aqua pump set ON by default");
}

// Méthodes pour la gestion des pompes avec logs détaillés
void SystemActuators::startTankPump() { 
  pumpTank.on(); 
  tankPumpStartTime = millis();
  LOG(LOG_INFO, "[POMPE_RESERV] DÉMARRAGE - Timestamp: %u ms", (unsigned)tankPumpStartTime);
  Serial.printf("[POMPE_RESERV] DÉMARRAGE - Timestamp: %u ms\n", (unsigned)tankPumpStartTime);
  Serial.printf("[Event] Tank pump START; ts=%u\n", (unsigned)tankPumpStartTime);
}

void SystemActuators::stopTankPump(uint32_t startTime) { 
  // Vérification de l'état réel avant arrêt
  bool wasRunning = pumpTank.state();
  
  pumpTank.off(); 
  
  uint32_t runtimeStart = startTime > 0 ? startTime : tankPumpStartTime;
  if (wasRunning && runtimeStart > 0) {
    uint32_t runtime = (uint32_t)(millis() - runtimeStart);
    tankPumpTotalRuntime += runtime;
    tankPumpTotalStops++; // Incrémenter le compteur d'arrêts effectifs
    tankPumpLastStopTime = millis();
    LOG(LOG_INFO, "[POMPE_RESERV] ARRÊT - Durée: %u ms, Total runtime: %u ms, Arrêts: %u", 
        (unsigned)runtime, (unsigned)tankPumpTotalRuntime, (unsigned)tankPumpTotalStops);
    Serial.printf("[POMPE_RESERV] ARRÊT - Durée: %u ms, Total runtime: %u ms, Arrêts: %u\n", 
                 (unsigned)runtime, (unsigned)tankPumpTotalRuntime, (unsigned)tankPumpTotalStops);
    Serial.printf("[Event] Tank pump STOP; run=%u ms, totalRun=%u ms, stops=%u\n",
                   (unsigned)runtime,
                   (unsigned)tankPumpTotalRuntime,
                   (unsigned)tankPumpTotalStops);
  } else if (wasRunning) {
    // Cas où la pompe était en marche mais sans timestamp (commande distante de synchronisation)
    LOG(LOG_INFO, "[POMPE_RESERV] ARRÊT - Synchronisation distante (pompe était active)");
    Serial.println(F("[POMPE_RESERV] ARRÊT - Synchronisation distante (pompe était active)"));
    Serial.println("[Event] Tank pump STOP; remote sync (was active)");
  } else {
    // Cas où la pompe n'était pas en marche et pas de timestamp (commande redondante)
    LOG(LOG_DEBUG, "[POMPE_RESERV] ARRÊT - Commande redondante (pompe déjà arrêtée)");
    Serial.println(F("[POMPE_RESERV] ARRÊT - Commande redondante (pompe déjà arrêtée)"));
    // Pas d'ajout dans EventLog pour éviter le spam
  }
  tankPumpStartTime = 0;
}

void SystemActuators::startAquaPump() { pumpAqua.on(); LOG(LOG_INFO, "[Event] Aqua pump ON"); }
void SystemActuators::stopAquaPump()  { pumpAqua.off(); LOG(LOG_INFO, "[Event] Aqua pump OFF"); }

// Méthodes pour le chauffage
void SystemActuators::startHeater() { heater.on(); LOG(LOG_INFO, "[Event] Heater ON"); }
void SystemActuators::stopHeater()  { heater.off(); LOG(LOG_INFO, "[Event] Heater OFF"); }

// Méthodes pour la lumière
void SystemActuators::startLight() { light.on(); LOG(LOG_INFO, "[Event] Light ON"); }
void SystemActuators::stopLight()  { light.off(); LOG(LOG_INFO, "[Event] Light OFF"); }

// Méthodes pour la nourriture
void SystemActuators::applyServoConfigToFeeders() {
  feederBig.setRestAngle(_servoGrosRest);
  feederSmall.setRestAngle(_servoPetitsRest);
}

void SystemActuators::setServoAngles(bool big, int rest, int feed, int inter) {
  rest = clampServoAngle(rest);
  feed = clampServoAngle(feed);
  inter = clampServoAngle(inter);
  if (big) {
    _servoGrosRest = rest;
    _servoGrosFeed = feed;
    _servoGrosInter = inter;
    feederBig.setRestAngle(_servoGrosRest);
  } else {
    _servoPetitsRest = rest;
    _servoPetitsFeed = feed;
    _servoPetitsInter = inter;
    feederSmall.setRestAngle(_servoPetitsRest);
  }
}

void SystemActuators::setServoGrosRest(int angle) {
  _servoGrosRest = clampServoAngle(angle);
  feederBig.setRestAngle(_servoGrosRest);
}
void SystemActuators::setServoGrosFeed(int angle) { _servoGrosFeed = clampServoAngle(angle); }
void SystemActuators::setServoGrosInter(int angle) { _servoGrosInter = clampServoAngle(angle); }
void SystemActuators::setServoPetitsRest(int angle) {
  _servoPetitsRest = clampServoAngle(angle);
  feederSmall.setRestAngle(_servoPetitsRest);
}
void SystemActuators::setServoPetitsFeed(int angle) { _servoPetitsFeed = clampServoAngle(angle); }
void SystemActuators::setServoPetitsInter(int angle) { _servoPetitsInter = clampServoAngle(angle); }

void SystemActuators::feedBigFish(uint16_t durationSec) {
  LOG(LOG_INFO, "[Event] Feed big fish %u s (angles %d/%d/%d)", durationSec,
      _servoGrosRest, _servoGrosFeed, _servoGrosInter);
  feederBig.dispenseWithIntermediate(_servoGrosFeed, _servoGrosInter, durationSec);
}
void SystemActuators::feedSmallFish(uint16_t durationSec) {
  LOG(LOG_INFO, "[Event] Feed small fish %u s (angles %d/%d/%d)", durationSec,
      _servoPetitsRest, _servoPetitsFeed, _servoPetitsInter);
  feederSmall.dispenseWithIntermediate(_servoPetitsFeed, _servoPetitsInter, durationSec);
}

// Nourrissage séquentiel pour éviter les conflits de puissance
bool SystemActuators::feedSequential(uint16_t bigDurationSec, uint16_t smallDurationSec, uint16_t delayBetweenSec) {
  // v11.179: Protection contre les appels multiples (race condition)
  if (g_smallFeedInstance != nullptr) {
    LOG(LOG_WARN, "Nourrissage séquentiel déjà en cours, ignoré");
    Serial.println("[Event] Sequential feed already in progress, ignored");
    return false;
  }
  
  LOG(LOG_INFO, "=== NOURRISSAGE SÉQUENTIEL ===");
  LOG(LOG_INFO, "Gros poissons: %u s, Petits poissons: %u s, Délai: %u s", bigDurationSec, smallDurationSec, delayBetweenSec);
  Serial.printf("[Event] Sequential feed: big=%u s, small=%u s, delay=%u s\n", bigDurationSec, smallDurationSec, delayBetweenSec);

  LOG(LOG_INFO, "Phase 1: Nourrissage gros poissons");
  Serial.println("[Event] Feed phase 1: big fish");
  feederBig.dispenseWithIntermediate(_servoGrosFeed, _servoGrosInter, bigDurationSec);

  const uint32_t totalBigTimeMs = static_cast<uint32_t>(bigDurationSec + (bigDurationSec / 2U)) * 1000UL;
  const uint32_t delayMs = static_cast<uint32_t>(delayBetweenSec) * 1000UL;
  const uint32_t scheduleMs = totalBigTimeMs + delayMs;

  LOG(LOG_INFO, "Planification phase 2 dans %lu ms (cycle: %lu ms + délai: %lu ms)", scheduleMs, totalBigTimeMs, delayMs);
  Serial.printf("[Event] Feed phase 2 scheduled in %lu ms\n", scheduleMs);

  g_smallFeedInstance = this;
  g_smallFeedDurationSec = smallDurationSec;

  const TickType_t scheduleTicks = pdMS_TO_TICKS(scheduleMs);
  if (!g_smallFeedTimer) {
    g_smallFeedTimer = xTimerCreate("SEQ_FEED_SMALL",
                                    scheduleTicks,
                                    pdFALSE,
                                    nullptr,
                                    smallFeedTimerCallback);
    if (!g_smallFeedTimer) {
      LOG(LOG_ERROR, "Impossible de planifier le nourrissage des petits poissons (timer FreeRTOS)");
      Serial.println("[Event] Feed phase 2 scheduling failed");
      g_smallFeedInstance = nullptr;
      g_smallFeedDurationSec = 0;
      // Gros poissons déjà nourris : true pour ne pas re-déclencher la séquence
      return true;
    }
  } else {
    xTimerStop(g_smallFeedTimer, 0);
    xTimerChangePeriod(g_smallFeedTimer, scheduleTicks, 0);
  }

  if (xTimerStart(g_smallFeedTimer, 0) != pdPASS) {
    LOG(LOG_ERROR, "Échec démarrage timer nourrissage petits poissons");
    LOG(LOG_WARN, "Feed phase 2 scheduling failed");
    g_smallFeedInstance = nullptr;
    g_smallFeedDurationSec = 0;
    // Gros poissons déjà nourris : true pour ne pas re-déclencher la séquence
    return true;
  }

  LOG(LOG_INFO, "=== FIN PLANIFICATION NOURRISSAGE SÉQUENTIEL ===");
  Serial.println("[Event] Sequential feed scheduled");
  return true;
}

// true tant que le timer des petits poissons est armé (séquence gros->petits en cours).
bool SystemActuators::isSequentialFeedInProgress() const { return g_smallFeedInstance != nullptr; }

// Getters pour l'état
bool SystemActuators::isTankPumpRunning() const { return pumpTank.state(); }
bool SystemActuators::isAquaPumpRunning() const { return pumpAqua.state(); }
bool SystemActuators::isHeaterOn() const { return heater.state(); }
bool SystemActuators::isLightOn() const { return light.state(); }

// Getters pour les statistiques de la pompe réservoir
unsigned long SystemActuators::getTankPumpCurrentRuntime() const {
  if (!pumpTank.state() || tankPumpStartTime == 0) {
    return 0;
  }
  return millis() - tankPumpStartTime;
}

unsigned long SystemActuators::getTankPumpTotalRuntime() const { return tankPumpTotalRuntime; }
unsigned long SystemActuators::getTankPumpTotalStops() const { return tankPumpTotalStops; }
unsigned long SystemActuators::getTankPumpLastStopTime() const { return tankPumpLastStopTime; }
