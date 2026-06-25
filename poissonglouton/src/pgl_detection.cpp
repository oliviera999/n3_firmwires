#include "pgl_detection.h"

#include <Arduino.h>

#include "config.h"
#include "pgl_log.h"

void PglDetection::begin() {
  pinMode(PGL_IR_PIN, INPUT_PULLUP);

  irPresent_ = detectIrAtBoot();
  usPresent_ = detectUltrasonAtBoot();
  irPrevState_ = digitalRead(PGL_IR_PIN);

  PGL_LOG("Capteurs: IR=%s (GPIO%d) US=%s (GPIO%d)",
          irPresent_ ? "OK" : "absent", PGL_IR_PIN,
          usPresent_ ? "OK" : "absent", PGL_US_PIN);
  PGL_LOG_V("Seuils: US<=%ucm debounce=%lums tandem=%lums",
            PGL_ULTRASON_TRIGGER_CM, PGL_DEBOUNCE_MS, PGL_TANDEM_WINDOW_MS);
  if (irPresent_) {
    PGL_LOG_V("IR etat initial GPIO%d=%d (1=libre 0=obstacle)", PGL_IR_PIN, irPrevState_ ? 1 : 0);
  }
  if (usPresent_) {
    const uint16_t d0 = readUltrasonCm();
    PGL_LOG_V("US distance initiale: %u cm", d0);
  }
}

PglSensorMode PglDetection::getActiveMode() const {
  if (irPresent_ && usPresent_) return PglSensorMode::TANDEM;
  if (irPresent_) return PglSensorMode::IR;
  if (usPresent_) return PglSensorMode::ULTRASON;
  return PglSensorMode::NONE;
}

bool PglDetection::hasIr() const { return irPresent_; }
bool PglDetection::hasUltrason() const { return usPresent_; }

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

PglDetectionEvent PglDetection::poll() {
  const uint32_t now = millis();
  PglDetectionEvent event = {false, getActiveMode(), false};
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

  if (irPresent_ && usPresent_) {
    // Tandem : compter si l'un OU l'autre déclenche ; tandemValidated si les deux
    // ont vu l'objet dans la fenêtre (évite le blocage US seul quand IR est câblé).
    if (irTriggered || usTriggered) {
      event.detected = true;
      event.mode = PglSensorMode::TANDEM;
      if (irTriggered && usTriggered) {
        event.tandemValidated = true;
      } else if (lastIrEdgeMs_ > 0 && lastUsEdgeMs_ > 0) {
        const uint32_t dt = (lastIrEdgeMs_ > lastUsEdgeMs_)
                                ? (lastIrEdgeMs_ - lastUsEdgeMs_)
                                : (lastUsEdgeMs_ - lastIrEdgeMs_);
        event.tandemValidated = (dt <= PGL_TANDEM_WINDOW_MS);
      }
    }
  } else if (irTriggered || usTriggered) {
    event.detected = true;
    event.mode = irTriggered ? PglSensorMode::IR : PglSensorMode::ULTRASON;
    event.tandemValidated = false;
  }

  if (event.detected) {
    lastDetectionMs_ = now;
    PGL_LOG_V("Declenchement brut: IR=%d US=%d mode=%u tandem_ok=%d",
              irTriggered ? 1 : 0, usTriggered ? 1 : 0,
              static_cast<unsigned int>(event.mode),
              event.tandemValidated ? 1 : 0);
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
