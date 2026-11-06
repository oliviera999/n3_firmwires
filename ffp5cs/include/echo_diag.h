#pragma once
// =============================================================================
// FFP5CS — Diagnostic d'écho ultrason (logique pure, testée en natif)
// =============================================================================
// v15.33 — Incident prod 30/10 → 06/11/2026 (n3_serveur
// docs/DIAGNOSTIC_FFP3_NIVEAU_AQUARIUM_2026-11.md) : le capteur aquarium a perdu tout
// écho, mais rien ne permettait à distance de savoir POURQUOI (aucun écho = câble /
// alimentation / capteur mort ; écho hors plage = objet ou eau collés au capteur). Seul
// le port série le disait. Ce diagnostic voyage désormais dans le POST.
//
// Contrat firmware → serveur (champ POST optionnel, bloc suffixe du corps signé) :
//   wlAquaDiag=<etat>,<timeouts>,<rejets>,<derniereDistanceRejeteeMm>
//   wlPotaDiag=…                        (même format, capteur du bac potager)
//   etat ∈ ok | no_echo | out_of_range | disabled
//   timeouts : tirs sans écho du cycle ; rejets : échos hors [20 mm ; 4000 mm[ ;
//   derniereDistanceRejeteeMm : distance du dernier écho rejeté (0 = aucun).
// Champ omis (chaîne vide) tant que le capteur n'a pas été lu dans le cycle.
// Côté serveur : PostDataController (wl*Diag → colonnes wlAquaStatus/Timeouts/Rejected/RawMm).
// =============================================================================

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace EchoDiag {

enum class Status : uint8_t {
  Unknown = 0,  // pas de lecture dans ce cycle → champ POST omis
  Ok,           // au moins un écho valide
  NoEcho,       // aucun écho (timeouts uniquement)
  OutOfRange,   // des échos, mais tous hors plage valide
  Disabled,     // capteur désactivé par SensorFailureManager (échecs consécutifs)
};

struct Diag {
  Status status = Status::Unknown;
  uint8_t timeouts = 0;
  uint8_t rejected = 0;
  uint16_t lastRejectedMm = 0;
};

inline uint8_t satInc(uint8_t v) { return v == 0xFF ? v : static_cast<uint8_t>(v + 1); }

/** Remise à zéro en début de cycle de lecture. */
inline void begin(Diag& d) { d = Diag{}; }

inline void recordTimeout(Diag& d) { d.timeouts = satInc(d.timeouts); }

inline void recordRejected(Diag& d, uint16_t mm) {
  d.rejected = satInc(d.rejected);
  d.lastRejectedMm = mm;
}

/** Statut final : un écho valide l'emporte ; sinon « désactivé », puis « hors plage », puis « sans écho ». */
inline Status classify(uint8_t validReadings, uint8_t rejected, bool disabled) {
  if (validReadings > 0) return Status::Ok;
  if (disabled) return Status::Disabled;
  if (rejected > 0) return Status::OutOfRange;
  return Status::NoEcho;
}

inline const char* statusName(Status s) {
  switch (s) {
    case Status::Ok: return "ok";
    case Status::NoEcho: return "no_echo";
    case Status::OutOfRange: return "out_of_range";
    case Status::Disabled: return "disabled";
    default: return "";
  }
}

/** Sérialise pour le POST ; chaîne vide si Unknown. Retourne la longueur écrite (0 si vide/erreur). */
inline int format(char* buf, size_t bufSize, const Diag& d) {
  if (!buf || bufSize == 0) return 0;
  buf[0] = '\0';
  if (d.status == Status::Unknown) return 0;
  int n = snprintf(buf, bufSize, "%s,%u,%u,%u", statusName(d.status), static_cast<unsigned>(d.timeouts),
                   static_cast<unsigned>(d.rejected), static_cast<unsigned>(d.lastRejectedMm));
  if (n < 0 || static_cast<size_t>(n) >= bufSize) {
    buf[0] = '\0';
    return 0;
  }
  return n;
}

}  // namespace EchoDiag
