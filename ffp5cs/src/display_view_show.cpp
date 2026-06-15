// display_view_show.cpp — écrans publics de DisplayView (showMain/Variables/Server
// Vars/Diagnostic/OtaProgress/SleepReason/SleepInfo/overlays/forceEndSplash).
// Extrait de display_view.cpp (audit : découpe). Méthodes membres, comportement identique.
#include "display_view.h"
#include "config.h"
#include "pins.h"
#include "i2c_bus.h"
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
#include "rom/ets_sys.h"
#endif
#include <WiFi.h>
#include "wifi_manager.h"  // Pour WiFiHelpers
#include <algorithm>
#include <string.h>

void DisplayView::showMain(float tempEau, float tempAir, float humidite, uint16_t aquaLvl,
                           uint16_t tankLvl, uint16_t potaLvl, uint16_t lumi, const char* timeStr) {
  if (!_present || splashActive() || isLocked()) return;
  
  // Vérifier si les valeurs ont changé significativement
  bool valuesChanged = _cache.main().update(tempEau,
                                           tempAir,
                                           humidite,
                                           aquaLvl,
                                           tankLvl,
                                           potaLvl,
                                           lumi,
                                           timeStr);
  
  // Si les valeurs n'ont pas changé et qu'on n'est pas en mode immédiat, on peut sauter l'affichage
  if (!valuesChanged && !_immediateMode && !_updateMode) {
    return;
  }
  
  // Protection contre les appels simultanés (sauf si on est déjà en mode affichage)
  if (_isDisplaying && !_updateMode) return;
  
  if (_updateMode) {
    _isDisplaying = true;
  }
  
  // Nettoyage complet avant l'affichage principal pour éviter les superpositions
  // Mais préserver l'overlay OTA s'il est actif
  if (!_otaOverlayActive) {
    clear();
  } else {
    // Si l'overlay OTA est actif, on efface seulement la zone principale
    // en préservant la zone de l'overlay (x >= 100, y < 8)
    _disp.fillRect(0, 0, 100, 8, BLACK);  // Zone gauche de la première ligne
    _disp.fillRect(0, 8, 128, 56, BLACK); // Reste de l'écran
  }
  
  bool wifiConnected = (WiFi.status() == WL_CONNECTED);
  // Copier les valeurs WiFi dans des buffers car les String temporaires seraient détruites
  char stationSsidBuf[33], stationIpBuf[16], apSsidBuf[33], apIpBuf[16];
  if (wifiConnected) {
    WiFiHelpers::getSSID(stationSsidBuf, sizeof(stationSsidBuf));
    WiFiHelpers::getIPString(stationIpBuf, sizeof(stationIpBuf));
  } else {
    stationSsidBuf[0] = '\0';
    stationIpBuf[0] = '\0';
  }
  WiFiHelpers::getAPSSID(apSsidBuf, sizeof(apSsidBuf));
  WiFiHelpers::getAPIPString(apIpBuf, sizeof(apIpBuf));

  renderMainScreen(tempEau, tempAir, humidite,
                   aquaLvl, tankLvl, potaLvl,
                   lumi, timeStr, wifiConnected,
                   stationSsidBuf, stationIpBuf,
                   apSsidBuf, apIpBuf);
  
  // Mode immédiat pour les changements de valeurs, mode optimisé sinon
  if (_immediateMode || valuesChanged) {
    flush();
  } else if (_updateMode) {
    _needsFlush = true;
  }
  
  if (_updateMode) {
    _isDisplaying = false;
  }
}

void DisplayView::showVariables(bool pumpAqua,
                                bool pumpTank,
                                bool heater,
                                bool light,
                                uint8_t hMat,
                                uint8_t hMid,
                                uint8_t hSoir,
                                uint16_t tPetits,
                                uint16_t tGros,
                                uint16_t thAq,
                                uint16_t thTank,
                                float thHeat,
                                uint16_t limFlood) {
  if (!_present || splashActive() || isLocked()) return;
  
  // Vérifier si les valeurs ont changé (optimisation avec cache)
  bool valuesChanged = _cache.variables().update(pumpAqua,
                                                 pumpTank,
                                                 heater,
                                                 light,
                                                 hMat,
                                                 hMid,
                                                 hSoir,
                                                 tPetits,
                                                 tGros,
                                                 thAq,
                                                 thTank,
                                                 thHeat,
                                                 limFlood);
  
  // Si les valeurs n'ont pas changé et qu'on n'est pas en mode immédiat, on peut sauter l'affichage
  if (!valuesChanged && !_immediateMode && !_updateMode) {
    return;
  }
  
  clear();
  renderVariables(pumpAqua, pumpTank, heater, light,
                  hMat, hMid, hSoir,
                  tPetits, tGros,
                  thAq, thTank, thHeat,
                  limFlood);
  
  // Mode immédiat pour les changements de valeurs, mode optimisé sinon
  if (_immediateMode || valuesChanged) {
    flush();
  } else if (_updateMode) {
    _needsFlush = true;
  }
}

