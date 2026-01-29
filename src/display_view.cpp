#include "display_view.h"
#include "config.h"
#include "pins.h"
#include "oled_logo.h"
#include <WiFi.h>
#include "wifi_manager.h"  // Pour WiFiHelpers
#include <algorithm>
#include <string.h>

// Utilitaires d'affichage (fusionnés depuis display_text_utils)
namespace {
  // Convertit UTF-8 en CP437 et écrit dans le buffer
  size_t utf8ToCp437(const char* input, char* output, size_t outputSize) {
    if (!input || !output || outputSize == 0) {
      if (output && outputSize > 0) {
        output[0] = '\0';
      }
      return 0;
    }
    
    size_t outIdx = 0;
    const uint8_t* s = reinterpret_cast<const uint8_t*>(input);
    
    while (*s && outIdx < outputSize - 1) {
      uint8_t c = *s++;
      if (c == 0xC3) {
        uint8_t n = *s;
        if (n) {
          ++s;
          char cp437_char = 0;
          switch (n) {
            case 0xA9: cp437_char = 0x82; break; // é
            case 0xA8: cp437_char = 0x8A; break; // è
            case 0xAA: cp437_char = 0x88; break; // ê
            case 0xA0: cp437_char = 0x85; break; // à
            case 0xA2: cp437_char = 0x83; break; // â
            case 0xB9: cp437_char = 0x97; break; // ù
            case 0xBB: cp437_char = 0x96; break; // û
            case 0xA7: cp437_char = 0x87; break; // ç
            case 0xB4: cp437_char = 0x93; break; // ô
            case 0xAF: cp437_char = 0x8B; break; // ï
            case 0xAE: cp437_char = 0x8C; break; // î
            case 0x89: cp437_char = 0x90; break; // É
            case 0x87: cp437_char = 0x80; break; // Ç
            default:
              continue; // Caractère non supporté, on ignore
          }
          if (cp437_char != 0) {
            output[outIdx++] = cp437_char;
          }
        }
      } else if ((c & 0xE0) == 0xC0) {
        if (*s) ++s;
      } else if ((c & 0xF0) == 0xE0) {
        if (*s) ++s;
        if (*s) ++s;
      } else if ((c & 0xF8) == 0xF0) {
        if (*s) ++s;
        if (*s) ++s;
        if (*s) ++s;
      } else {
        output[outIdx++] = char(c);
      }
    }
    
    output[outIdx] = '\0';
    return outIdx;
  }

  // Tronque un texte en place à maxChars caractères (ajoute "..." si nécessaire)
  void clipInPlace(char* text, size_t textSize, int maxChars) {
    if (!text || textSize == 0 || maxChars <= 0) {
      if (text && textSize > 0) {
        text[0] = '\0';
      }
      return;
    }
    
    size_t len = strlen(text);
    if (static_cast<int>(len) <= maxChars) {
      return;
    }
    
    if (maxChars >= 3) {
      size_t newLen = maxChars - 3;
      if (newLen < textSize - 1) {
        text[newLen] = '\0';
        strncat(text, "...", textSize - newLen - 1);
      }
    } else {
      if (maxChars < static_cast<int>(textSize)) {
        text[maxChars] = '\0';
      }
    }
  }

  uint8_t characterWidth(uint8_t size) {
    if (size == 0) return 6;
    return static_cast<uint8_t>(6 * size);
  }
}

// Texte protégé contre le dépassement horizontal (police 6x8 px par taille 1)
void DisplayView::printClipped(int16_t x, int16_t y, const char* text, uint8_t size) {
  if (!_present || !text) return;
  
  // CORRECTION v11.57: Suppression des vérifications I2C répétées
  // Les vérifications I2C sont faites uniquement lors de l'initialisation
  // pour éviter le spam d'erreurs lors des mises à jour fréquentes
  // Largeur max pixels
  const int16_t maxWidth = _disp.width();
  // Largeur d'un caractère: 6 px en taille 1 (5 + 1 espace)
  uint8_t charWidth = characterWidth(size);

  // Convertit en CP437 pour cohérence avec l'affichage
  char toPrint[256];
  utf8ToCp437(text, toPrint, sizeof(toPrint));
  // Calcule le nombre max de caractères qui rentrent sur la ligne
  int16_t available = maxWidth - x;
  if (available <= 0) return;
  int16_t maxChars = available / charWidth;
  if (maxChars <= 0) return;
  clipInPlace(toPrint, sizeof(toPrint), maxChars);

  _disp.setTextSize(size);
  _disp.setCursor(x, y);
  _disp.println(toPrint);
}

