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

/**
 * Telemetrie instantanee jointe au heartbeat serveur (monitoring de flotte).
 * Toutes ces valeurs proviennent de getters deja existants (aucun nouvel etat
 * persistant) : files d'attente d'evenements, mode de stockage, batterie, mode
 * de detection actif. Le module reseau ne depend ainsi ni du compteur ni de la
 * detection : main.cpp collecte ces valeurs simples et les passe au heartbeat.
 */
struct PglHeartbeatTelemetry {
  uint16_t pending = 0;          // evenements en attente (RAM/NVS courant)
  uint32_t journalPending = 0;   // evenements en attente dans le journal SD
  uint16_t nvsPending = 0;       // evenements persistes en NVS non acquittes
  bool sdOk = false;             // true si stockage en mode journal SD
  int16_t batteryMilliVolt = 0;  // tension batterie instantanee (mV)
  uint8_t sensorMode = 0;        // mode de detection actif (PglSensorMode)
};

/** Dernier etat des echanges HTTP avec iot.olution.info (post-data / heartbeat). */
struct PglServerCommStatus {
  int lastPostHttp = 0;
  int lastHeartbeatHttp = 0;
  uint32_t lastPostMs = 0;
  uint32_t lastHeartbeatMs = 0;
};
