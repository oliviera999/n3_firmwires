#pragma once
// =============================================================================
// n3_time — Formatage d'uptime (pur)
// =============================================================================
// Mutualisé depuis ffp5cs/include/uptime_format.h (helper formatUptime du bloc
// d'infos système des e-mails). Logique 100 % entière + snprintf, sans matériel
// ni état global : convertit une durée en millisecondes (typiquement millis())
// en une chaîne "Jd HH:MM:SS" (jours, heures sur 2 chiffres, minutes, secondes).
//
// Pur : n'utilise que <cstdio> (std::snprintf) et <cstddef>. Aucune dépendance
// Arduino.h/config.h, aucun buffer statique (le caller fournit le buffer).
//
// Parité : transcription ligne-à-ligne du code inline d'origine (mêmes divisions
// entières, mêmes troncatures vers unsigned int, même format snprintf
// "%ud %02u:%02u:%02u").
// =============================================================================

#include <cstdio>
#include <cstddef>

namespace UptimeFormat {

// Écrit l'uptime formaté ("Jd HH:MM:SS") dans buf (taille bufSize) à partir d'une
// durée en millisecondes. Renvoie buf (ou rien à écrire si buf/bufSize invalides).
inline char* formatUptime(unsigned long ms, char* buf, size_t bufSize) {
  if (buf == nullptr || bufSize == 0) {
    return buf;
  }
  unsigned long totalSec = ms / 1000UL;
  unsigned int days = totalSec / 86400UL;
  totalSec %= 86400UL;
  unsigned int hours = totalSec / 3600UL;
  totalSec %= 3600UL;
  unsigned int mins = totalSec / 60UL;
  unsigned int secs = totalSec % 60UL;
  std::snprintf(buf, bufSize, "%ud %02u:%02u:%02u", days, hours, mins, secs);
  return buf;
}

}  // namespace UptimeFormat
