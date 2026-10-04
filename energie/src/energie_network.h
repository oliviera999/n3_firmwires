#pragma once
/**
 * Banc energie — reseau : WiFi non bloquant (n3_wifi), NTP (n3_time),
 * POST serveur (n3_data, HMAC), mail d'alerte (n3_mail), OTA (n3_ota_ui).
 */

#include <Arduino.h>

#include "energie_sensors.h"

struct EnergieNetStatus {
  bool wifiConnected;
  int rssi;
  int lastPostCode;        // dernier code HTTP (ou < 0 erreur reseau), 0 = jamais
  uint32_t postOk;
  uint32_t postFail;
  bool timeSynced;
};

void networkBegin();
/** A appeler a chaque tour de loop() : avance la session WiFi, NTP, OTA. */
void networkLoop(uint32_t nowMs);
/** Envoie une fenetre de mesures. Retourne le code HTTP (< 0 si reseau KO). */
int networkPost(const EnergieWindow& w);
/** Mail d'alerte batterie basse (ou de test). Retourne true si envoye. */
bool networkSendMail(const char* subject, const char* body);
/** Verification OTA immediate (commande console « ota »). */
void networkOtaCheckNow();
const EnergieNetStatus& networkStatus();
