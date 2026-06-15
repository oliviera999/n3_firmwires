#pragma once
// =============================================================================
// FFP5CS — Décision de blocage de la veille (pure, testable nativement)
// =============================================================================
// Extrait de AutomatismSleep::handleBlockingConditions (automatism_sleep.cpp).
// Chemin VEILLE = critique (un mauvais blocage = batterie vidée ou réveils manqués),
// d'où une extraction de la SEULE décision « faut-il bloquer la veille, et pour quelle
// raison », sous forme PURE : aucune dépendance Arduino/config, temps injecté.
//
// Périmètre (peu invasif) :
//   - la fonction prend l'état explicite (`State` = `wsBlockStartMs`, seul timestamp
//     qui INFLUENCE la décision : il pilote le timeout WebSocket de 5 min) + les
//     entrées (`Inputs`, dont `nowMs` et `webActivityTimeoutMs` injectés) ;
//   - elle renvoie un `Result` : la `Reason` terminale (ou Allow) + les transitions et
//     effets que l'APPELANT applique (logs Serial, anti-spam/throttle, mutations des
//     membres/paramètres `forceWakeFromWeb`/`lastWakeMs`). Voir handleBlockingConditions.
//
// Les timestamps de throttle des logs (`_lastWsLogMs`/`_lastWebLogMs`/`_lastForceWakeLogMs`)
// ne conditionnent QUE l'affichage (jamais la décision) → laissés à l'appelant (cosmétique).
//
// Types : `uint32_t` pour toute l'arithmétique millis() → wrap-around 32 bits identique
// sur ESP32 (où `unsigned long` == 32 bits) ET en test natif (où il vaut 64 bits).
// =============================================================================

#include <cstdint>

namespace SleepBlocking {

// Timeout dur : au-delà, on force la veille MALGRÉ des clients WebSocket (5 min).
inline constexpr uint32_t WS_BLOCK_TIMEOUT_MS = 300000;
// Intervalle anti-spam des logs « bloqué » (throttle) — appliqué par l'appelant.
inline constexpr uint32_t LOG_THROTTLE_MS = 30000;
// Seuil « décompte long » vs « court » (secondes restantes).
inline constexpr uint32_t COUNTDOWN_LONG_THRESHOLD_SEC = 300;

// Raison terminale du blocage de veille (ou Allow = veille permise).
enum class Reason : uint8_t {
  Allow,          // aucune condition bloquante -> veille permise (handleBlockingConditions -> true)
  WsClients,      // clients WebSocket connectés (dans le timeout)
  WebActivity,    // activité web récente (forceWakeFromWeb, non expirée)
  ForceWakeUp,    // forceWakeUp GPIO persistant
  TankPump,       // pompe de remplissage active
  Feeding,        // nourrissage en cours
  CountdownLong,  // décompte long (> 300 s restants)
  CountdownShort  // décompte court (<= 300 s restants)
};

// État persistant influençant la décision : début du blocage WebSocket (pilote le
// timeout 5 min). C'est le membre `_wsBlockStartMs`. decide() le met à jour.
struct State {
  uint32_t wsBlockStartMs = 0;
};

// Entrées (toutes fournies par l'appelant — Wi-Fi/temps non abstraits, injectés).
struct Inputs {
  uint8_t  wsClients = 0;
  bool     forceWakeFromWeb = false;
  uint32_t lastWebActivityMs = 0;
  bool     forceWakeUp = false;
  bool     tankPumpRunning = false;
  bool     feedingInProgress = false;
  uint32_t countdownEnd = 0;
  uint32_t nowMs = 0;
  uint32_t webActivityTimeoutMs = 0;  // = TimingConfig::WEB_ACTIVITY_TIMEOUT_MS (injecté)
};

// Résultat : la raison + les effets que l'appelant applique (logs/throttle/mutations).
struct Result {
  Reason   reason = Reason::Allow;
  bool     wsTimedOut = false;        // transition: log TIMEOUT (non-throttlé) ; wsBlockStartMs déjà remis à 0
  bool     webExpired = false;        // transition: forceWakeFromWeb=false + log « expirée » (non-throttlé)
  bool     refreshLastWake = false;   // lastWakeMs = now (tankPump / feeding / décompte long)
  uint32_t wsElapsedMs = 0;           // pour le log « bloqué (… %u ms écoulées) » (raison WsClients)
  uint32_t remainingCountdownSec = 0; // pour les logs décompte (long/court)
  bool canSleep() const { return reason == Reason::Allow; }
};

// Réplique EXACTE du helper anonyme isStillPending de automatism_sleep.cpp (wrap-safe :
// cast signé pour gérer le débordement millis()).
inline bool isStillPending(uint32_t targetMs, uint32_t nowMs) {
  return targetMs != 0 && static_cast<int32_t>(targetMs - nowMs) > 0;
}

// Décision pure. Met à jour `st.wsBlockStartMs` (cycle de vie du blocage WS). Renvoie
// le Result. Parité ligne à ligne avec l'ancien corps de handleBlockingConditions
// (ordre des conditions, comparaisons strictes, et transitions « fall-through » inclus).
inline Result decide(State& st, const Inputs& in) {
  Result res;
  const uint32_t now = in.nowMs;

  // 1. VÉRIFICATION CLIENTS WEBSOCKET
  if (in.wsClients > 0) {
    if (st.wsBlockStartMs == 0) {
      st.wsBlockStartMs = now;
    }
    if (now - st.wsBlockStartMs > WS_BLOCK_TIMEOUT_MS) {
      // Timeout atteint : forcer la veille malgré les clients -> log TIMEOUT, reset, fall-through.
      res.wsTimedOut = true;
      st.wsBlockStartMs = 0;
    } else {
      res.wsElapsedMs = now - st.wsBlockStartMs;
      res.reason = Reason::WsClients;
      return res;
    }
  } else {
    st.wsBlockStartMs = 0;
  }

  // 2. ACTIVITÉ WEB TEMPORAIRE
  if (in.forceWakeFromWeb) {
    if (in.lastWebActivityMs > 0 && (now - in.lastWebActivityMs > in.webActivityTimeoutMs)) {
      res.webExpired = true;  // -> appelant: forceWakeFromWeb=false + log « expirée »
    } else {
      res.reason = Reason::WebActivity;
      return res;
    }
  }

  // 3. FORCEWAKEUP PERSISTANT
  if (in.forceWakeUp) {
    res.reason = Reason::ForceWakeUp;
    return res;
  }

  // 4. POMPE DE REMPLISSAGE
  if (in.tankPumpRunning) {
    res.refreshLastWake = true;
    res.reason = Reason::TankPump;
    return res;
  }

  // 5. NOURRISSAGE EN COURS
  if (in.feedingInProgress) {
    res.refreshLastWake = true;
    res.reason = Reason::Feeding;
    return res;
  }

  // 6. DÉCOMPTE EN COURS
  if (isStillPending(in.countdownEnd, now)) {
    res.remainingCountdownSec = static_cast<uint32_t>(in.countdownEnd - now) / 1000UL;
    if (res.remainingCountdownSec > COUNTDOWN_LONG_THRESHOLD_SEC) {
      res.refreshLastWake = true;
      res.reason = Reason::CountdownLong;
    } else {
      res.reason = Reason::CountdownShort;
    }
    return res;
  }

  // v11.178: Bloc hasSignificantActivity() supprimé (toujours false - audit dead-code)
  res.reason = Reason::Allow;
  return res;
}

}  // namespace SleepBlocking
