#pragma once

#include <Arduino.h>
#include "system_sensors.h"

class Automatism;

struct AutomatismRuntimeContext {
  Automatism& core;
  const SensorReadings& readings;
  uint32_t nowMs;
};


