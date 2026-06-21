#pragma once
// =============================================================================
// FFP5CS — Formatage d'uptime (pur)
// =============================================================================
// Extrait de mailer.cpp (formatUptime, helper du bloc d'infos système des
// e-mails). Logique 100 % entière + snprintf, sans matériel ni état global :
// convertit une durée en millisecondes (typiquement millis()) en une chaîne
// "Jd HH:MM:SS" (jours, heures sur 2 chiffres, minutes, secondes).
//
// Pur : n'utilise que <cstdio> (std::snprintf) et <cstddef>. Aucune dépendance
// Arduino.h/config.h, aucun buffer statique (le caller fournit le buffer, comme
// l'original le faisait via g_uptimeBuffer). L'appel à millis() et le buffer
// statique restent côté mailer.cpp.
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
//
// Parité avec mailer.cpp:formatUptime :
//   totalSec = ms / 1000
//   days  = totalSec / 86400 ; totalSec %= 86400
//   hours = totalSec / 3600  ; totalSec %= 3600
//   mins  = totalSec / 60
//   secs  = totalSec % 60
// Les variables days/hours/mins/secs sont des unsigned int (mêmes troncatures
// implicites que l'original) ; le format snprintf est strictement identique.
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