DisplayView::DisplaySession::DisplaySession(DisplayView& view, bool immediateMode, uint32_t lockMs, bool setDisplaying)
    : _view(view),
      _prevImmediate(view._immediateMode),
      _setDisplaying(setDisplaying) {
  if (lockMs > 0) {
    _view.lockScreen(lockMs);
  }
  if (immediateMode) {
    _view._immediateMode = true;
  }
  if (_setDisplaying) {
    _prevDisplaying = _view._isDisplaying;
    _view._isDisplaying = true;
  }
}

DisplayView::DisplaySession::~DisplaySession() {
  if (_setDisplaying) {
    _view._isDisplaying = _prevDisplaying;
  }
  _view._immediateMode = _prevImmediate;
}

// Nouvelles méthodes d'optimisation
void DisplayView::beginUpdate() {
  if (!_present) return;
  
  // CORRECTION v11.58: Suppression des appels répétés à _disp.begin()
  // beginUpdate() active simplement le mode mise à jour, pas de communication I2C
  _updateMode = true;
  _needsFlush = false;
}

void DisplayView::endUpdate() {
  if (!_present) return;
  
  // CORRECTION v11.58: Simplification de la logique de flush
  // Pas de boucle timeout, juste un flush simple si nécessaire
  _updateMode = false;
  if (_needsFlush) {
    _disp.display();
    _needsFlush = false;
  }
}

void DisplayView::setUpdateMode(bool immediate) {
  _immediateMode = immediate;
}

bool DisplayView::needsRefresh() const {
  return _needsFlush;
}

void DisplayView::resetStatusCache() {
  _cache.resetStatus();
}

void DisplayView::resetMainCache() {
  _cache.resetMain();
}

void DisplayView::resetVariablesCache() {
  _cache.resetVariables();
}

void DisplayView::forceClear() {
  if (!_present) return;
  
  // Force un nettoyage complet de l'écran
  _disp.clearDisplay();
  _disp.display();
  
  // Réinitialise tous les caches
  resetMainCache();
  resetStatusCache();
  resetVariablesCache();
  
  // Force le mode immédiat temporairement
  _immediateMode = true;
  _needsFlush = false;
}

void DisplayView::drawStatus(int8_t sendState, int8_t recvState, int8_t rssi, bool mailBlink,
                             int8_t tideDir, int diffValue) {
  if (!_present) return;
  if (isLocked()) return;

  // CORRECTION v11.57: Suppression des vérifications I2C répétées
  // Les vérifications I2C sont faites uniquement lors de l'initialisation
  // pour éviter le spam d'erreurs lors des mises à jour fréquentes

  // Vérification si les valeurs ont changé (optimisation)
  bool needsUpdate = _cache.status().update(sendState,
                                           recvState,
                                           rssi,
                                           mailBlink,
                                           tideDir,
                                           diffValue);
  
  if (!needsUpdate && !_updateMode) return; // Pas de changement, pas de mise à jour nécessaire

  StatusBarParams params{sendState,
                         recvState,
                         rssi,
                         mailBlink,
                         tideDir,
                         diffValue,
                         _otaOverlayActive,
                         _lastOtaPercent};
  renderStatusBar(params);
  
  // Marquer qu'un flush est nécessaire
  if (_updateMode) {
    _needsFlush = true;
  }
}

