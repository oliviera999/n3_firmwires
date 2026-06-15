#pragma once
// =============================================================================
// FFP5CS — Anti-spam alerte « aquarium trop plein » (logique pure)
// =============================================================================
// Extrait de Automatism::handleAlerts (automatism_display.cpp) dans le cadre du
// découpage de la god-class (chantier C4). Machine d'état pure : debounce (temps
// passé sous le seuil avant d'alerter), cooldown (délai min entre deux emails),
// hystérésis de sortie (niveau au-dessus de seuil+hyst, stable un certain temps).
//
// Aucune dépendance Arduino / config.h : paramétrée par valeurs déjà converties
// (seuils en mm, délais en secondes), donc testable nativement (g++).
// Les EFFETS DE BORD (envoi email, sauvegarde NVS, blink OLED, flags
// d'affichage) restent dans l'appelant : `evaluate()` ne fait QUE décider.
// =============================================================================

#include <cstdint>

namespace FloodAlert {

// État persistant de la machine anti-spam (détenu par Automatism, recopié ici).
struct State {
  bool inFlood = false;               // condition trop-plein actuellement notifiée
  uint32_t floodEnterSinceEpoch = 0;  // 1er instant (epoch s) passé sous limFlood
  uint32_t aboveResetSinceEpoch = 0;  // 1er instant (epoch s) au-dessus de limFlood+hyst
  uint32_t lastFloodEmailEpoch = 0;   // dernier email envoyé (epoch s) ; 0 = jamais
};

// Paramètres déjà convertis (seuils en mm — distance ultrason, délais en secondes).
// NB : « trop plein » = distance mesurée FAIBLE (le capteur mesure la distance à la
// surface), d'où la comparaison wlAqua < limFloodMm.
struct Params {
  uint16_t limFloodMm;        // cmToMm(limFlood) : sous cette distance = trop plein
  uint16_t resetThresholdMm;  // cmToMm(limFlood + hystCm) : seuil d'hystérésis de sortie
  uint32_t debounceSec;       // floodDebounceMin * 60 : temps min sous seuil avant alerte
  uint32_t cooldownSec;       // floodCooldownMin * 60 : délai min entre deux emails
  uint32_t resetStableSec;    // floodResetStableMin * 60 : temps stable au-dessus pour sortir
};

// Décision retournée à l'appelant (qui détient les effets de bord).
enum class Decision {
  None,            // rien à faire
  SendFloodEmail,  // conditions réunies → l'appelant tente l'envoi puis, si succès, markEmailSent()
  ExitFlood        // hystérésis stable → l'appelant réinitialise ses flags (_highAquaSent=false)
};

// Met à jour les horodatages (et `inFlood` en cas de sortie) de `st` selon le niveau
// courant, et retourne la décision. Pure : aucune E/S.
//   wlAquaMm  : distance mesurée (mm)
//   nowEpoch  : epoch courant (s)
//   mailEnabled : si false, aucune alerte n'est proposée (mais les horodatages évoluent)
inline Decision evaluate(State& st, uint16_t wlAquaMm, uint32_t nowEpoch,
                         const Params& p, bool mailEnabled) {
  if (wlAquaMm < p.limFloodMm) {
    if (st.floodEnterSinceEpoch == 0) st.floodEnterSinceEpoch = nowEpoch;
    st.aboveResetSinceEpoch = 0;
    if (!st.inFlood && mailEnabled) {
      uint32_t elapsedUnder = (nowEpoch >= st.floodEnterSinceEpoch)
                                  ? (nowEpoch - st.floodEnterSinceEpoch) : 0;
      bool debounceOk = elapsedUnder >= p.debounceSec;
      bool cooldownOk = (st.lastFloodEmailEpoch == 0) ||
                        ((nowEpoch - st.lastFloodEmailEpoch) >= p.cooldownSec);
      if (debounceOk && cooldownOk) {
        return Decision::SendFloodEmail;
      }
    }
    return Decision::None;
  }
  // Niveau au-dessus de limFlood : candidat à la sortie de l'état trop-plein.
  st.floodEnterSinceEpoch = 0;
  if (wlAquaMm >= p.resetThresholdMm) {
    if (st.aboveResetSinceEpoch == 0) st.aboveResetSinceEpoch = nowEpoch;
    uint32_t elapsedAbove = (nowEpoch >= st.aboveResetSinceEpoch)
                                ? (nowEpoch - st.aboveResetSinceEpoch) : 0;
    if (elapsedAbove >= p.resetStableSec) {
      st.inFlood = false;
      return Decision::ExitFlood;
    }
  } else {
    st.aboveResetSinceEpoch = 0;
  }
  return Decision::None;
}

// À appeler après un envoi d'email réussi : arme l'anti-spam (entre en flood).
inline void markEmailSent(State& st, uint32_t nowEpoch) {
  st.inFlood = true;
  st.lastFloodEmailEpoch = nowEpoch;
}

}  // namespace FloodAlert
