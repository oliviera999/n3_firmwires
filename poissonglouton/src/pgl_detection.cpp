#include "pgl_detection.h"

#include <Arduino.h>

#include "config.h"
#include "pgl_log.h"
#include "pgl_sensor_fusion.h"

void PglDetection::begin() {
  pinMode(PGL_IR_PIN, INPUT_PULLUP);

  irPresent_ = detectIrAtBoot();
  usPresent_ = detectUltrasonAtBoot();
  irPrevState_ = digitalRead(PGL_IR_PIN);

  // Presence PIR par flag : l'autodetection PIR n'est pas fiable (repos = LOW,
  // identique a une absence de module). Le module PIR pilote sa sortie en INPUT.
  pirPresent_ = (PGL_ENABLE_PIR != 0);
  if (pirPresent_) {
    pinMode(PGL_PIR_PIN, INPUT);
    pirPrevState_ = digitalRead(PGL_PIR_PIN);
  }

  PGL_LOG("Capteurs: IR=%s (GPIO%d) US=%s (GPIO%d) PIR=%s (GPIO%d)",
          irPresent_ ? "OK" : "absent", PGL_IR_PIN,
          usPresent_ ? "OK" : "absent", PGL_US_PIN,
          pirPresent_ ? "OK" : "absent", PGL_PIR_PIN);
  PGL_LOG_V("Seuils: US<=%ucm debounce=%lums corroboration=%lums",
            PGL_ULTRASON_TRIGGER_CM, PGL_DEBOUNCE_MS,
            PGL_SENSOR_CORROBORATION_WINDOW_MS);
  if (irPresent_) {
    PGL_LOG_V("IR etat initial GPIO%d=%d (1=libre 0=obstacle)", PGL_IR_PIN, irPrevState_ ? 1 : 0);
  }
  if (usPresent_) {
    const uint16_t d0 = readUltrasonCm();
    PGL_LOG_V("US distance initiale: %u cm", d0);
  }
  if (pirPresent_) {
    PGL_LOG_V("PIR etat initial GPIO%d=%d (0=repos 1=mouvement) presence_win=%lums",
              PGL_PIR_PIN, pirPrevState_ ? 1 : 0, PGL_PIR_PRESENCE_WINDOW_MS);
  }
}

uint8_t PglDetection::getActiveSensorsMask() const {
  uint8_t mask = 0;
  if (irPresent_) mask |= PGL_SENS_IR;
  if (usPresent_) mask |= PGL_SENS_US;
  if (pirPresent_) mask |= PGL_SENS_PIR;
  return mask;
}

bool PglDetection::hasIr() const { return irPresent_; }
bool PglDetection::hasUltrason() const { return usPresent_; }
bool PglDetection::hasPir() const { return pirPresent_; }

uint16_t PglDetection::getUltrasonDistanceCm() {
  const uint32_t now = millis();
  if ((now - lastUltrasonReadMs_) >= 100) {
    lastUltrasonReadMs_ = now;
    lastUltrasonCm_ = readUltrasonCm();
  }
  return lastUltrasonCm_;
}

bool PglDetection::readIrObstacle() const {
  if (!irPresent_) {
    return false;
  }
  return !digitalRead(PGL_IR_PIN);
}

bool PglDetection::readPirMotion() const {
  if (!pirPresent_) {
    return false;
  }
  return digitalRead(PGL_PIR_PIN);  // actif-HAUT : HIGH = mouvement
}

