#pragma once
// Stub minimal de <esp_random.h> pour les tests natifs : fournit esp_fill_random.
// Remplissage DÉTERMINISTE (zéros) — on ne teste que le FORMAT du nonce (longueur,
// hex, NUL), pas son entropie (validée sur cible avec le vrai HW RNG).
// Récupéré via -I stubs ; le build firmware utilise le vrai <esp_random.h>.

#include <stddef.h>

static inline void esp_fill_random(void* buf, size_t len) {
  unsigned char* p = (unsigned char*)buf;
  for (size_t i = 0; i < len; ++i) p[i] = 0;
}
