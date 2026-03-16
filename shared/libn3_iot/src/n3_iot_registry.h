#pragma once

#include "n3_iot_sensors.h"
#include "n3_data.h"
#include <vector>

#define N3_IOT_MAX_SENSORS 24
#define N3_IOT_MAX_FIELDS 40

/**
 * Registre de capteurs et builder de champs POST.
 * Permet d'ajouter des capteurs, de les lire en lot, et de construire
 * le tableau N3DataField pour n3DataPost.
 */
class N3SensorRegistry {
public:
  void add(N3SensorDriver* sensor);

  /**
   * Lit tous les capteurs et remplit buf avec les paires (name, value).
   * Retourne le nombre de champs ecrits.
   */
  size_t readAll(N3DataField* buf, size_t maxCount);

  size_t getSensorCount() const { return _sensors.size(); }

private:
  std::vector<N3SensorDriver*> _sensors;
};
