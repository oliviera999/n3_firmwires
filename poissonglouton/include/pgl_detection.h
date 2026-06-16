#pragma once

#include "pgl_types.h"

class PglDetection {
 public:
  void begin();
  PglSensorMode getActiveMode() const;
  bool hasIr() const;
  bool hasUltrason() const;
  /** Dernière distance US (cm), 0 = hors portée / pas d'écho. Lecture limitée à ~10 Hz. */
  uint16_t getUltrasonDistanceCm();
  /** true si obstacle détecté (pin LOW avec pull-up). */
  bool readIrObstacle() const;
  PglDetectionEvent poll();

 private:
  bool detectIrAtBoot();
  bool detectUltrasonAtBoot();
  uint16_t readUltrasonCm();

  bool irPresent_ = false;
  bool usPresent_ = false;
  uint32_t lastDetectionMs_ = 0;
  uint32_t lastIrEdgeMs_ = 0;
  uint32_t lastUsEdgeMs_ = 0;
  bool irPrevState_ = true;
  // Polls US consécutifs sous le seuil (filtre anti-écho + front).
  uint8_t usBelowCount_ = 0;
  uint16_t lastUltrasonCm_ = 0;
  uint32_t lastUltrasonReadMs_ = 0;
  uint8_t usRuntimeValidCount_ = 0;
};
