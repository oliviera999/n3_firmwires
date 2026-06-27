#include "pgl_sensor_fusion.h"

// Voir pgl_sensor_fusion.h pour la strategie (roles capteurs, anti-double-
// comptage, robustesse sous-ensemble). Fonction pure, sans etat ni Arduino.

namespace {
// recent : true si l'evenement (edge) a eu lieu dans la fenetre win. 0 = jamais.
inline bool recent(uint32_t edge, uint32_t now, uint32_t win) {
  return (edge != 0) && ((now - edge) <= win);
}
}  // namespace

PglFusionResult pglFuseDetection(const PglFusionInput& in) {
  const bool hasIR = (in.presentMask & PGL_SENS_IR) != 0;
  const bool hasUS = (in.presentMask & PGL_SENS_US) != 0;
  const bool hasPIR = (in.presentMask & PGL_SENS_PIR) != 0;
  const bool hasCounter = hasIR || hasUS;

  PglFusionResult result = {false, 0, false};

  if (hasCounter) {
    const bool counterTriggered =
        (hasIR && in.irTriggered) || (hasUS && in.usTriggered);
    if (!counterTriggered) {
      return result;  // {false, 0, false}
    }

    uint8_t mask = 0;
    if (hasIR && in.irTriggered) mask |= PGL_SENS_IR;
    if (hasUS && in.usTriggered) mask |= PGL_SENS_US;

    bool corroborated = false;
    if ((mask & PGL_SENS_IR) && (mask & PGL_SENS_US)) {
      // Les deux capteurs de comptage ont declenche ce poll.
      corroborated = true;
    } else if (hasIR && hasUS) {
      // Un seul a declenche ce poll ; l'autre est-il recent (fenetre) ?
      if ((mask & PGL_SENS_IR) &&
          recent(in.lastUsEdgeMs, in.nowMs, in.corroborationWindowMs)) {
        corroborated = true;
      }
      if ((mask & PGL_SENS_US) &&
          recent(in.lastIrEdgeMs, in.nowMs, in.corroborationWindowMs)) {
        corroborated = true;
      }
    }

    bool pirConfirmed = false;
    if (hasPIR) {
      pirConfirmed =
          in.pirTriggered || recent(in.lastPirEdgeMs, in.nowMs, in.pirPresenceWindowMs);
      if (pirConfirmed) mask |= PGL_SENS_PIR;
    }

    if (hasPIR && in.pirGatesCount && !pirConfirmed) {
      return PglFusionResult{false, 0, false};  // gate (defaut OFF)
    }

    if (pirConfirmed) {
      // Presence PIR + capteur de comptage = corrobore.
      corroborated = true;
    }

    return PglFusionResult{true, mask, corroborated};
  }

  // Aucun capteur de comptage (PIR seul ou rien).
  if (hasPIR && in.pirCountsWhenAlone && in.pirTriggered) {
    return PglFusionResult{true, PGL_SENS_PIR, false};  // faible confiance
  }
  return result;  // {false, 0, false}
}
