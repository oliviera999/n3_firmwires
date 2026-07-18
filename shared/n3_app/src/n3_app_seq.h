#pragma once
// Séquenceur PUR du cycle de réveil deep-sleep (cohorte n3pp / msp /
// uploadphotosserver). Décrit l'ordre canonique des étapes d'un réveil unique
// (setup → sleep) et la décision d'enchaînement d'une étape à la suivante.
//
// Contrat de cycle SANS dépendance Arduino/ESP : uniquement <stdint.h> + bool.
// Le firmware possède son état (RTC_DATA_ATTR, matériel, WiFi…) ; ce header n'en
// transporte que des scalaires (le « contexte ») et ne décide QUE de l'ordre.
// Aucune String, aucun malloc, aucun effet de bord — 100 % testable en natif
// (test_app), compile sous les 3 toolchains. Tranche T6.0 (additive, sans
// consommateur) du chantier core shared : le wrapper on-target (n3AppRun,
// callbacks) viendra en T6.1. Style calqué sur n3_common/n3_ota_periodic.h.
#include <stdint.h>

// Étapes du cycle de réveil deep-sleep, dans l'ordre canonique. La valeur
// numérique = rang dans le cycle : n3AppNextStep s'appuie sur cet ordre.
enum N3AppStep {
  N3_STEP_WAKE = 0,   // réveil : cause, compteur de boot, init état RTC
  N3_STEP_WIFI,       // connexion WiFi (multi-réseaux)
  N3_STEP_CLOCK,      // synchro horloge (NTP / restauration NVS)
  N3_STEP_REMOTE_CFG, // récupération de la config distante
  N3_STEP_OTA,        // vérification OTA périodique (sautable si non due)
  N3_STEP_SENSE,      // acquisition capteurs / capture
  N3_STEP_PAYLOAD,    // construction + envoi du payload (POST)
  N3_STEP_AUTOMATION, // automatismes locaux (arrosage, servo, alertes…)
  N3_STEP_REPORTS,    // rapports mail / stats réseau
  N3_STEP_SLEEP,      // préparation et entrée en veille (ou restart)
  N3_STEP_DONE        // cycle terminé (état absorbant, idempotent)
};

// État de réveil partagé (le contexte). Ne DÉFINIT aucun RTC_DATA_ATTR : le
// firmware possède son état, le contexte n'en transporte que des scalaires.
struct N3AppContext {
  uint8_t  wakeupCause;       // esp_sleep_get_wakeup_cause() côté firmware (opaque ici)
  bool     wifiOk;
  bool     clockValid;
  uint32_t epochSeconds;      // 0 si non fiable
  uint32_t bootCount;
  bool     postOkThisWake;
  uint32_t sleepSeconds;
  bool     requestRestart;    // sortie anticipée -> ESP.restart() (reset distant)
  bool     requestSleepNow;   // sortie anticipée -> sommeil immédiat (veille batterie / échec capture)
  void*    user;              // pointeur d'état firmware
};

// Décision PURE d'enchaînement. Renvoie la prochaine étape à exécuter à partir
// de `current`, en honorant :
//   - requestRestart / requestSleepNow  -> saute directement à N3_STEP_SLEEP
//     (le firmware, dans son callback enterSleep, fera restart ou sleep selon le flag)
//   - otaDue == false                    -> saute l'étape N3_STEP_OTA
// L'ordre nominal est celui de l'enum. Fonction sans effet de bord.
inline N3AppStep n3AppNextStep(N3AppStep current, const N3AppContext& ctx, bool otaDue) {
  // Règle 1 (idempotence) : une fois le cycle terminé, il le reste. On traite ce
  // cas en premier pour que ni les flags ni rien d'autre ne relancent le cycle
  // (pas de boucle infinie une fois à DONE).
  if (current >= N3_STEP_DONE) {
    return N3_STEP_DONE;
  }

  // Règle 2 (sortie anticipée) : un reset distant (requestRestart) ou une mise en
  // veille immédiate (requestSleepNow) court-circuite le reste du cycle et va
  // droit à N3_STEP_SLEEP — mais uniquement si l'on n'est pas déjà à/après SLEEP
  // (sinon on renverrait SLEEP en boucle sans jamais atteindre DONE). Le firmware
  // décidera restart vs sleep selon le flag dans son callback d'entrée en veille.
  if ((ctx.requestRestart || ctx.requestSleepNow) && current < N3_STEP_SLEEP) {
    return N3_STEP_SLEEP;
  }

  // Règle 3 (fin nominale) : depuis SLEEP on va à DONE.
  if (current == N3_STEP_SLEEP) {
    return N3_STEP_DONE;
  }

  // Règle 4 (avancement nominal) : étape suivante dans l'ordre de l'enum.
  N3AppStep next = static_cast<N3AppStep>(static_cast<int>(current) + 1);

  // Règle 5 (saut OTA) : si la prochaine étape est OTA mais qu'aucune vérification
  // n'est due, on la saute — REMOTE_CFG enchaîne directement sur SENSE.
  if (next == N3_STEP_OTA && !otaDue) {
    return N3_STEP_SENSE;
  }

  return next;
}