void DisplayView::drawStatusEx(int8_t sendState, int8_t recvState, int8_t rssi, bool mailBlink,
                               int8_t tideDir, int diffValue, bool force) {
  if (!_present) return;
  bool wasLocked = isLocked();
  if (wasLocked && !force) return;

  // Bypass change-check when forced to guarantee icon visibility
  bool needsUpdate = _cache.status().update(sendState,
                                           recvState,
                                           rssi,
                                           mailBlink,
                                           tideDir,
                                           diffValue,
                                           force);
  if (!needsUpdate && !_updateMode) return;

  StatusBarParams params{sendState,
                         recvState,
                         rssi,
                         mailBlink,
                         tideDir,
                         diffValue,
                         _otaOverlayActive,
                         _lastOtaPercent};
  renderStatusBar(params);

  if (_updateMode) _needsFlush = true;
}

void DisplayView::showCountdown(const char* label, uint16_t secLeft, bool isManual){
  if(!_present || splashActive() || _isDisplaying || isLocked()) return;
  DisplaySession session(*this, true, 0, true);

  _disp.clearDisplay();
  resetMainCache();
  resetStatusCache();

  renderCountdown(label, secLeft, isManual);
  
  // Force le flush immédiat pour les décomptes (toujours fluide)
  _disp.display();
  _needsFlush = false;
}

void DisplayView::showFeedingCountdown(const char* fishType, const char* phase, uint16_t secLeft, bool isManual){
  if(!_present || splashActive() || _isDisplaying || isLocked()) return;
  DisplaySession session(*this, true, 0, true);

  _disp.clearDisplay();
  resetMainCache();
  resetStatusCache();

  renderFeedingCountdown(fishType, phase, secLeft, isManual);
  
  // Force le flush immédiat pour les décomptes de nourrissage (toujours fluide)
  _disp.display();
  _needsFlush = false;
}

DisplayView::DisplayView(uint8_t addr, uint8_t w, uint8_t h)
    : _disp(w, h, &Wire, -1) {
  _diagLine = 0;
}