void DisplayView::showDiagnostic(const char* msg) {
  if (!_present || splashActive()) return;

  const uint8_t maxLines = 6; // header ("Diag:") on line 0 + 5 messages (1..5)

  // Verrouille l'écran pour éviter la superposition avec l'affichage principal
  // Chaque message prolonge le verrou ~0,8s (réduit pour accélérer l'init)
  lockScreen(800);

  // Si aucune ligne diag n'a encore été affichée, initialisation de l'en-tête
  if (_diagLine == 0) {
    clear();
    _disp.setTextSize(1);
    _disp.setCursor(0, 0);
    _disp.println("Diag:");
    _diagLine = 1; // prochaine ligne disponible
  }

  // Si on dépasse le nombre de lignes disponible, on réinitialise l'écran diag
  if (_diagLine > maxLines) {
    clear();
    _disp.setTextSize(1);
    _disp.setCursor(0, 0);
    _disp.println("Diag:");
    _diagLine = 1;
  }

  // Positionne le curseur sur la ligne courante et écrit le message avec clipping horizontal
  {
    char line[128];
    utf8ToCp437(msg, line, sizeof(line));
    appendDiagnosticLine(line, _diagLine);
  }
  ++_diagLine;

  if (!_updateMode) flush();
  else _needsFlush = true;
}

void DisplayView::showServerVars(bool pumpAqua,bool pumpTank,bool heater,bool light,
                                 uint8_t hMat,uint8_t hMid,uint8_t hSoir,
                                 uint16_t tPetits,uint16_t tGros,
                                 uint16_t thAq,uint16_t thTank,float thHeat,
                                 uint16_t tRemp,uint16_t limFlood,
                                 bool wakeUp,uint16_t freqWake){
  if(!_present || isLocked()) return;
  clear();
  renderServerVars(pumpAqua, pumpTank, heater, light,
                   hMat, hMid, hSoir,
                   tPetits, tGros,
                   thAq, thTank, thHeat,
                   tRemp, limFlood,
                   wakeUp, freqWake);
  if (!_updateMode) flush();
  else _needsFlush = true;
} 

void DisplayView::showOtaProgress(uint8_t percent, const char* fromLabel, const char* toLabel, const char* phase){
  if(!_present || splashActive() || _isDisplaying) return;

  DisplaySession session(*this, true, 0, true);

  _disp.clearDisplay();
  resetMainCache();
  resetStatusCache();

  // En-tête (phase claire)
  {
    char headerBuf[128];
    if (phase && *phase) {
      char phaseBuf[64];
utf8ToCp437(phase, phaseBuf, sizeof(phaseBuf));
      snprintf(headerBuf, sizeof(headerBuf), "OTA: %s", phaseBuf);
    } else {
      snprintf(headerBuf, sizeof(headerBuf), "OTA: ");
    }
    printClipped(0, 0, headerBuf, 1);
  }

  // Lignes partitions
  if (fromLabel && *fromLabel) {
    char fromBuf[128], labelBuf[64];
utf8ToCp437(fromLabel, labelBuf, sizeof(labelBuf));
    snprintf(fromBuf, sizeof(fromBuf), "De: %s", labelBuf);
    printClipped(0, 10, fromBuf, 1);
  }
  if (toLabel && *toLabel) {
    char toBuf[128], labelBuf[64];
utf8ToCp437(toLabel, labelBuf, sizeof(labelBuf));
    snprintf(toBuf, sizeof(toBuf), "Vers: %s", labelBuf);
    printClipped(0, 18, toBuf, 1);
  }

  // Pourcentage centré en grand
  char pbuf[8];
  snprintf(pbuf, sizeof(pbuf), "%u%%", (unsigned)percent);
  _disp.setTextSize(3);
  _disp.setCursor(0, 24);
  printClipped(0, 24, pbuf, 2);

  // Barre de progression (largeur 120px, hauteur 10px) sous le pourcentage
  const int barX = 4;
  const int barY = 48;
  const int barW = 120;
  const int barH = 10;
  _disp.drawRect(barX, barY, barW, barH, WHITE);
  int fillW = (int)((percent > DisplayConfig::PERCENTAGE_MAX ? DisplayConfig::PERCENTAGE_MAX : percent) * barW / DisplayConfig::PERCENTAGE_MAX);
  if (fillW > 0) {
    _disp.fillRect(barX + 1, barY + 1, max(0, fillW - 2), barH - 2, WHITE);
  }

  _disp.display();
  _needsFlush = false;
}

