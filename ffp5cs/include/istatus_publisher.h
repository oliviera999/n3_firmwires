#pragma once
// =============================================================================
// FFP5CS — Interface de publication d'état (inversion de dépendance, chantier C4)
// =============================================================================
// `IStatusPublisher` abstrait le SEUL point de sortie réseau consommé par les
// orchestrateurs de la god-class `Automatism` qui ont besoin de pousser un
// instantané d'état vers le serveur : `Automatism::sendFullUpdate(...)`.
//
// But : découpler l'orchestration (ex. fin de cycle nourrissage) de la machinerie
// concrète de `sendFullUpdate` (qui tire AutomatismSync / WebClient / POST réseau),
// afin d'injecter un FAUX publisher dans les tests natifs et tester le câblage
// (quelles paires `extraPairs` partent, dans quel cas) sans matériel ni réseau.
//
// Surface MINIMALE : une seule méthode. En-tête volontairement LÉGER (pas de
// config.h / Arduino) : `SensorReadings` est seulement déclaré (paramètre par
// référence). `Automatism` l'implémente via un mince adaptateur (cf. automatism.h)
// qui forwarde vers son `sendFullUpdate(...)` existant (catégorie POST par défaut).
// =============================================================================

struct SensorReadings;  // déclaration anticipée (publish prend const SensorReadings&)

class IStatusPublisher {
 public:
  virtual ~IStatusPublisher() = default;

  // Pousse un instantané d'état vers le serveur. `extraPairs` = paires
  // « clé=valeur&… » supplémentaires (ex. évènement nourrissage), ou nullptr.
  // Renvoie true ssi la publication a été acceptée (sync distant OK).
  virtual bool publish(const SensorReadings& r, const char* extraPairs) = 0;
};
