#pragma once

#include <Arduino.h>

enum class PglSensorMode : uint8_t {
  NONE = 0,
  IR = 1,
  ULTRASON = 2,
  TANDEM = 3
};

struct PglDetectionEvent {
  bool detected;
  PglSensorMode mode;
  bool tandemValidated;
};

struct PglStoredEvent {
  uint32_t epoch;
  uint16_t countDelta;
  uint8_t sensorMode;
  uint8_t tandemValidated;
  int16_t batteryMilliVolt;
  int16_t rssi;
  uint32_t eventId;  // identifiant monotone unique (0 = non assigné)
};

/** Dernier etat des echanges HTTP avec iot.olution.info (post-data / heartbeat). */
struct PglServerCommStatus {
  int lastPostHttp = 0;
  int lastHeartbeatHttp = 0;
  uint32_t lastPostMs = 0;
  uint32_t lastHeartbeatMs = 0;
};
