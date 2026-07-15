#pragma once
// Orchestrateur du cycle de réveil deep-sleep (tranche T6.1 du chantier core
// shared). Fine couche de dispatch au-dessus du séquenceur PUR `n3AppNextStep`
// (n3_app_seq.h, T6.0) : elle appelle, dans l'ordre canonique, les callbacks
// fournis par le firmware, en réévaluant le saut OTA et en honorant les sorties
// anticipées (reset distant / veille immédiate).
//
// SANS dépendance Arduino/ESP : uniquement des pointeurs de fonction et le
// contexte. Le restart et l'entrée en veille NE sont PAS faits ici — ils sont
// délégués au callback `enterSleep` (le firmware choisit restart vs sleep selon
// `ctx.requestRestart`). Tout le matériel (WiFi, capteurs/caméra, OLED, POST/
// HMAC, sommeil) vit dans les callbacks du firmware, jamais dans cette lib :
// n3AppRun compile donc sous les 3 toolchains ET se teste intégralement en
// natif (test_app). Le contexte n'expose que des scalaires ; les états
// `RTC_DATA_ATTR` restent possédés par le firmware.
//
// Additive (sans consommateur) : l'adoption firmware par firmware viendra en
// T6.2 (commencer par uploadphotosserver, cf. docs du chantier).
#include "n3_app_seq.h"

// Callback d'étape : reçoit le contexte mutable et y écrit ses résultats
// (ex. connectWifi -> ctx.wifiOk, buildAndSendPayload -> ctx.postOkThisWake,
// une automation de veille -> ctx.requestSleepNow…). `nullptr` = étape sautée.
typedef void (*N3AppCb)(N3AppContext&);

// Prédicat « une vérification OTA est-elle due ? » — évalué à la transition
// REMOTE_CFG -> OTA pour décider du saut de l'étape OTA. `nullptr` = jamais due.
typedef bool (*N3AppPredicate)(N3AppContext&);

// Callbacks du cycle, un par étape de N3AppStep (hors DONE). Chaque champ peut
// être `nullptr` (étape inerte) — ex. `runAutomation` = nullptr pour
// uploadphotosserver qui n'a pas d'actionneurs.
struct N3AppConfig {
  N3AppCb        onWake;               // N3_STEP_WAKE       : brown-out, pinMode, restore RTC, splash
  N3AppCb        connectWifi;          // N3_STEP_WIFI       : Wificonnect (+ HeureSansWifi/radioReset) -> ctx.wifiOk
  N3AppCb        syncClock;            // N3_STEP_CLOCK      : configTime / resync NVS
  N3AppCb        applyRemoteConfig;    // N3_STEP_REMOTE_CFG : GET + parse clés PROPRE au firmware (A10)
  N3AppPredicate isOtaDue;             // décide le saut de l'étape OTA (nullptr => non due)
  N3AppCb        otaCheck;             // N3_STEP_OTA        : vérification OTA périodique
  N3AppCb        readSensorsOrCapture; // N3_STEP_SENSE      : lectureCapteurs / capturePhoto (+drain)
  N3AppCb        buildAndSendPayload;  // N3_STEP_PAYLOAD    : POST -> ctx.postOkThisWake
  N3AppCb        runAutomation;        // N3_STEP_AUTOMATION : arrosage / tracker (nullptr = aucun)
  N3AppCb        sendReports;          // N3_STEP_REPORTS    : mails de transition / stats / heartbeat
  N3AppCb        enterSleep;           // N3_STEP_SLEEP      : sommeil OU restart (selon ctx.requestRestart)
};

// Exécute un cycle de réveil : dispatch pur du callback de chaque étape (s'il est
// non nul) puis avancement via n3AppNextStep. `otaDue` n'est évalué qu'à l'étape
// REMOTE_CFG (seule transition où il compte), pour n'appeler le prédicat qu'une
// fois. La boucle se termine à N3_STEP_DONE ; sur cible, `enterSleep` ne rend en
// général pas la main (deep sleep / restart), ce qui termine le cycle avant.
inline void n3AppRun(const N3AppConfig& cfg, N3AppContext& ctx) {
  N3AppStep step = N3_STEP_WAKE;
  while (step != N3_STEP_DONE) {
    N3AppCb cb = nullptr;
    switch (step) {
      case N3_STEP_WAKE:       cb = cfg.onWake;               break;
      case N3_STEP_WIFI:       cb = cfg.connectWifi;          break;
      case N3_STEP_CLOCK:      cb = cfg.syncClock;            break;
      case N3_STEP_REMOTE_CFG: cb = cfg.applyRemoteConfig;    break;
      case N3_STEP_OTA:        cb = cfg.otaCheck;             break;
      case N3_STEP_SENSE:      cb = cfg.readSensorsOrCapture; break;
      case N3_STEP_PAYLOAD:    cb = cfg.buildAndSendPayload;  break;
      case N3_STEP_AUTOMATION: cb = cfg.runAutomation;        break;
      case N3_STEP_REPORTS:    cb = cfg.sendReports;          break;
      case N3_STEP_SLEEP:      cb = cfg.enterSleep;           break;
      case N3_STEP_DONE:                                      break;
    }
    if (cb) {
      cb(ctx);
    }
    // otaDue ne sert qu'à la transition REMOTE_CFG -> OTA ; ailleurs il est
    // ignoré par n3AppNextStep, donc on n'évalue le prédicat qu'à ce moment.
    const bool otaDue =
        (step == N3_STEP_REMOTE_CFG && cfg.isOtaDue) ? cfg.isOtaDue(ctx) : false;
    step = n3AppNextStep(step, ctx, otaDue);
  }
}
