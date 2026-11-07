#pragma once
// =============================================================================
// n3_common — Diagnostic de boot (logique PURE) : raison de reset, état OTA de l'image,
// compteur de boots ratés consécutifs, décision « mode sans échec »
// =============================================================================
// Mutualisé pour ffp5cs / n3pp / msp (incident du 06/11/2026). Le premier heartbeat après boot
// transporte ces informations (`rst`, `part`, `ota`, `fails`, `safe`, `boot=1`) ; le serveur les
// range dans *Heartbeat et device_events (n3_serveur ≥ 6.59.0).
//
// Cycle du compteur de boots ratés :
//   boot        → failedBoots += 1 (persisté AVANT tout le reste : un crash immédiat compte)
//   boot sain   → failedBoots = 0 (critère : Wi-Fi connecté + heure fiable + POST accepté)
//   ≥ kSafeModeAfterFailedBoots boots ratés d'affilée → démarrage en MODE SANS ÉCHEC : pas d'OTA,
//   pas d'automatismes non essentiels, pompe aquarium maintenue, heartbeat minimal `safe=1`.
// Aucune dépendance Arduino ; esp_reset_reason_t vient de <esp_system.h> (stub en natif).
// =============================================================================

#include <stdint.h>
#include <esp_system.h>

#include "n3_reset_reason.h"

namespace n3boot {

constexpr uint8_t kSafeModeAfterFailedBoots = 3;

// Libellés serveur de l'état OTA de l'image en cours (esp_ota_img_states_t) — alignés sur
// HeartbeatExtras (n3_serveur) et sur la clé `ota` du heartbeat.
// Valeurs ESP-IDF : NEW=0, PENDING_VERIFY=1, VALID=2, INVALID=3, ABORTED=4, UNDEFINED=0xFFFFFFFF.
inline const char* otaStateLabel(uint32_t state) {
  switch (state) {
    case 0: return "new";
    case 1: return "pending";
    case 2: return "valid";
    case 3: return "invalid";
    case 4: return "aborted";
    default: return "undefined";
  }
}

// Doit-on démarrer en mode sans échec ? `failedBoots` est le compteur APRÈS incrément du boot
// courant (donc 3 = ce boot est le 3e raté consécutif).
inline bool shouldEnterSafeMode(uint8_t failedBoots, uint8_t threshold = kSafeModeAfterFailedBoots) {
  return threshold > 0 && failedBoots >= threshold;
}

// Un reset pour cause de crash ou de watchdog au boot précédent rend le boot courant « suspect »
// (il compte dans les ratés même si le précédent avait été marqué sain avant de crasher plus tard ?
// NON : un crash après un boot sain est un incident d'EXÉCUTION, pas de boot ; seul le compteur
// non remis à zéro au cours du boot précédent fait foi). Cette fonction sert uniquement au libellé.
inline bool isCrashReset(esp_reset_reason_t reason) { return ResetReason::isCrash(reason); }

}  // namespace n3boot