bool DisplayView::begin() {
  Serial.printf("[OLED] Début initialisation - FEATURE_OLED=%d\n", FEATURE_OLED);
  
#if FEATURE_OLED == 0
  Serial.println("[OLED] FEATURE_OLED=0 - OLED DÉSACTIVÉ (macro de compilation)");
  _present = false;
  return false;
#endif

#ifdef OLED_DIAGNOSTIC
  Serial.println("=== DIAGNOSTIC OLED DÉTAILLÉ ===");
  Serial.printf("Pins configurés: SDA=%d, SCL=%d\n", Pins::I2C_SDA, Pins::I2C_SCL);
  Serial.printf("Adresse OLED: 0x%02X\n", Pins::OLED_ADDR);
#endif

  // CORRECTION v11.55: Gestion robuste I2C avec détection silencieuse
  static bool i2cInitialized = false;
  
  if (!i2cInitialized) {
    // Initialiser I2C avec les pins définis
    Wire.begin(Pins::I2C_SDA, Pins::I2C_SCL);
    delay(SensorConfig::I2C_STABILIZATION_DELAY_MS); // Attendre la stabilisation I2C
    i2cInitialized = true;
    
#ifdef OLED_DIAGNOSTIC
    Serial.println("I2C initialisé, test de détection...");
#endif
  } else {
#ifdef OLED_DIAGNOSTIC
    Serial.println("I2C déjà initialisé, test de détection...");
#endif
  }
  
  // CORRECTION v11.57: Détection robuste avec gestion d'erreur améliorée
  Wire.beginTransmission(Pins::OLED_ADDR);
  byte error = Wire.endTransmission();
  
  // Si erreur I2C, désactiver immédiatement pour éviter le spam
  if (error != 0) {
    Serial.printf("[OLED] Non détectée sur I2C (SDA:%d, SCL:%d, ADDR:0x%02X) - Erreur: %d\n", 
                  Pins::I2C_SDA, Pins::I2C_SCL, Pins::OLED_ADDR, error);
    Serial.println("[OLED] Mode dégradé activé (pas d'affichage)");
    _present = false;
    return false;
  }
  
  // OLED détectée, essayer de l'initialiser
  if (_disp.begin(SSD1306_SWITCHCAPVCC, Pins::OLED_ADDR)) {
    _present = true;
    
    // CORRECTION v11.57: Suppression du test de stabilité répété
    // Le test initial suffit, pas besoin de retester après chaque init
    _disp.clearDisplay();
    _disp.setTextColor(WHITE);
#if FEATURE_OLED
    // Active le mapping CP437 pour permettre l'affichage des accents (é, è, ç, ...)
    _disp.cp437(true);
#endif

    // --- SPLASH SCREEN UNIFIÉ (non bloquant) ---
    Serial.println("[OLED] Affichage du splash screen...");
    _disp.clearDisplay();
    
    // Fonction helper pour centrer le texte
    auto centerPrint = [&](const char* txt, uint8_t size, uint8_t y) {
      _disp.setTextSize(size);
      int16_t x = (_disp.width() - (strlen(txt) * 6 * size)) / 2;
      if (x < 0) x = 0;           // sécurité si texte trop long
      _disp.setCursor(x, y);
      _disp.println(txt);
    };

    // Ligne 1 : "Projet farmflow FFP5" (taille 1, centré)
    centerPrint("Projet farmflow FFP5", 1, 0);
    
    // Ligne 2 : Version du firmware (taille 1, centré)
    char vbuf[16];
    snprintf(vbuf, sizeof(vbuf), "v%s", ProjectConfig::VERSION);
    centerPrint(vbuf, 1, 10);
    
    // Logo N3 45x45 centré en dessous (à partir de y=18)
    int16_t logo_x = (_disp.width() - LOGO_WIDTH) / 2;   // (128-45)/2 = 41
    int16_t logo_y = 18;  // Juste en dessous du texte
    
    // Dessiner un rectangle blanc comme fond pour le logo
    _disp.fillRect(logo_x, logo_y, LOGO_WIDTH, LOGO_HEIGHT, WHITE);
    
    // Puis dessiner le logo en noir par-dessus (pour inverser les couleurs)
    _disp.drawBitmap(logo_x, logo_y, epd_bitmap_logo_n3_site, LOGO_WIDTH, LOGO_HEIGHT, BLACK);
    
    _disp.display();

    // Le splash reste visible selon DisplayConfig::SPLASH_DURATION_MS (non bloquant)
    _splashUntil = millis() + DisplayConfig::SPLASH_DURATION_MS;
    Serial.printf("[OLED] Splash screen activé jusqu'à %lu (durée: %u ms)\n", _splashUntil, DisplayConfig::SPLASH_DURATION_MS);

    _diagLine = 0; // reset diagnostic line counter at each begin
    resetStatusCache(); // Réinitialise le cache des états
    resetMainCache(); // Réinitialise le cache de l'affichage principal
    flush();
    
    Serial.printf("[OLED] Initialisée avec succès sur I2C (SDA:%d, SCL:%d, ADDR:0x%02X)\n", 
                 Pins::I2C_SDA, Pins::I2C_SCL, Pins::OLED_ADDR);
  } else {
    _present = false;
    Serial.printf("[OLED] Échec de l'initialisation sur I2C (SDA:%d, SCL:%d, ADDR:0x%02X)\n", 
                 Pins::I2C_SDA, Pins::I2C_SCL, Pins::OLED_ADDR);
  }
  
  return _present;
}

void DisplayView::clear() {
  if (!_present) return;
  _disp.clearDisplay();
}

void DisplayView::flush() {
  if (!_present) return;
  
  // Vérifier la santé I2C avant d'écrire
  if (!checkI2cHealth()) return;
  
  _disp.display();
  _needsFlush = false;
  
  // Reset du compteur d'erreurs si le flush réussit
  _i2cErrorCount = 0;
}

