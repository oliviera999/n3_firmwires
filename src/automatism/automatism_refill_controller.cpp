#include "automatism/automatism_refill_controller.h"

#include "automatism.h"
#include "automatism/automatism_utils.h"
#include <WiFi.h>
#include "esp_task_wdt.h"

using namespace AutomatismUtils;

AutomatismRefillController::AutomatismRefillController(Automatism& core)
    : _core(core) {}

void AutomatismRefillController::process(const AutomatismRuntimeContext& ctx) {
  uint32_t startMs = millis();
  _core.handleRefillInternal(ctx);
  uint32_t duration = millis() - startMs;
  if (duration > 75) {
    Serial.printf("[RefillCtrl] ⚠️ Traitement long: %u ms\n", static_cast<unsigned>(duration));
  }
  esp_task_wdt_reset();
}

void Automatism::handleRefillInternal(const AutomatismRuntimeContext& ctx) {
  const SensorReadings& r = ctx.readings;
  unsigned long currentMillis = ctx.nowMs;

  // ========================================
  // FONCTION CRITIQUE : REMPLISSAGE AUTOMATIQUE
  // PRIORITÉ ABSOLUE - TEMPS D'ACTIVATION CRITIQUES
  // ========================================

  unsigned long nowControllerMs = currentMillis == 0 ? millis() : currentMillis;

  // ========================================
  // 0. SÉCURITÉ AQUARIUM TROP PLEIN
  // Empêche le remplissage si l'aquarium est déjà trop plein (distance < limFlood)
  // ========================================
  if (r.wlAqua < limFlood) {
    if (!tankPumpLocked || _tankPumpLockReason != TankPumpLockReason::AQUARIUM_OVERFILL) {
      tankPumpLocked = true;
      _tankPumpLockReason = TankPumpLockReason::AQUARIUM_OVERFILL;
      if (_acts.isTankPumpRunning()) {
        _acts.stopTankPump(_pumpStartMs);
        _lastTankStopReason = TankPumpStopReason::OVERFLOW_SECURITY; // réutilisé pour motif sécurité
        _countdownEnd = 0;
        // Notifier immédiatement le serveur de l'arrêt
        if (WiFi.status() == WL_CONNECTED && _config.isRemoteSendEnabled()) {
          SensorReadings cur = _sensors.read();
          sendFullUpdate(cur, "etatPompeTank=0&pump_tank=0&pump_tankCmd=0");
          Serial.println(F("[Auto] ✅ Arrêt sécurité notifié au serveur - pump_tank=0"));
        }
      }
      Serial.println(F("[CRITIQUE] Sécurité: aquarium trop plein – pompe réservoir verrouillée"));
    }
  } else {
      // Déverrouillage automatique si l'aquarium n'est plus en état de trop-plein
      if (tankPumpLocked && _tankPumpLockReason == TankPumpLockReason::AQUARIUM_OVERFILL && !inFlood) {
        tankPumpLocked = false;
        _tankPumpLockReason = TankPumpLockReason::NONE;
        emailTankSent = false;
        emailTankStartSent = false; // Réinitialiser pour permettre un nouveau mail de démarrage
        emailTankStopSent = false;   // Réinitialiser pour permettre un nouveau mail d'arrêt
        Serial.println(F("[Auto] Pompe réservoir déverrouillée (aquarium OK)"));
      }
  }

  // ========================================
  // 1. CONDITION DE DÉMARRAGE - PRIORITÉ ABSOLUE
  // ========================================
  // CORRECTION v11.10: Vérification supplémentaire pour éviter les cycles infinis
  // Si la pompe n'est pas en cours et qu'on est en mode manuel, réinitialiser
  if (!_acts.isTankPumpRunning() && _manualTankOverride) {
    Serial.println(F("[CRITIQUE] Mode manuel détecté sans pompe active - réinitialisation"));
    _manualTankOverride = false;
    _countdownEnd = 0;
    _pumpStartMs = 0;
  }

  if (r.wlAqua > aqThresholdCm && !tankPumpLocked && tankPumpRetries < MAX_PUMP_RETRIES && !_manualTankOverride) {
    // Correction: l'état de verrouillage de la pompe aquarium ne doit pas
    // bloquer le démarrage de la pompe réservoir.
    if (!_acts.isTankPumpRunning()) {
      // Sécurité pré-démarrage: ne pas démarrer si la réserve est trop basse
      if (r.wlTank > tankThresholdCm) {
        Serial.printf("[CRITIQUE] Démarrage bloqué: réserve trop basse (distance=%u cm > seuil=%u cm)\n", r.wlTank, tankThresholdCm);
        // Verrouille immédiatement pour éviter des tentatives répétées inutiles
        tankPumpLocked = true;
        _tankPumpLockReason = TankPumpLockReason::RESERVOIR_LOW;
        _lastTankStopReason = TankPumpStopReason::OVERFLOW_SECURITY;
        _countdownEnd = 0;
        if (mailNotif && !emailTankSent) {
          char msg[512];
          snprintf(msg, sizeof(msg),
                   "Démarrage REMPLISSAGE bloqué (réserve trop basse)\n"
                   "- Réserve (distance): %d cm (seuil: %d cm)\n"
                   "- Aqua: %d cm (seuil: %d cm)\n"
                   "Déblocage: lorsque la distance réservoir < %d cm (confirmée).",
                   r.wlTank, tankThresholdCm, r.wlAqua, aqThresholdCm, (int)tankThresholdCm - 5);
          _mailer.sendAlert("Pompe réservoir BLOQUÉE (réserve basse)", msg, _emailAddress);
          emailTankSent = true;
        }
        return; // ne pas démarrer
      }
      Serial.println(F("[CRITIQUE] === DÉBUT REMPLISSAGE AUTOMATIQUE ==="));
      Serial.printf("[CRITIQUE] Niveau aquarium: %d cm, Seuil: %d cm\n", r.wlAqua, aqThresholdCm);
      Serial.printf("[CRITIQUE] Durée configurée: %lu s\n", refillDurationMs / 1000);

      // DÉMARRAGE IMMÉDIAT - PRIORITÉ ABSOLUE
      uint32_t startTime = millis();

      _acts.startTankPump();
      _countdownLabel = "Refill";
      _countdownEnd = millis() + refillDurationMs;
      _pumpStartMs = millis(); // wrap-safe start time in ms
      _levelAtPumpStart = r.wlAqua;
      logActivity("Démarrage pompe réservoir automatique");

      // Envoi immédiat des données vers le serveur distant pour enregistrer l'activation de la pompe
      if (WiFi.status() == WL_CONNECTED && _config.isRemoteSendEnabled()) {
        SensorReadings currentReadings = _sensors.read();
        sendFullUpdate(currentReadings, "etatPompeTank=1");
        Serial.println(F("[Auto] Données envoyées au serveur - pompe réservoir activée"));
      }

      uint32_t executionTime = (uint32_t)(millis() - startTime);
      Serial.printf("[CRITIQUE] Pompe démarrée en %u ms\n", (unsigned)executionTime);
      Serial.printf("[CRITIQUE] Démarrage pompe réservoir (niveau: %d cm, seuil: %d cm, durée: %lu s)\n",
                    r.wlAqua, aqThresholdCm, refillDurationMs / 1000);
      Serial.println(F("[CRITIQUE] === REMPLISSAGE EN COURS ==="));
      
      // Mail informatif de démarrage
      if (mailNotif && !emailTankStartSent) {
        char msg[512];
        snprintf(msg, sizeof(msg),
                 "Démarrage REMPLISSAGE automatique\n"
                 "- Niveau aquarium: %d cm (seuil: %d cm)\n"
                 "- Réserve: %d cm\n"
                 "- Durée prévue: %lu secondes\n"
                 "- Mode: Automatique",
                 r.wlAqua, aqThresholdCm, r.wlTank, refillDurationMs / 1000);
        _mailer.send("Remplissage démarré", msg, "System", _emailAddress);
        emailTankStartSent = true;
        emailTankStopSent = false; // Réinitialiser pour permettre le mail de fin
        Serial.println(F("[Auto] ✅ Email de démarrage remplissage envoyé"));
      }
    }
  }

  // ========================================
  // 1.5. FIN CYCLE MANUEL : Auto-stop + notification serveur (v11.66)
  // ========================================
  if (_manualTankOverride && _acts.isTankPumpRunning()) {
    if (_countdownEnd > 0 && millis() >= _countdownEnd) {
      Serial.println(F("[CRITIQUE] === FIN REMPLISSAGE MANUEL (timeout) ==="));

      // Arrêt physique
      _acts.stopTankPump(_pumpStartMs);
      _lastTankStopReason = TankPumpStopReason::MANUAL;

      // Reset flags
      _manualTankOverride = false;
      _countdownEnd = 0;
      _pumpStartMs = 0;

      // Notification serveur : pump_tank=0 pour reset état distant
      if (WiFi.status() == WL_CONNECTED && _config.isRemoteSendEnabled()) {
        SensorReadings cur = _sensors.read();
        bool success = sendFullUpdate(cur, "etatPompeTank=0&pump_tank=0&pump_tankCmd=0");
        if (success) {
          Serial.println(F("[Auto] ✅ Fin cycle notifiée au serveur - pump_tank=0"));
        } else {
          Serial.println(F("[Auto] ⚠️ Échec notification fin cycle au serveur - sera retentée"));
        }
      } else {
        Serial.println(F("[Auto] ⚠️ WiFi déconnecté - notification fin cycle reportée"));
      }
      
      // Mail informatif de fin de remplissage manuel
      if (mailNotif && !emailTankStopSent) {
        SensorReadings cur = _sensors.read();
        int levelImprovement = _levelAtPumpStart - cur.wlAqua;
        uint32_t actualDurationSec = refillDurationMs / 1000;
        char msg[512];
        snprintf(msg, sizeof(msg),
                 "Remplissage MANUEL terminé\n"
                 "- Durée: %u secondes\n"
                 "- Amélioration niveau: %d cm\n"
                 "- Niveau aquarium: %d cm (début: %d cm)\n"
                 "- Réserve: %d cm\n"
                 "- Mode: Manuel",
                 actualDurationSec, levelImprovement, cur.wlAqua, _levelAtPumpStart, cur.wlTank);
        _mailer.send("Remplissage terminé", msg, "System", _emailAddress);
        emailTankStopSent = true;
        emailTankStartSent = false; // Réinitialiser pour permettre le prochain mail de démarrage
        Serial.println(F("[Auto] ✅ Email de fin remplissage manuel envoyé"));
      }
    }
  }

  // ========================================
  // 2. ARRÊT FORCÉ APRÈS DURÉE MAX - SÉCURITÉ CRITIQUE
  // ========================================
  if (_acts.isTankPumpRunning()) {
    const uint32_t nowMs = millis();
    const uint32_t elapsedMs = (uint32_t)(nowMs - _pumpStartMs); // wrap-safe
    const uint32_t maxMs = refillDurationMs;
    if ((elapsedMs / 1000U) > 3000000U) {
      Serial.printf("[CRITIQUE] Anomaly: elapsed=%u ms (>3e6 s). Resetting pumpStartMs to now. nowMs=%u, startMs(old)=%u, maxMs=%u\n",
                    (unsigned)elapsedMs, (unsigned)nowMs, (unsigned)_pumpStartMs, (unsigned)maxMs);
      _pumpStartMs = nowMs;
    }
    if (elapsedMs >= maxMs) {
      Serial.printf("[CRITIQUE] Timing stop diag: nowMs=%u, startMs=%u, elapsedMs=%u, maxMs=%u\n",
                    (unsigned)nowMs, (unsigned)_pumpStartMs, (unsigned)elapsedMs, (unsigned)maxMs);
      Serial.println(F("[CRITIQUE] === ARRÊT FORCÉ REMPLISSAGE ==="));
      Serial.printf("[CRITIQUE] Durée écoulée: %u s, Durée max: %u s\n", (unsigned)(elapsedMs/1000U), (unsigned)(maxMs/1000U));

      // ARRÊT IMMÉDIAT - SÉCURITÉ CRITIQUE
      const uint32_t stopStartMs = millis();

      _lastTankStopReason = TankPumpStopReason::MAX_DURATION;

      _acts.stopTankPump(_pumpStartMs);
      _pumpStartMs = 0;
      _countdownEnd = 0;
      _manualTankOverride = false;

      if (WiFi.status() == WL_CONNECTED) {
        SensorReadings cur = _sensors.read();
        bool success = sendFullUpdate(cur, "etatPompeTank=0&pump_tank=0&pump_tankCmd=0");
        if (success) {
          Serial.println(F("[Auto] ✅ Fin cycle forcé notifiée au serveur - pump_tank=0"));
        } else {
          Serial.println(F("[Auto] ⚠️ Échec notification fin cycle forcé au serveur"));
        }
      }

      int levelImprovement = _levelAtPumpStart - r.wlAqua;
      Serial.printf("[CRITIQUE] Amélioration du niveau: %d cm\n", levelImprovement);

      if (levelImprovement < 1) {
        ++tankPumpRetries;
        Serial.printf("[CRITIQUE] Pompe inefficace (%u/%u) – niveau %d cm (début: %d cm, amélioration: %d cm)\n",
                      tankPumpRetries, MAX_PUMP_RETRIES, r.wlAqua, _levelAtPumpStart, levelImprovement);
        if (tankPumpRetries >= MAX_PUMP_RETRIES) {
          tankPumpLocked = true;
          _tankPumpLockReason = TankPumpLockReason::INEFFICIENT;
          Serial.println(F("[CRITIQUE] Pompe réservoir BLOQUÉE – plus d'essais"));
          sendFullUpdate(r, "etatPompeTank=0&pump_tankCmd=0&pump_tank=0");
          if (mailNotif && !emailTankSent) {
            char msg[1024];
            snprintf(msg, sizeof(msg),
                     "La pompe de réserve a été BLOQUÉE (inefficacité)\n"
                     "- Tentatives consécutives: %d/%d\n"
                     "- Amélioration mesurée: %.1f cm (min attendu: 1 cm)\n"
                     "- Aqua: %d cm, Réserve: %d cm\n"
                     "- Seuils: aqThreshold=%d, tankThreshold=%d cm\n"
                     "\nCe qui permettra le déblocage:\n"
                     "- Déblocage automatique: après ~30 s, si le niveau de réserve < %d cm\n"
                     "- Ou action manuelle: amorcer la pompe, vérifier tuyaux/filtres/prises d'air, s'assurer que la réserve est suffisante, puis relancer un cycle.\n",
                     tankPumpRetries, (unsigned)MAX_PUMP_RETRIES, levelImprovement, r.wlAqua, r.wlTank,
                     aqThresholdCm, tankThresholdCm, (int)tankThresholdCm - 10);
            _mailer.sendAlert("Pompe réservoir bloquée", msg, _emailAddress);
            emailTankSent = true;
            Serial.println(F("[CRITIQUE] Email d'alerte envoyé"));
          }
        }
      } else {
        tankPumpRetries = 0;
        Serial.printf("[CRITIQUE] Remplissage réussi: niveau amélioré de %d cm\n", levelImprovement);
        
        // Mail informatif de fin de remplissage réussi
        if (mailNotif && !emailTankStopSent) {
          uint32_t actualDurationSec = elapsedMs / 1000;
          char msg[512];
          snprintf(msg, sizeof(msg),
                   "Remplissage TERMINÉ avec succès\n"
                   "- Durée réelle: %u secondes\n"
                   "- Amélioration niveau: %d cm\n"
                   "- Niveau aquarium: %d cm (début: %d cm)\n"
                   "- Réserve: %d cm\n"
                   "- Mode: Automatique",
                   actualDurationSec, levelImprovement, r.wlAqua, _levelAtPumpStart, r.wlTank);
          _mailer.send("Remplissage terminé", msg, "System", _emailAddress);
          emailTankStopSent = true;
          emailTankStartSent = false; // Réinitialiser pour permettre le prochain mail de démarrage
          Serial.println(F("[Auto] ✅ Email de fin remplissage envoyé"));
        }
      }

      const uint32_t stopExecMs = (uint32_t)(millis() - stopStartMs);
      Serial.printf("[CRITIQUE] Arrêt effectué en %u ms\n", (unsigned)stopExecMs);
      Serial.println(F("[CRITIQUE] Arrêt pompe réservoir (durée écoulée)"));
      Serial.println(F("[CRITIQUE] === FIN REMPLISSAGE ==="));
    }
  }

  if (false && !_manualTankOverride && _acts.isTankPumpRunning() && r.wlAqua <= aqThresholdCm) {
    Serial.println(F("[CRITIQUE] === ARRÊT ANTICIPÉ REMPLISSAGE AUTO ==="));
    Serial.printf("[CRITIQUE] Niveau atteint: %d cm, Seuil: %d cm\n", r.wlAqua, aqThresholdCm);

    uint32_t stopTime = millis();

    _acts.stopTankPump(_pumpStartMs);
    _countdownEnd = 0;
    _manualTankOverride = false;
    tankPumpRetries = 0;

    uint32_t stopExecutionTime = (uint32_t)(millis() - stopTime);
    Serial.printf("[CRITIQUE] Arrêt anticipé effectué en %u ms\n", (unsigned)stopExecutionTime);
    Serial.println(F("[CRITIQUE] === REMPLISSAGE AUTO TERMINÉ AVEC SUCCÈS ==="));
  }

  if (!_manualTankOverride) {
    static uint8_t aboveCount = 0;
    static uint8_t belowCount = 0;
    if (r.wlTank > tankThresholdCm) {
      aboveCount = min<uint8_t>(aboveCount + 1, 3);
      belowCount = 0;
      if (!tankPumpLocked && aboveCount >= 2) {
        Serial.println(F("[CRITIQUE] === SÉCURITÉ RÉSERVE BASSE ==="));
        Serial.printf("[CRITIQUE] Niveau réservoir (distance): %d cm, Seuil: %d cm\n", r.wlTank, tankThresholdCm);
        tankPumpLocked = true;
        _tankPumpLockReason = TankPumpLockReason::RESERVOIR_LOW;
        _acts.stopTankPump(_pumpStartMs);
        _lastTankStopReason = TankPumpStopReason::OVERFLOW_SECURITY;
        _countdownEnd = 0;
        Serial.println(F("[CRITIQUE] Sécurité: pompe réservoir verrouillée (réserve trop basse confirmée)"));
        Serial.println(F("[CRITIQUE] === PROTECTION ACTIVÉE ==="));
        if (mailNotif && !emailTankSent) {
          char msg[512];
          snprintf(msg, sizeof(msg),
                   "La pompe de réserve a été VERROUILLÉE (réserve trop basse)\n"
                   "- Réserve (distance): %d cm (seuil: %d cm)\n"
                   "- Aqua: %d cm\n"
                   "\nDéblocage automatique: lorsque la distance réservoir < %d cm avec 3 confirmations consécutives.\n"
                   "Conseil: remplissez la réserve (éviter marche à sec), puis relancez.",
                   r.wlTank, tankThresholdCm, r.wlAqua, (int)tankThresholdCm - 5);
          _mailer.sendAlert("Pompe réservoir verrouillée (réserve trop basse)", msg, _emailAddress);
          emailTankSent = true;
        }
      }
    } else if (r.wlTank < tankThresholdCm - 5) {
      belowCount = min<uint8_t>(belowCount + 1, 3);
      aboveCount = 0;
      if (tankPumpLocked && belowCount >= 3) {
        tankPumpLocked = false;
        _tankPumpLockReason = TankPumpLockReason::NONE;
        emailTankSent = false;
        emailTankStartSent = false; // Réinitialiser pour permettre un nouveau mail de démarrage
        emailTankStopSent = false;   // Réinitialiser pour permettre un nouveau mail d'arrêt
        Serial.printf("[Auto] Pompe réservoir déverrouillée (réserve OK, distance: %d cm, confirmations)\n", r.wlTank);
      }
    } else {
      aboveCount = min<uint8_t>(aboveCount, 2);
      belowCount = min<uint8_t>(belowCount, 2);
    }
  }

  static unsigned long lastRecoveryAttempt = 0;
  if (tankPumpLocked && tankPumpRetries >= MAX_PUMP_RETRIES) {
    unsigned long currentMillisLocal = millis();
    if (currentMillisLocal - lastRecoveryAttempt > 30 * 1000UL) {
      if (r.wlTank < tankThresholdCm - 10) {
        Serial.println(F("[CRITIQUE] === RÉCUPÉRATION AUTOMATIQUE ==="));
        Serial.printf("[CRITIQUE] Niveau réservoir: %d cm, Seuil: %d cm\n", r.wlTank, tankThresholdCm);

        tankPumpLocked = false;
        tankPumpRetries = 0;
        emailTankSent = false;
        emailTankStartSent = false; // Réinitialiser pour permettre un nouveau mail de démarrage
        emailTankStopSent = false;   // Réinitialiser pour permettre un nouveau mail d'arrêt
        _tankPumpLockReason = TankPumpLockReason::NONE;
        lastRecoveryAttempt = currentMillisLocal;
        Serial.println(F("[CRITIQUE] Récupération automatique: pompe réservoir débloquée"));
        Serial.println(F("[CRITIQUE] === RÉCUPÉRATION EFFECTUÉE ==="));
      }
    }
  }
}