void DisplayView::showOtaProgressEx(uint8_t percent, const char* fromLabel, const char* toLabel,
                         const char* phase, const char* currentVersion,
                         const char* newVersion, const char* hostLabel){
  if(!_present || splashActive() || _isDisplaying || isLocked()) return;

  DisplaySession session(*this, true, 0, true);

  _disp.clearDisplay();
  resetMainCache();
  resetStatusCache();

  // En-tête
  {
    char headerBuf[128];
    if (phase && *phase) {
      char phaseBuf[64];
utf8ToCp437(phase, phaseBuf, sizeof(phaseBuf));
      snprintf(headerBuf, sizeof(headerBuf), "OTA %s", phaseBuf);
    } else {
      snprintf(headerBuf, sizeof(headerBuf), "OTA ");
    }
    printClipped(0, 0, headerBuf, 1);
  }
  // Hote ou WiFi SSID
  if (hostLabel && *hostLabel) {
    char hostBuf[128], labelBuf[64];
utf8ToCp437(hostLabel, labelBuf, sizeof(labelBuf));
    snprintf(hostBuf, sizeof(hostBuf), "Hote: %s", labelBuf);
    printClipped(0, 8, hostBuf, 1);
  }
  // Versions
  if (currentVersion && *currentVersion) {
    char actBuf[32];
    snprintf(actBuf, sizeof(actBuf), "Act: v%s", currentVersion);
    printClipped(0, 16, actBuf, 1);
  }
  if (newVersion && *newVersion) {
    char nvBuf[32];
    snprintf(nvBuf, sizeof(nvBuf), "Nv: v%s", newVersion);
    printClipped(72, 16, nvBuf, 1);
  }
  // Partitions
  if (fromLabel && *fromLabel) {
    char fromBuf[128], labelBuf[64];
utf8ToCp437(fromLabel, labelBuf, sizeof(labelBuf));
    snprintf(fromBuf, sizeof(fromBuf), "De:%s", labelBuf);
    printClipped(0, 24, fromBuf, 1);
  }
  if (toLabel && *toLabel) {
    char toBuf[128], labelBuf[64];
utf8ToCp437(toLabel, labelBuf, sizeof(labelBuf));
    snprintf(toBuf, sizeof(toBuf), "Vers:%s", labelBuf);
    printClipped(64, 24, toBuf, 1);
  }

  // Barre de progression
  const int barX = 4, barY = 40, barW = 120, barH = 10;
  _disp.drawRect(barX, barY, barW, barH, WHITE);
  int fillW = (int)((percent > DisplayConfig::PERCENTAGE_MAX ? DisplayConfig::PERCENTAGE_MAX : percent) * barW / DisplayConfig::PERCENTAGE_MAX);
  if (fillW > 0) {
    _disp.fillRect(barX + 1, barY + 1, max(0, fillW - 2), barH - 2, WHITE);
  }
  // Pourcentage
  char pbuf[8];
  snprintf(pbuf, sizeof(pbuf), "%u%%", (unsigned)percent);
  printClipped(0, 52, pbuf, 1);

  _disp.display();
  _needsFlush = false;
}

