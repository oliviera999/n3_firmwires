#pragma once
// =============================================================================
// FFP5CS — Orchestrateur de régulation chauffage (effets de bord, testable nativement)
// =============================================================================
// Premier ORCHESTRATEUR à effets de bord extrait de la god-class (chantier C4), rendu
// possible par les interfaces `IActuators` + `IMailer`. Contrairement aux modules de
// *décision* purs (HeaterControl…), celui-ci EXÉCUTE les effets — mais uniquement via
// des interfaces injectables, donc **testable nativement** avec FakeActuators/FakeMailer.
//
// Extrait du bloc chauffage de Automatism::handleAlerts (automatism_display.cpp).
// La décision reste déléguée à HeaterControl (pur). Le seul effet de bord NON couvert
// par une interface (le blink OLED « mail ») est signalé par la valeur de retour :
// `run()` renvoie true si un mail a été envoyé, et l'appelant déclenche alors le blink.
//
// Dépend seulement de iactuators.h / imailer.h / heater_hysteresis.h + <cstdio> -> g++.
// =============================================================================

#include <cstdio>
#include "iactuators.h"
#include "imailer.h"
#include "heater_hysteresis.h"

namespace HeaterOrchestrator {

// Applique la régulation : décide (HeaterControl) puis exécute (relais + mail).
// Met à jour `heaterPrevState`. Renvoie true ssi un mail a été envoyé (-> blink côté appelant).
// Parité avec l'ancien bloc inline : relais commuté quelle que soit `mailEnabled` ; mail
// (et donc retour true) uniquement si `mailEnabled` ; message « Temp eau: X.X°C ».
inline bool run(IActuators& acts, IMailer& mailer, bool& heaterPrevState,
                float tempWater, float thresholdC, float hysteresisC,
                bool mailEnabled, const char* email) {
  const HeaterControl::Decision d =
      HeaterControl::evaluate(tempWater, thresholdC, hysteresisC, heaterPrevState);

  if (d == HeaterControl::Decision::TurnOn) {
    acts.startHeater();
    heaterPrevState = true;
    if (mailEnabled) {
      char msg[64];
      snprintf(msg, sizeof(msg), "Temp eau: %.1f°C", tempWater);
      mailer.sendAlert("Chauffage ON", msg, email);
      return true;
    }
  } else if (d == HeaterControl::Decision::TurnOff) {
    acts.stopHeater();
    heaterPrevState = false;
    if (mailEnabled) {
      char msg[64];
      snprintf(msg, sizeof(msg), "Temp eau: %.1f°C", tempWater);
      mailer.sendAlert("Chauffage OFF", msg, email);
      return true;
    }
  }
  return false;
}

}  // namespace HeaterOrchestrator