PglDetectionEvent PglDetection::poll() {
  const uint32_t now = millis();
  PglDetectionEvent event = {false, 0, false};
  const uint16_t usDistance = getUltrasonDistanceCm();

  // US absent au boot mais mesures valides en runtime → activer le capteur.
  if (!usPresent_ && usDistance > 0) {
    if (usRuntimeValidCount_ < 255) usRuntimeValidCount_++;
    if (usRuntimeValidCount_ >= 3) {
      usPresent_ = true;
      PGL_LOG("US: active en runtime (mesures valides sur GPIO%d)", PGL_US_PIN);
    }
  } else if (usDistance == 0) {
    usRuntimeValidCount_ = 0;
  }

#if PGL_VERBOSE_LOG
  static uint32_t lastTraceMs = 0;
  if ((now - lastTraceMs) >= 2000) {
    lastTraceMs = now;
    if (irPresent_) {
      const int irLevel = digitalRead(PGL_IR_PIN) ? 1 : 0;
      PGL_LOG_V("Sonde IR GPIO%d=%d (%s)", PGL_IR_PIN, irLevel, irLevel ? "libre" : "obstacle");
    }
    if (usPresent_) {
      PGL_LOG_V("Sonde US distance=%u cm (seuil %u, polls_sous_seuil=%u)",
                usDistance, PGL_ULTRASON_TRIGGER_CM,
                static_cast<unsigned int>(usBelowCount_));
    } else {
      PGL_LOG_V("Sonde US absente au boot — GPIO%d: %s",
                PGL_US_PIN,
                usDistance == 0 ? "pas d'echo" : "mesure brute");
      if (usDistance > 0) {
        PGL_LOG_V("  -> distance mesuree: %u cm (module peut-etre OK, relancer si cable branche)",
                  usDistance);
      }
    }
  }
#endif

  if ((now - lastDetectionMs_) < PGL_DEBOUNCE_MS) {
    return event;
  }

  bool irTriggered = false;
  bool usTriggered = false;
  bool pirTriggered = false;

  if (irPresent_) {
    const bool irState = digitalRead(PGL_IR_PIN);
    if (irPrevState_ && !irState) {
      irTriggered = true;
      lastIrEdgeMs_ = now;
      PGL_LOG("IR GPIO%d: front obstacle detecte", PGL_IR_PIN);
    }
    irPrevState_ = irState;
  }

  if (usPresent_) {
    // Filtre temporel : PGL_US_CONSECUTIVE_POLLS lectures consécutives sous le seuil.
    // Front : après déclenchement, l'objet doit sortir du champ pour recompter.
    if (usDistance > 0 && usDistance <= PGL_ULTRASON_TRIGGER_CM) {
      if (usBelowCount_ < 255) usBelowCount_++;
    } else {
      usBelowCount_ = 0;
    }
    if (usBelowCount_ == PGL_US_CONSECUTIVE_POLLS) {
      usTriggered = true;
      lastUsEdgeMs_ = now;
      usBelowCount_ = 0;
      PGL_LOG("US GPIO%d: declenchement distance=%u cm (seuil %u)",
              PGL_US_PIN, usDistance, PGL_ULTRASON_TRIGGER_CM);
    }
  }

  if (pirPresent_) {
    // Front LOW->HIGH (sortie PIR active-HAUT) avec garde anti re-trigger :
    // un nouveau front est ignore tant que PGL_PIR_DEBOUNCE_MS ne s'est pas
    // ecoule depuis le dernier (le PIR reste HIGH plusieurs secondes par nature).
    const bool pirState = digitalRead(PGL_PIR_PIN);
    const bool guardElapsed =
        (lastPirEdgeMs_ == 0) || ((now - lastPirEdgeMs_) >= PGL_PIR_DEBOUNCE_MS);
    if (!pirPrevState_ && pirState && guardElapsed) {
      pirTriggered = true;
      lastPirEdgeMs_ = now;
      pirEdgeGuard_ = 1;
      PGL_LOG("PIR GPIO%d: front mouvement detecte", PGL_PIR_PIN);
    }
    pirPrevState_ = pirState;
  }

  // Decision de comptage deleguee a la fonction pure de fusion : elle decide
  // a partir des triggers DEJA debounce de chaque capteur. presentMask la rend
  // robuste a tout sous-ensemble de {IR, US, PIR}.
  PglFusionInput in;
  in.presentMask = getActiveSensorsMask();
  in.irTriggered = irTriggered;
  in.usTriggered = usTriggered;
  in.pirTriggered = pirTriggered;
  in.lastIrEdgeMs = lastIrEdgeMs_;
  in.lastUsEdgeMs = lastUsEdgeMs_;
  in.lastPirEdgeMs = lastPirEdgeMs_;
  in.nowMs = now;
  in.corroborationWindowMs = PGL_SENSOR_CORROBORATION_WINDOW_MS;
  in.pirPresenceWindowMs = PGL_PIR_PRESENCE_WINDOW_MS;
  in.pirGatesCount = (PGL_PIR_GATES_COUNT != 0);
  in.pirCountsWhenAlone = (PGL_PIR_COUNTS_WHEN_ALONE != 0);

  const PglFusionResult fusion = pglFuseDetection(in);
  event.detected = fusion.detected;
  event.sensorsMask = fusion.sensorsMask;
  event.corroborated = fusion.corroborated;

  if (event.detected) {
    lastDetectionMs_ = now;
    PGL_LOG_V("Declenchement brut: IR=%d US=%d PIR=%d mask=%u corrobore=%d",
              irTriggered ? 1 : 0, usTriggered ? 1 : 0, pirTriggered ? 1 : 0,
              static_cast<unsigned int>(event.sensorsMask),
              event.corroborated ? 1 : 0);
  }
  return event;
}

