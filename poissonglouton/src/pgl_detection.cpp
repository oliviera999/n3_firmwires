#include "pgl_detection.h"

#include <Arduino.h>

#include "config.h"
#include "pgl_log.h"

void PglDetection::begin() {
  pinMode(PGL_IR_PIN, INPUT_PULLUP);
  pinMode(PGL_US_TRIG_PIN, OUTPUT);
  pinMode(PGL_US_ECHO_PIN, INPUT);
  digitalWrite(PGL_US_TRIG_PIN, LOW);

  irPresent_ = detectIrAtBoot();
  usPresent_ = detectUltrasonAtBoot();
  irPrevState_ = digitalRead(PGL_IR_PIN);

  PGL_LOG("Capteurs: IR=%s (GPIO%d) US=%s (TRIG%d ECHO%d)",
          irPresent_ ? "OK" : "absent", PGL_IR_PIN,
          usPresent_ ? "OK" : "absent", PGL_US_TRIG_PIN, PGL_US_ECHO_PIN);
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

PglDetectionEvent PglDetection::poll() {
  const uint32_t now = millis();
  PglDetectionEvent event = {false, getActiveMode(), false};

#if PGL_VERBOSE_LOG
  static uint32_t lastTraceMs = 0;
  if ((now - lastTraceMs) >= 3000) {
    lastTraceMs = now;
    if (irPresent_) {
      PGL_LOG_V("Sonde IR GPIO%d=%d", PGL_IR_PIN, digitalRead(PGL_IR_PIN) ? 1 : 0);
    }
    if (usPresent_) {
      PGL_LOG_V("Sonde US distance=%u cm (seuil %u)", readUltrasonCm(), PGL_ULTRASON_TRIGGER_CM);
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
    }
    irPrevState_ = irState;
  }

  if (usPresent_) {
    const uint16_t distanceCm = readUltrasonCm();
    // Filtre temporel : il faut PGL_US_CONSECUTIVE_POLLS lectures consécutives
    // sous le seuil pour valider (rejette les échos parasites isolés), et le
    // déclenchement se fait sur front (l'objet doit sortir du champ avant de
    // pouvoir compter à nouveau) — comme l'IR.
    if (distanceCm > 0 && distanceCm <= PGL_ULTRASON_TRIGGER_CM) {
      if (usBelowCount_ < 255) usBelowCount_++;
    } else {
      usBelowCount_ = 0;
    }
    if (usBelowCount_ == PGL_US_CONSECUTIVE_POLLS) {
      usTriggered = true;
      lastUsEdgeMs_ = now;
    }
  }

  if (irPresent_ && usPresent_) {
    const bool tandem = irTriggered || usTriggered;
    if (tandem) {
      const uint32_t dt = (lastIrEdgeMs_ > lastUsEdgeMs_) ? (lastIrEdgeMs_ - lastUsEdgeMs_) : (lastUsEdgeMs_ - lastIrEdgeMs_);
      if (dt <= PGL_TANDEM_WINDOW_MS) {
        event.detected = true;
        event.mode = PglSensorMode::TANDEM;
        event.tandemValidated = true;
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
  // Heuristique simple: on lit plusieurs fois, un pin flottant varie fortement.
  uint8_t highCount = 0;
  uint8_t lowCount = 0;
  for (uint8_t i = 0; i < 20; ++i) {
    const bool state = digitalRead(PGL_IR_PIN);
    if (state) highCount++; else lowCount++;
    delay(5);
  }
  return (highCount > 0 || lowCount > 0);
}

bool PglDetection::detectUltrasonAtBoot() {
  uint8_t validReads = 0;
  for (uint8_t i = 0; i < 4; ++i) {
    const uint16_t d = readUltrasonCm();
    if (d > 0 && d <= PGL_ULTRASON_MAX_VALID_CM) validReads++;
    delay(20);
  }
  return validReads >= 1;
}

uint16_t PglDetection::readUltrasonCm() {
  digitalWrite(PGL_US_TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(PGL_US_TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(PGL_US_TRIG_PIN, LOW);

  const unsigned long duration = pulseIn(PGL_US_ECHO_PIN, HIGH, 25000UL);
  if (duration == 0) return 0;
  const uint16_t distanceCm = static_cast<uint16_t>((duration * 0.0343f) / 2.0f);
  if (distanceCm > PGL_ULTRASON_MAX_VALID_CM) return 0;
  return distanceCm;
}
