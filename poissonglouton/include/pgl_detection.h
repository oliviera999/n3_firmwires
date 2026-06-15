#pragma once

#include "pgl_types.h"

class PglDetection {
 public:
  void begin();
  PglSensorMode getActiveMode() const;
  bool hasIr() const;
  bool hasUltrason() const;
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
};