bool PglDetection::detectIrAtBoot() {
  PGL_LOG("IR: test GPIO%d (pull-up, 1=libre 0=obstacle)", PGL_IR_PIN);
  uint8_t highCount = 0;
  uint8_t lowCount = 0;
  for (uint8_t i = 0; i < 20; ++i) {
    const bool state = digitalRead(PGL_IR_PIN);
    if (state) {
      highCount++;
    } else {
      lowCount++;
    }
    if (i < 4) {
      PGL_LOG("IR essai %u/20: GPIO%d=%d (%s)",
              static_cast<unsigned int>(i + 1),
              PGL_IR_PIN,
              state ? 1 : 0,
              state ? "libre" : "obstacle");
    }
    delay(5);
  }
  const bool ok = (highCount > 0 || lowCount > 0);
  PGL_LOG("IR: %s (%u HIGH, %u LOW) — etat=%s",
          ok ? "DETECTE" : "ABSENT",
          highCount,
          lowCount,
          digitalRead(PGL_IR_PIN) ? "libre" : "obstacle");
  return ok;
}

bool PglDetection::detectUltrasonAtBoot() {
  PGL_LOG("US: test HC-SR04 sur GPIO%d (seuil declenchement %ucm, max %ucm)",
          PGL_US_PIN, PGL_ULTRASON_TRIGGER_CM, PGL_ULTRASON_MAX_VALID_CM);
  uint8_t validReads = 0;
  for (uint8_t i = 0; i < 4; ++i) {
    const uint16_t d = readUltrasonCm();
    if (d == 0) {
      PGL_LOG("US essai %u/4: aucune mesure (timeout echo ou > %ucm)",
              static_cast<unsigned int>(i + 1), PGL_ULTRASON_MAX_VALID_CM);
    } else {
      PGL_LOG("US essai %u/4: %u cm", static_cast<unsigned int>(i + 1), d);
      validReads++;
    }
    delay(20);
  }
  const bool ok = validReads >= 1;
  PGL_LOG("US: %s (%u/4 lectures valides)", ok ? "DETECTE" : "ABSENT", validReads);
  return ok;
}

uint16_t PglDetection::readUltrasonCm() {
  // Broche partagée trig/echo : impulsion puis lecture sur le même GPIO (comme ffp5cs).
  pinMode(PGL_US_PIN, OUTPUT);
  digitalWrite(PGL_US_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(PGL_US_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(PGL_US_PIN, LOW);

  pinMode(PGL_US_PIN, INPUT);
  // Timeout pulseIn derive de la portee utile : au-dela de
  // PGL_ULTRASON_MAX_VALID_CM la mesure est de toute facon rejetee plus bas, donc
  // inutile de bloquer 25 ms a attendre un echo lointain/absent. Temps aller-retour
  // = 2 * distance / vitesse_son. A 343 m/s (0.0343 cm/us) : 2 * 120 cm / 0.0343
  // ≈ 7000 us. On ajoute ~15 % de marge (temperature/diffusion) -> ~8000 us, ce
  // qui ramene le pire cas de blocage de 25 ms a ~8 ms sans toucher la logique de
  // detection (seuils, debounce, tandem, filtrage inchanges).
  constexpr unsigned long kEchoTimeoutUs =
      static_cast<unsigned long>((2.0f * PGL_ULTRASON_MAX_VALID_CM / 0.0343f) * 1.15f);
  const unsigned long duration = pulseIn(PGL_US_PIN, HIGH, kEchoTimeoutUs);
  if (duration == 0) return 0;
  const uint16_t distanceCm = static_cast<uint16_t>((duration * 0.0343f) / 2.0f);
  if (distanceCm > PGL_ULTRASON_MAX_VALID_CM) return 0;
  return distanceCm;
}