bool DisplayView::checkI2cHealth() {
  if (!_present) return false;
  
  // Vérification périodique pour ré-activer l'écran si les erreurs ont cessé
  unsigned long now = millis();
  if (_i2cErrorCount >= I2C_ERROR_LIMIT) {
    // Écran désactivé temporairement, vérifier si on peut le réactiver
    if (now - _lastI2cCheck >= I2C_CHECK_INTERVAL_MS) {
      _lastI2cCheck = now;
      
      // Test silencieux de présence I2C
      Wire.beginTransmission(Pins::OLED_ADDR);
      byte error = Wire.endTransmission();
      
      if (error == 0) {
        // L'écran répond à nouveau, réinitialiser
        Serial.println(F("[OLED] Écran I2C détecté à nouveau, réactivation..."));
        _i2cErrorCount = 0;
        return true;
      } else {
        // Toujours pas de réponse, log silencieux (une seule fois)
        return false;
      }
    }
    return false;  // Écran désactivé, ne pas tenter d'écrire
  }
  
  return true;
}

void DisplayView::reportI2cError() {
  if (_i2cErrorCount < I2C_ERROR_LIMIT) {
    _i2cErrorCount++;
    if (_i2cErrorCount >= I2C_ERROR_LIMIT) {
      Serial.printf("[OLED] %d erreurs I2C consécutives - écran désactivé temporairement\n", I2C_ERROR_LIMIT);
      _lastI2cCheck = millis();
    }
  }
}

void DisplayView::lockScreen(uint32_t durationMs) {
  _locked = true;
  _lockUntil = millis() + durationMs;
}

bool DisplayView::isLocked() const {
  if (!_locked) return false;
  unsigned long now = millis();
  if (now >= _lockUntil) {
    // Auto-expiration du verrouillage (nécessite d'altérer l'état depuis une méthode const)
    const_cast<DisplayView*>(this)->_locked = false;
    const_cast<DisplayView*>(this)->_lockUntil = 0;
    return false;
  }
  return true;
}

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
    char line[256];
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
      strcpy(headerBuf, "OTA: ");
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
      strcpy(headerBuf, "OTA ");
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
  Serial.println("[OLED] Force fin du splash screen");
  _splashUntil = 0;
  // Réinitialiser les caches pour forcer un redessin complet
  resetMainCache();
  resetStatusCache();
}

// Helpers de formatage
void DisplayView::formatLevel(uint16_t value, char* buffer, size_t size) {
  if (value == 0) {
    strncpy(buffer, "--", size);
    buffer[size - 1] = '\0';
  } else {
    snprintf(buffer, size, "%u", value);
  }
}

void DisplayView::formatTemp(float value, char* buffer, size_t size) {
  if (isnan(value)) {
    strncpy(buffer, "--.-", size);
    buffer[size - 1] = '\0';
    return;
  }
  snprintf(buffer, size, "%.1f", value);
}

void DisplayView::formatHumidity(float value, char* buffer, size_t size) {
  if (isnan(value)) {
    strncpy(buffer, "--.-", size);
    buffer[size - 1] = '\0';
    return;
  }
  snprintf(buffer, size, "%.1f", value);
}

const char* DisplayView::onOffLabel(bool value) {
  return value ? "ON" : "OFF";
}

