#include "n3_iot_registry.h"

void N3SensorRegistry::add(N3SensorDriver* sensor) {
  if (sensor && _sensors.size() < N3_IOT_MAX_SENSORS) {
    _sensors.push_back(sensor);
  }
}

size_t N3SensorRegistry::readAll(N3DataField* buf, size_t maxCount) {
  size_t n = 0;
  for (auto* s : _sensors) {
    if (n >= maxCount) break;
    buf[n].name = s->getName();
    buf[n].value = s->readValue();
    n++;
  }
  return n;
}
