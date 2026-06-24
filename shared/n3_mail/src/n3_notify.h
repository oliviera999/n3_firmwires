#ifndef N3_NOTIFY_H
#define N3_NOTIFY_H

// ---------------------------------------------------------------------------
// n3_notify — taxonomie de notification partagee (severite + mode de verbosite)
//
// Aligne sur le serveur n3_serveur (App\Notification\Severity / NotificationMode).
// Chaque alerte firmware porte une SEVERITE (P1..P4). Un MODE telecommandable
// (cle de config distante GPIO 101 "enableEmailChecked") plafonne la severite
// envoyee. Le mode est retro-compatible avec l'ancien interrupteur on/off :
//   "checked"   -> Full (tout)        "unchecked"/"" -> None (rien)
// et accepte les nouvelles valeurs   none | important | partial | full.
//
// Header-only, sans dependance Arduino (const char*, pas de String) : utilisable
// par n3pp / msp / uploadphotosserver et testable nativement (Unity / g++).
// ---------------------------------------------------------------------------

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

// Severite (importance) d'une notification. P1 = plus critique.
// La valeur numerique = priorite (1..4), utilisee pour le filtrage par mode.
enum class N3Severity : int {
  Critical   = 1,  // P1 : risque imminent (batterie critique, pompe collee...)
  Alert      = 2,  // P2 : anomalie a traiter (sol sec...)
  Info       = 3,  // P3 : confirmation d'action (arrosage effectue...)
  Diagnostic = 4   // P4 : rapports periodiques, demarrage, tests
};

// Mode de verbosite (telecommandable). La valeur = priorite maximale (incluse)
// laissee passer. 0 = rien ne passe.
enum class N3NotifMode : int {
  None      = 0,  // aucun envoi
  Important = 2,  // P1 + P2
  Partial   = 3,  // P1 + P2 + P3
  Full      = 4   // P1 + P2 + P3 + P4 (tout)
};

// Code court "P1".."P4" (expose dans le sujet pour tri cote boite mail).
inline const char* n3SeverityCode(N3Severity s) {
  switch (s) {
    case N3Severity::Critical:   return "P1";
    case N3Severity::Alert:      return "P2";
    case N3Severity::Info:       return "P3";
    case N3Severity::Diagnostic: return "P4";
  }
  return "P?";
}

inline int n3SeverityPriority(N3Severity s) { return static_cast<int>(s); }
inline int n3NotifModeMaxPriority(N3NotifMode m) { return static_cast<int>(m); }

// Le mode autorise-t-il l'envoi de cette severite ?
inline bool n3NotifModeAllows(N3NotifMode mode, N3Severity sev) {
  return n3SeverityPriority(sev) <= n3NotifModeMaxPriority(mode);
}

// Parse une valeur de config distante en mode (insensible a la casse, espaces ignores).
//  - retro-compat : "checked" -> Full ; "unchecked" -> None
//  - nouvelles    : "none"/"0" -> None ; "important" ; "partial" ; "full"
//  - vide         -> None ; inconnu -> fallback (Full par defaut)
inline N3NotifMode n3NotifModeFromString(const char* value,
                                         N3NotifMode fallback = N3NotifMode::Full) {
  if (value == nullptr) return fallback;

  char buf[16];
  size_t n = 0;
  for (const char* p = value; *p != '\0' && n < sizeof(buf) - 1; ++p) {
    char c = *p;
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
    buf[n++] = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  }
  buf[n] = '\0';

  if (n == 0) return N3NotifMode::None;
  if (strcmp(buf, "checked") == 0) return N3NotifMode::Full;     // legacy ON
  if (strcmp(buf, "unchecked") == 0) return N3NotifMode::None;   // legacy OFF
  if (strcmp(buf, "none") == 0 || strcmp(buf, "0") == 0) return N3NotifMode::None;
  if (strcmp(buf, "important") == 0) return N3NotifMode::Important;
  if (strcmp(buf, "partial") == 0) return N3NotifMode::Partial;
  if (strcmp(buf, "full") == 0) return N3NotifMode::Full;
  return fallback;
}

// Construit un sujet "[FAMILLE][Pn] objet" (borne) dans outBuf. Retourne outBuf.
// Si family est vide/NULL, produit "[Pn] objet".
inline const char* n3MailFormatSubject(char* outBuf, size_t outSize,
                                       const char* family, N3Severity sev,
                                       const char* subject) {
  if (outBuf == nullptr || outSize == 0) return "";
  const char* code = n3SeverityCode(sev);
  const char* subj = (subject != nullptr) ? subject : "";
  if (family != nullptr && family[0] != '\0') {
    snprintf(outBuf, outSize, "[%s][%s] %s", family, code, subj);
  } else {
    snprintf(outBuf, outSize, "[%s] %s", code, subj);
  }
  return outBuf;
}

#endif  // N3_NOTIFY_H