// Méthodes de rendu
void DisplayView::renderMainScreen(float tempEau, float tempAir, float humidite,
                                    uint16_t aquaLvl, uint16_t tankLvl, uint16_t potaLvl,
                                    uint16_t lumi, const char* timeStr, bool wifiConnected,
                                    const char* stationSsid, const char* stationIp,
                                    const char* apSsid, const char* apIp) {
  _disp.setTextSize(1);

  {
    char buf[32];
    snprintf(buf, sizeof(buf), "FFP5CS v%s %s", ProjectConfig::VERSION, ProjectConfig::PROFILE_TYPE);
    printClipped(0, 0, buf, 1);
  }

  if (wifiConnected) {
    printClipped(0, 8, stationSsid, 1);
    printClipped(0, 16, stationIp, 1);
  } else {
    printClipped(0, 8, apSsid, 1);
    printClipped(0, 16, apIp, 1);
  }

  {
    char buf[32];
    char tempEauBuf[8];
    char tempAirBuf[8];
    formatTemp(tempEau, tempEauBuf, sizeof(tempEauBuf));
    formatTemp(tempAir, tempAirBuf, sizeof(tempAirBuf));
    snprintf(buf, sizeof(buf), "Eau:%sC Air:%sC", tempEauBuf, tempAirBuf);
    printClipped(0, 24, buf, 1);
  }

  {
    char buf[32];
    char aqBuf[6];
    char tkBuf[6];
    char ptBuf[6];
    formatLevel(aquaLvl, aqBuf, sizeof(aqBuf));
    formatLevel(tankLvl, tkBuf, sizeof(tkBuf));
    formatLevel(potaLvl, ptBuf, sizeof(ptBuf));
    snprintf(buf, sizeof(buf), "Aq:%s Tk:%s Pt:%s", aqBuf, tkBuf, ptBuf);
    printClipped(0, 32, buf, 1);
  }

  {
    char buf[32];
    char humBuf[8];
    formatHumidity(humidite, humBuf, sizeof(humBuf));
    snprintf(buf, sizeof(buf), "Hum:%s%% Lum:%u", humBuf, lumi);
    printClipped(0, 40, buf, 1);
  }

  printClipped(0, 48, timeStr, 1);

  if (::MonitoringConfig::ENABLE_DRIFT_VISUAL_INDICATOR) {
    static unsigned long lastDriftCheck = 0;
    unsigned long now = millis();
    if (now - lastDriftCheck > ::MonitoringConfig::DRIFT_CHECK_INTERVAL_MS) {
      lastDriftCheck = now;
      if (!wifiConnected) {
        _disp.drawPixel(127, 0, WHITE);
      } else {
        _disp.drawPixel(127, 0, BLACK);
      }
    }
  }
}

void DisplayView::renderCountdown(const char* label, uint16_t secondsLeft, bool isManual) {
  printClipped(0, 0, label, 2);

  _disp.setTextSize(3);
  _disp.setCursor(0, 24);
  if (secondsLeft == 0) {
    _disp.setTextSize(2);
    _disp.print("Terminé");
  } else {
    _disp.printf("%u", secondsLeft);
  }

  _disp.setTextSize(1);
  if (isManual) {
    _disp.drawChar(110, 0, 'M', WHITE, BLACK, 1);
  } else {
    _disp.drawChar(110, 0, 'A', WHITE, BLACK, 1);
  }
}

void DisplayView::renderFeedingCountdown(const char* fishType, const char* phase,
                                         uint16_t secondsLeft, bool isManual) {
  printClipped(0, 0, "Nourriture", 1);
  printClipped(0, 10, fishType, 2);
  char tempsBuf[64];
  snprintf(tempsBuf, sizeof(tempsBuf), "Temps %s", phase);
  printClipped(0, 26, tempsBuf, 1);

  _disp.setTextSize(2);
  _disp.setCursor(0, 36);
  if (secondsLeft == 0) {
    _disp.print("Terminé");
  } else {
    _disp.printf("%u", secondsLeft);
  }

  _disp.setTextSize(1);
  if (isManual) {
    _disp.drawChar(110, 0, 'M', WHITE, BLACK, 1);
  } else {
    _disp.drawChar(110, 0, 'A', WHITE, BLACK, 1);
  }
}

void DisplayView::renderVariables(bool pumpAqua, bool pumpTank, bool heater, bool light,
                                  uint8_t hMat, uint8_t hMid, uint8_t hSoir,
                                  uint16_t tPetits, uint16_t tGros,
                                  uint16_t thAq, uint16_t thTank, float thHeat,
                                  uint16_t limFlood) {
  _disp.setTextSize(1);
  printClipped(0, 0, "Relais+Cfg:", 1);

  char buf[32];
  snprintf(buf, sizeof(buf), "PomAq:%s PomTk:%s", onOffLabel(pumpAqua), onOffLabel(pumpTank));
  printClipped(0, 8, buf, 1);

  snprintf(buf, sizeof(buf), "Chauff:%s Lumi:%s", onOffLabel(heater), onOffLabel(light));
  printClipped(0, 16, buf, 1);

  snprintf(buf, sizeof(buf), "Feed h:%02u %02u %02u", hMat, hMid, hSoir);
  printClipped(0, 24, buf, 1);

  snprintf(buf, sizeof(buf), "Tps P:%us G:%us", tPetits, tGros);
  printClipped(0, 32, buf, 1);

  snprintf(buf, sizeof(buf), "Seuil Aq:%u Ta:%u", thAq, thTank);
  printClipped(0, 40, buf, 1);

  snprintf(buf, sizeof(buf), "Ch:%.1fC F:%ucm", thHeat, limFlood);
  printClipped(0, 48, buf, 1);
}

