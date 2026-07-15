#pragma once
// Harnais OTA périodique + écran OLED mutualisé — extrait verbatim de
// n3pp/msp main.cpp (tranche T4.2 du chantier core shared).
//
// Répartition des responsabilités :
//  - le firmware garde la propriété des états persistants (compteur cumulé en
//    RTC_DATA_ATTR) et passe des pointeurs dans la config ;
//  - les buffers d'affichage historiquement module-static (versions courante /
//    distante, dernier pourcentage rendu) sont encapsulés dans N3OtaUiContext ;
//  - la cadence (due / restant / cumul saturant) délègue à la logique pure
//    OtaPeriodic (n3_common/n3_ota_periodic.h), testée en natif.
#include <Arduino.h>
#include <Adafruit_SSD1306.h>
#include "n3_ota.h"
#include "n3_ota_periodic.h"

struct N3OtaUiConfig {
  const char* title;            // 1re ligne de l'écran OTA ("N3PP OTA", "MSP OTA")
  const char* prodMetadataUrl;  // metadata.json du canal production
  const char* testMetadataUrl;  // metadata.json du canal test
  bool useTestUrl;              // true en TEST_MODE (résolu à la compile par le firmware)
  const char* firmwareVersion;  // FIRMWARE_VERSION du firmware appelant
  Adafruit_SSD1306* display;    // écran OLED (nullptr = aucun affichage)
  const bool* displayOk;        // flag d'init écran du firmware (nullptr = jamais prêt)
  uint32_t intervalSeconds;     // cadence entre checks (OtaPeriodic::kDefaultIntervalSeconds)
  uint32_t* elapsedSecondsRtc;  // compteur cumulé, RTC_DATA_ATTR possédé par le firmware
};

struct N3OtaUiContext {
  N3OtaUiConfig cfg;
  char currentVersion[16];   // version locale affichée (renseignée au start callback)
  char remoteVersion[16];    // version distante affichée
  uint8_t displayedPercent;  // dernier pourcentage rendu (255 = aucun)
};

// Initialise le contexte : copie la config et remet l'état d'affichage à zéro.
void n3OtaUiInit(N3OtaUiContext& ctx, const N3OtaUiConfig& cfg);

// Check OTA immédiat, hors cadence (ex. avant un reset distant). Retourne la
// valeur de n3OtaCheck (true = MAJ tentée ; le succès reboote la carte).
bool n3OtaUiCheckNow(N3OtaUiContext& ctx);

// Check périodique : ne déclenche que si le cumul atteint l'intervalle, puis
// remet le compteur à zéro. Logs série identiques aux versions historiques.
void n3OtaUiMaybePeriodicCheck(N3OtaUiContext& ctx, const char* reason);

// Cumule le temps écoulé (sommeil et/ou éveil) vers le compteur, saturé à
// l'intervalle. Ignore les durées nulles ou négatives.
void n3OtaUiAccumulateElapsed(N3OtaUiContext& ctx, int elapsedSeconds);
