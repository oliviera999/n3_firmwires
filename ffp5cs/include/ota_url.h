#pragma once
// =============================================================================
// FFP5CS — Normalisation d'URL OTA (downgrade HTTPS→HTTP)
// =============================================================================
// Extrait de ota_manager.cpp (audit §3.8) pour rendre testable la manipulation
// in-place de l'URL (memmove bug-prone : un off-by-one corromprait chaque URL
// OTA). WROOM/S3 homogénéisent l'OTA en HTTP ; l'intégrité du binaire reste
// garantie par vérification MD5 avant flash.
// =============================================================================

#include <Arduino.h>  // Serial (log)
#include <cstddef>
#include <cstring>    // memmove, strlen, strncmp

namespace OtaUrl {

// Convertit "https://..." en "http://..." sur place (supprime le 's').
// No-op si url null, buffer trop petit, ou schéma déjà non-https.
inline void downgradeToHttp(char* url, size_t bufSize) {
  if (!url || bufSize < 8) return;
  if (strncmp(url, "https://", 8) == 0) {
    memmove(url + 4, url + 5, strlen(url + 5) + 1);
    Serial.printf("[OTA] URL downgrade HTTPS→HTTP: %s\n", url);
  }
}

}  // namespace OtaUrl