void DisplayView::renderServerVars(bool pumpAqua, bool pumpTank, bool heater, bool light,
                                   uint8_t hMat, uint8_t hMid, uint8_t hSoir,
                                   uint16_t tPetits, uint16_t tGros,
                                   uint16_t thAq, uint16_t thTank, float thHeat,
                                   uint16_t tRemp, uint16_t limFlood,
                                   bool wakeUp, uint16_t freqWake) {
  _disp.setTextSize(1);
  printClipped(0, 0, "Vars:", 1);

  char buf[64];
  snprintf(buf, sizeof(buf), "Paq:%d Pta:%d R:%d L:%d", pumpAqua, pumpTank, heater, light);
  printClipped(0, 8, buf, 1);

  snprintf(buf, sizeof(buf), "Feed h:%u %u %u", hMat, hMid, hSoir);
  printClipped(0, 16, buf, 1);

  snprintf(buf, sizeof(buf), "Tps P:%u G:%u", tPetits, tGros);
  printClipped(0, 24, buf, 1);

  snprintf(buf, sizeof(buf), "Lim Aq:%u Ta:%u", thAq, thTank);
  printClipped(0, 32, buf, 1);

  snprintf(buf, sizeof(buf), "Ch:%.1f R:%u F:%u", thHeat, tRemp, limFlood);
  printClipped(0, 40, buf, 1);

  snprintf(buf, sizeof(buf), "W:%d Fq:%us", wakeUp ? 1 : 0, freqWake);
  printClipped(0, 48, buf, 1);
}

void DisplayView::renderStatusBar(const StatusBarParams& params) {
  // Dessiner la barre d'état en haut de l'écran (8 pixels de hauteur)
  
  // Effacer la zone de la barre d'état
  _disp.fillRect(0, 0, 128, 8, BLACK);
  
  // WiFi RSSI indicator (gauche)
  _disp.setCursor(0, 0);
  _disp.setTextSize(1);
  _disp.setTextColor(WHITE);
  
  if (params.rssi >= -50) {
    _disp.print(F("WiFi +++"));
  } else if (params.rssi >= -70) {
    _disp.print(F("WiFi ++ "));
  } else if (params.rssi >= -85) {
    _disp.print(F("WiFi +  "));
  } else if (params.rssi > -100) {
    _disp.print(F("WiFi -  "));
  } else {
    _disp.print(F("WiFi X  "));
  }
  
  // Indicateur send/recv (milieu)
  _disp.setCursor(60, 0);
  if (params.sendState == 1) {
    _disp.print(F("S"));
  } else if (params.sendState == -1) {
    _disp.print(F("!"));
  } else {
    _disp.print(F(" "));
  }
  
  if (params.recvState == 1) {
    _disp.print(F("R"));
  } else if (params.recvState == -1) {
    _disp.print(F("!"));
  } else {
    _disp.print(F(" "));
  }
  
  // Indicateur marée (droite)
  _disp.setCursor(80, 0);
  if (params.tideDir > 0) {
    _disp.print(F("^")); // Marée montante
  } else if (params.tideDir < 0) {
    _disp.print(F("v")); // Marée descendante
  } else {
    _disp.print(F("-")); // Stable
  }
  
  // Mail blink indicator
  if (params.mailBlink) {
    _disp.setCursor(90, 0);
    _disp.print(F("@"));
  }
  
  // OTA progress overlay (si actif)
  if (params.otaOverlayActive) {
    _disp.setCursor(100, 0);
    _disp.printf("%d%%", params.otaPercent);
  }
}

void DisplayView::appendDiagnosticLine(const char* line, uint8_t lineIndex) {
  _disp.setTextSize(1);
  printClipped(0, lineIndex * 8, line, 1);
}