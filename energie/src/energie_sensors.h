#pragma once
/**
 * Banc energie — voies INA226, accumulation, etat de charge batterie, alerte.
 *
 * Toute la logique (calibration, integration, SoC, alerte) vient de la brique
 * partagee shared/n3_power ; ce module ne fait que l'orchestrer pour 3 voies.
 */

#include <Arduino.h>

#include "energie_config.h"
#include "n3_ina226.h"
#include "n3_power_battery.h"
#include "n3_power_energy.h"

// Bits du champ InaStatus (contrat serveur docs/API_ENERGIE.md) :
//  bit i      (i = voie) : INA present et configure
//  bit 4 + i             : saturation shunt vue pendant la fenetre
//  bit 8 + i             : re-initialisation (config perdue) pendant la fenetre
#define ENERGIE_STATUS_PRESENT_SHIFT 0
#define ENERGIE_STATUS_SATURATED_SHIFT 4
#define ENERGIE_STATUS_REINIT_SHIFT 8

struct EnergieChannelSettings {
  float shuntOhm;
  float maxCurrentA;
  float gain;
  bool invert;
};

struct EnergieChannel {
  const char* name;   // libelle court (console, OLED)
  const char* field;  // prefixe des champs POST (Panneau / Batterie / Conso)
  uint8_t address;
  EnergieChannelSettings settings;
  N3Ina226Device dev;
  N3Power::ChannelAccumulator acc{ENERGIE_MAX_GAP_MS};
  bool present = false;      // probe OK (ID TI INA226)
  bool configured = false;   // calibration ecrite
  bool lastOk = false;       // derniere lecture valide
  N3Ina226Reading last = {};
  bool windowSaturated = false;
  bool windowReinit = false;
  uint32_t reinitCount = 0;
};

/** Fenetre prete a envoyer (cf. sensorsTakeWindow). */
struct EnergieWindow {
  bool valid[ENERGIE_CH_COUNT];
  N3Power::WindowSnapshot ch[ENERGIE_CH_COUNT];
  float batterieSoc;      // %, NAN si inconnu
  double batterieAh;      // charge nette integree depuis le boot / « reset »
  float batterieVadc;     // V (pont diviseur carte), NAN si desactive
  uint16_t inaStatus;
  uint32_t i2cErrors;     // cumul toutes voies
};

extern EnergieChannel g_channels[ENERGIE_CH_COUNT];
extern N3Power::LeadAcidSoc g_batterySoc;
extern N3Power::LowBatteryAlert g_lowBatteryAlert;

/** Charge les surcharges NVS, sonde et configure les 3 INA. Wire doit etre pret. */
void sensorsBegin();
/** Mesure 1 Hz : lecture, re-init si config perdue, accumulation, SoC, alerte. */
void sensorsSample(uint32_t nowMs);
/** true (une fois) quand l'alerte batterie basse doit partir. */
bool sensorsConsumeLowBatteryAlert();
/** Instantane de la fenetre courante puis remise a zero. */
EnergieWindow sensorsTakeWindow();

/** (Re)configure une voie avec ses reglages courants. */
bool sensorsSetupChannel(uint8_t ch);
/** Reglages persistants (NVS) — appeles par la console. */
void sensorsSaveSettings(uint8_t ch);
void sensorsSetAveraging(uint16_t samples);
uint16_t sensorsAveraging();
/** Remet a zero les cumuls d'energie et la charge batterie integree. */
void sensorsResetCounters();
/** Impose le SoC batterie (persiste). */
void sensorsSeedSoc(float socPercent);
/** Sauvegarde periodique du SoC en NVS. */
void sensorsMaybeSaveSoc(uint32_t nowMs);
/** Efface les surcharges NVS (retour aux valeurs de energie_config.h). */
void sensorsFactoryReset();
/** Lecture ADC du pont diviseur batterie (NAN si desactive). */
float sensorsReadVadc();
/** Index de voie depuis un argument console (0/1/2 ou pan/bat/con). -1 si inconnu. */
int sensorsChannelFromArg(const char* arg);
/** Compteur de redemarrages (NVS). */
uint32_t sensorsBootCount();
