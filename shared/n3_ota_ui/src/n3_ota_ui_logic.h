#pragma once
// Logique pure du harnais OTA/OLED (détection « déjà à jour », pourcentage de
// l'écran de fin, largeur de la barre de progression) — extraite verbatim de
// n3pp/msp main.cpp (tranche T4.2 du chantier core shared), sans dépendance
// Arduino, testée en natif (test_ota_ui).
#include <stdint.h>
#include <string.h>

// Un libellé de fin n3OtaCheck signifie « déjà à jour » (pas un échec) si les
// détails contiennent l'un des deux motifs émis par n3_ota.
inline bool n3OtaUiEndIsUpToDate(const char* details) {
  return (details != nullptr) &&
         (strstr(details, "deja a jour") != nullptr ||
          strstr(details, "pas de mise a jour") != nullptr);
}

// Pourcentage affiché sur l'écran de fin (hors succès) : 100 si « déjà à
// jour », sinon dernier pourcentage rendu (255 = aucun -> 0).
inline uint8_t n3OtaUiEndPercent(bool upToDate, uint8_t displayedPercent) {
  if (upToDate) return 100;
  return (displayedPercent == 255) ? 0 : displayedPercent;
}

// Largeur de remplissage de la barre de progression (cadre de screenWidth px,
// bordure de 1 px de chaque côté).
inline int n3OtaUiProgressFillWidth(int screenWidth, uint8_t percent) {
  return (percent >= 100) ? (screenWidth - 2) : ((percent * (screenWidth - 2)) / 100);
}
