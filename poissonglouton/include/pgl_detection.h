#pragma once

#include "pgl_types.h"

/** true si le debounce global autorise un nouveau comptage (testable en natif). */
inline bool pglDetectionDebounceAllowsCount(uint32_t nowMs, uint32_t lastDetectionMs,
                                            uint32_t debounceMs) {
  return lastDetectionMs == 0 || (nowMs - lastDetectionMs) >= debounceMs;
}

/**
 * Promotion runtime US (capteur absent au boot) : compte les mesures valides ;
 * un timeout isole ne remet plus le compteur a zero (demotion apres N echecs).
 */
inline void pglDetectionUpdateUsRuntimePresence(
    bool& usPresent, uint8_t& runtimeValidCount, uint8_t& runtimeInvalidCount,
    uint16_t usDistance, uint8_t promoteThreshold, uint8_t demoteThreshold) {
  if (usPresent) {
    return;
  }
  if (usDistance > 0) {
    runtimeInvalidCount = 0;
    if (runtimeValidCount < 255) {
      runtimeValidCount++;
    }
    if (runtimeValidCount >= promoteThreshold) {
      usPresent = true;
    }
    return;
  }
  if (runtimeValidCount > 0) {
    if (runtimeInvalidCount < 255) {
      runtimeInvalidCount++;
    }
    if (runtimeInvalidCount >= demoteThreshold) {
      runtimeValidCount = 0;
      runtimeInvalidCount = 0;
    }
  }
}

/** true si la zone US est libre (re-armement apres un declenchement). */
inline bool pglDetectionUsZoneClear(uint16_t usDistance, uint16_t triggerCm) {
  return usDistance == 0 || usDistance > triggerCm;
}

/** Filtre US consécutif sous seuil ; retourne true si declenchement (compteur remis a 0). */
inline bool pglDetectionCheckUsTrigger(uint8_t& usBelowCount, uint16_t usDistance,
                                       uint16_t triggerCm, uint8_t consecutiveRequired) {
  if (usDistance > 0 && usDistance <= triggerCm) {
    if (usBelowCount < 255) {
      usBelowCount++;
    }
  } else {
    usBelowCount = 0;
    return false;
  }
  if (usBelowCount >= consecutiveRequired) {
    usBelowCount = 0;
    return true;
  }
  return false;
}

/** Front PIR LOW->HIGH avec garde anti re-trigger (testable en natif). */
inline bool pglDetectionCheckPirTrigger(bool pirPrevState, bool pirState, uint32_t lastPirEdgeMs,
                                        uint32_t nowMs, uint32_t debounceMs) {
  const bool guardElapsed =
      (lastPirEdgeMs == 0) || ((nowMs - lastPirEdgeMs) >= debounceMs);
  return !pirPrevState && pirState && guardElapsed;
}

/** Front IR HIGH->LOW (obstacle, pull-up). */
inline bool pglDetectionCheckIrTrigger(bool irPrevState, bool irState) {
  return irPrevState && !irState;
}

class PglDetection {
 public:
  void begin();
  /** Masque des capteurs presents (bitmask PGL_SENS_* : IR=1, US=2, PIR=4). */
  uint8_t getActiveSensorsMask() const;
  bool hasIr() const;
  bool hasUltrason() const;
  bool hasPir() const;
  /** Dernière distance US (cm), 0 = hors portée / pas d'écho. Lecture limitée à ~10 Hz. */
  uint16_t getUltrasonDistanceCm();
  /** true si obstacle détecté (pin LOW avec pull-up). */
  bool readIrObstacle() const;
  /** true si mouvement PIR courant (sortie active-HAUT). false si PIR absent. */
  bool readPirMotion() const;
  PglDetectionEvent poll();

 private:
  bool detectIrAtBoot();
  bool detectUltrasonAtBoot();
  uint16_t readUltrasonCm();

  bool irPresent_ = false;
  bool usPresent_ = false;
  bool pirPresent_ = false;
  uint32_t lastDetectionMs_ = 0;
  uint32_t lastIrEdgeMs_ = 0;
  uint32_t lastUsEdgeMs_ = 0;
  uint32_t lastPirEdgeMs_ = 0;
  bool irPrevState_ = true;
  bool pirPrevState_ = false;  // PIR au repos = LOW (sortie active-HAUT)
  uint8_t usBelowCount_ = 0;
  uint16_t lastUltrasonCm_ = 0;
  uint32_t lastUltrasonReadMs_ = 0;
  uint8_t usRuntimeValidCount_ = 0;
  uint8_t usRuntimeInvalidCount_ = 0;
  bool usArmed_ = true;
};