void DisplayView::showSleepReason(const char* cause, const char* detailLine1, const char* detailLine2,
                                  uint16_t lockMs, bool mailBlink){
  if(!_present || splashActive()) return;
  DisplaySession session(*this, true, lockMs);
  _disp.clearDisplay();
  resetMainCache();
  resetStatusCache();
  printClipped(0, 0, "Veille", 1);
  if (cause && *cause) {
    char causeBuf[128];
utf8ToCp437(cause, causeBuf, sizeof(causeBuf));
    printClipped(0, 10, causeBuf, 1);
  }
  if (detailLine1 && *detailLine1) {
    char detail1Buf[128];
utf8ToCp437(detailLine1, detail1Buf, sizeof(detail1Buf));
    printClipped(0, 20, detail1Buf, 1);
  }
  if (detailLine2 && *detailLine2) {
    char detail2Buf[128];
utf8ToCp437(detailLine2, detail2Buf, sizeof(detail2Buf));
    printClipped(0, 30, detail2Buf, 1);
  }
  // Statut bar with mail icon if blinking requested (force draw even when locked)
  drawStatusEx(0, 0, -127, mailBlink, 0, 0, true);
  _disp.display();
}

void DisplayView::showSleepInfo(const char* reason, const char* detail1, const char* detail2, uint32_t lockMs) {
  if(!_present || splashActive() || _isDisplaying) return;

  DisplaySession session(*this, true, lockMs, true);

  _disp.clearDisplay();
  resetMainCache();
  resetStatusCache();

  // Titre
  printClipped(0, 0, "Veille", 1);
  // Raison explicite
  if (reason && *reason) {
    char reasonBuf[128], labelBuf[64];
utf8ToCp437(reason, labelBuf, sizeof(labelBuf));
    snprintf(reasonBuf, sizeof(reasonBuf), "Raison: %s", labelBuf);
    printClipped(0, 10, reasonBuf, 1);
  }
  // Détails optionnels
  if (detail1 && *detail1) {
    char detail1Buf[128];
utf8ToCp437(detail1, detail1Buf, sizeof(detail1Buf));
    printClipped(0, 20, detail1Buf, 1);
  }
  if (detail2 && *detail2) {
    char detail2Buf[128];
utf8ToCp437(detail2, detail2Buf, sizeof(detail2Buf));
    printClipped(0, 28, detail2Buf, 1);
  }

  // Icône lune simple
  // Note: drawCircle et fillCircle ne sont pas disponibles dans cette version d'Adafruit_SSD1306
  // Utilisation d'alternatives ou suppression si non critique
  // _disp.drawCircle(118, 8, 6, WHITE);
  // _disp.fillCircle(121, 8, 6, BLACK);

  _disp.display();
  _needsFlush = false;
}

void DisplayView::showOtaProgressOverlay(uint8_t percent) {
  if (!_present || splashActive()) return;
  
  // Activer l'overlay OTA
  _otaOverlayActive = true;
  _lastOtaPercent = percent;
  
  // Position en haut à droite de l'écran
  int x = DisplayConfig::OTA_OVERLAY_X_POS;  // Position X (haut droite)
  int y = DisplayConfig::OTA_OVERLAY_Y_POS;    // Position Y (première ligne)
  
  // Effacer la zone de l'overlay (zone de 28px de large pour "OTA: 100%")
  _disp.fillRect(x, y, DisplayConfig::OTA_OVERLAY_WIDTH, DisplayConfig::OTA_OVERLAY_HEIGHT, BLACK);
  
  // Afficher le pourcentage OTA
  char percentStr[12];
  snprintf(percentStr, sizeof(percentStr), "OTA: %u%%", percent);
  printClipped(x, y, percentStr, 1);
  
  // Flush immédiat pour l'overlay
  _disp.display();
}

void DisplayView::hideOtaProgressOverlay() {
  if (!_present) return;
  
  // Désactiver l'overlay OTA
  _otaOverlayActive = false;
  _lastOtaPercent = 0;
  
  // Effacer la zone de l'overlay
  _disp.fillRect(DisplayConfig::OTA_OVERLAY_X_POS, DisplayConfig::OTA_OVERLAY_Y_POS, DisplayConfig::OTA_OVERLAY_WIDTH, DisplayConfig::OTA_OVERLAY_HEIGHT, BLACK);
  
  // Flush immédiat
  _disp.display();
}

void DisplayView::forceEndSplash() {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
  ets_printf("[OLED] Force fin splash\n");
#elif (defined(ENABLE_SERIAL_MONITOR) && (ENABLE_SERIAL_MONITOR == 1)) || !defined(PROFILE_PROD)
  Serial.println("[OLED] Force fin du splash screen");
#endif
  _splashUntil = 0;
  // Réinitialiser les caches pour forcer un redessin complet
  resetMainCache();
  resetStatusCache();
}

