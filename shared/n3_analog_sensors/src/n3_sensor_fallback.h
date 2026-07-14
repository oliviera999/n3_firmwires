#pragma once
// =============================================================================
// n3_analog_sensors — Cascade de repli d'une lecture capteur (pur)
// =============================================================================
// Mutualisé depuis ffp5cs (sensor_reading_fallback.h), logique identique mais
// API renommée en termes NEUTRES (l'original parlait de « waterLevel » ; la
// cascade vaut pour toute mesure uint16 où 0 = « pas de mesure »).
//
// Convention : une valeur de 0 signifie « lecture invalide/absente ».
//   resolve       : current > 0 ? current : (lastValid > 0 ? lastValid : fallback)
//   resolveOrZero : idem, mais si useFallback=false renvoie 0 quand current
//                   est invalide (mesure omise, ni dernier-bon ni défaut)
//   formatPostValue : 0 -> chaîne vide (champ omis du POST), sinon décimal.
//                     Cette distinction fait partie du contrat firmware<->serveur
//                     (le champ vide n'entre pas dans le body signé).
//
// Pur : <stdint.h>/<stdio.h> uniquement. Testé nativement (test_sensor_fallback).
// =============================================================================

#include <stdint.h>
#include <stdio.h>

namespace N3SensorFallback {

inline uint16_t resolve(uint16_t current, uint16_t lastValid, uint16_t fallback) {
  if (current > 0) {
    return current;
  }
  if (lastValid > 0) {
    return lastValid;
  }
  return fallback;
}

/** useFallback=false : mesure absente (0) si current invalide, sans lastValid ni défaut. */
inline uint16_t resolveOrZero(uint16_t current, uint16_t lastValid,
                              uint16_t fallback, bool useFallback) {
  if (current > 0) {
    return current;
  }
  if (!useFallback) {
    return 0;
  }
  return resolve(current, lastValid, fallback);
}

inline void formatPostValue(char* buf, size_t bufSize, uint16_t v) {
  if (!buf || bufSize == 0) {
    return;
  }
  if (v == 0) {
    buf[0] = '\0';
  } else {
    snprintf(buf, bufSize, "%u", static_cast<unsigned>(v));
  }
}

}  // namespace N3SensorFallback
