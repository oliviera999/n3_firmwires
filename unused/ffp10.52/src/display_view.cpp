#include "display_view.h"
#include "project_config.h"
#include "pins.h"
#include <WiFi.h>
#include <algorithm>
#include <string.h>

// Conversion UTF-8 → CP437 (minimal, couvre les accents FR courants)
static String utf8ToCp437(const char* input) {
  String out;
  if (!input) return out;
  const uint8_t* s = reinterpret_cast<const uint8_t*>(input);
  while (*s) {
    uint8_t c = *s++;
    if (c == 0xC3) {
      uint8_t n = *s;
      if (n) {
        ++s;
        switch (n) {
          // Minuscules
          case 0xA9: out += char(0x82); break; // é
          case 0xA8: out += char(0x8A); break; // è
          case 0xAA: out += char(0x88); break; // ê
          case 0xA0: out += char(0x85); break; // à
          case 0xA2: out += char(0x83); break; // â
          case 0xB9: out += char(0x97); break; // ù
          case 0xBB: out += char(0x96); break; // û
          case 0xA7: out += char(0x87); break; // ç
          case 0xB4: out += char(0x93); break; // ô
          case 0xAF: out += char(0x8B); break; // ï
          case 0xAE: out += char(0x8C); break; // î
          // Majuscules courantes
          case 0x89: out += char(0x90); break; // É
          case 0x87: out += char(0x80); break; // Ç
          default:
            // Caractère non mappé : on ignore le diacritique
            // (optionnel: out += '?';)
            break;
        }
      }
    } else if ((c & 0xE0) == 0xC0) {
      // Début d'une séquence UTF-8 2 octets inattendue (non 0xC3)
      // On consomme l'octet suivant et on ignore
      if (*s) ++s;
    } else if ((c & 0xF0) == 0xE0) {
      // Début d'une séquence UTF-8 3 octets: on saute les deux suivants
      if (*s) ++s;
      if (*s) ++s;
    } else if ((c & 0xF8) == 0xF0) {
      // Début d'une séquence UTF-8 4 octets: on saute les trois suivants
      if (*s) ++s;
      if (*s) ++s;
      if (*s) ++s;
    } else {
      // ASCII direct
      out += char(c);
    }
  }
  return out;
}

// Texte protégé contre le dépassement horizontal (police 6x8 px par taille 1)
void DisplayView::printClipped(int16_t x, int16_t y, const String& text, uint8_t size) {
  if (!_present) return;
  // Largeur max pixels
  const int16_t maxWidth = _disp.width();
  // Largeur d'un caractère: 6 px en taille 1 (5 + 1 espace)
  int16_t charWidth = 6 * size;
  if (charWidth <= 0) charWidth = 6;

  // Convertit en CP437 pour cohérence avec l'affichage
  String t = utf8ToCp437(text.c_str());
  // Calcule le nombre max de caractères qui rentrent sur la ligne
  int16_t available = maxWidth - x;
  if (available <= 0) return;
  int16_t maxChars = available / charWidth;
  if (maxChars <= 0) return;

  String toPrint = t;
  if ((int)toPrint.length() > maxChars) {
    // Garde place pour "..." si possible
    if (maxChars >= 3) {
      toPrint = toPrint.substring(0, maxChars - 3) + "...";
    } else {
      toPrint = toPrint.substring(0, maxChars);
    }
  }

  _disp.setTextSize(size);
  _disp.setCursor(x, y);
  _disp.println(toPrint.c_str());
}

// ---- helpers ----
static uint8_t wifiBars(int8_t rssi){
  if(rssi>=-60) return 4;
  if(rssi>=-67) return 3;
  if(rssi>=-75) return 2;
  if(rssi>=-82) return 1;
  return 0;
}

// Nouvelles méthodes d'optimisation
void DisplayView::beginUpdate() {
  if (!_present) return;
  _updateMode = true;
  _needsFlush = false;
}

void DisplayView::endUpdate() {
  if (!_present) return;
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
  // Réinitialise les états mémorisés de la barre de statut
  _lastSendState = -2;
  _lastRecvState = -2;
  _lastRssi = -128;
  _lastMailBlink = false;
  _lastTideDir = 0;
  _lastDiffValue = 0;
}

void DisplayView::resetMainCache() {
  // Réinitialise les états mémorisés de l'écran principal
  _lastTempEau = -999;
  _lastTempAir = -999;
  _lastHumidite = -999;
  _lastAquaLvl = 999;
  _lastTankLvl = 999;
  _lastPotaLvl = 999;
  _lastLumi = 999;
  _lastTimeStr = "";
}

void DisplayView::forceClear() {
  if (!_present) return;
  
  // Force un nettoyage complet de l'écran
  _disp.clearDisplay();
  _disp.display();
  
  // Réinitialise tous les caches
  resetMainCache();
  resetStatusCache();
  
  // Force le mode immédiat temporairement
  _immediateMode = true;
  _needsFlush = false;
}

void DisplayView::drawStatus(int8_t sendState, int8_t recvState, int8_t rssi, bool mailBlink,
                             int8_t tideDir, int diffValue) {
  if (!_present) return;
  if (isLocked()) return;

  // Vérification si les valeurs ont changé (optimisation)
  bool needsUpdate = (sendState != _lastSendState || recvState != _lastRecvState || 
                     rssi != _lastRssi || mailBlink != _lastMailBlink || 
                     tideDir != _lastTideDir || diffValue != _lastDiffValue);
  
  if (!needsUpdate && !_updateMode) return; // Pas de changement, pas de mise à jour nécessaire
  
  // Mise à jour des états mémorisés
  _lastSendState = sendState;
  _lastRecvState = recvState;
  _lastRssi = rssi;
  _lastMailBlink = mailBlink;
  _lastTideDir = tideDir;
  _lastDiffValue = diffValue;

  // Petit helper pour dessiner une flèche (↑ ou ↓) dans un carré de 8×8
  auto drawArrow = [&](bool up, int x, int y, int8_t state) {
    bool filled  = (state > 0);
    bool failed  = (state < 0);

    // Sécurise : efface la zone 8×8 avant de redessiner l'icône de flèche
    _disp.fillRect(x - 1, y - 7, 8, 8, BLACK);

    // Dessin de l'icône de flèche
    if (up) {
      // Tête
      if (filled)
        _disp.fillTriangle(x + 3, y - 6, x, y, x + 6, y, WHITE);
      else
        _disp.drawTriangle(x + 3, y - 6, x, y, x + 6, y, WHITE);
      // Corps
      if (filled)
        _disp.fillRect(x + 2, y - 2, 2, 2, WHITE);
      else
        _disp.drawRect(x + 2, y - 2, 2, 2, WHITE);
    } else {
      // Flèche vers le bas
      if (filled)
        _disp.fillTriangle(x + 3, y, x, y - 6, x + 6, y - 6, WHITE);
      else
        _disp.drawTriangle(x + 3, y, x, y - 6, x + 6, y - 6, WHITE);
      if (filled)
        _disp.fillRect(x + 2, y - 4, 2, 2, WHITE);
      else
        _disp.drawRect(x + 2, y - 4, 2, 2, WHITE);
    }

    // Si échec, on barre l'icône
    if (failed) {
      _disp.drawLine(x, y - 6, x + 6, y, WHITE);
      _disp.drawLine(x, y, x + 6, y - 6, WHITE);
    }
  };

  // 0) Icône enveloppe (notification mail) — clignote
  {
    // Toujours nettoyer la zone de l'enveloppe pour éviter les résidus visuels
    int ex = 0, ey = 56;
    _disp.fillRect(ex, ey, 12, 8, BLACK);
    if (mailBlink) {
      _disp.drawRect(ex, ey, 12, 8, WHITE);
      _disp.drawLine(ex, ey, ex + 6, ey + 4, WHITE);
      _disp.drawLine(ex + 6, ey + 4, ex + 12, ey, WHITE);
    }
  }
  
  // Préserver l'overlay OTA s'il est actif
  if (_otaOverlayActive) {
    // Redessiner l'overlay OTA pour qu'il ne soit pas effacé par la barre de statut
    char percentStr[12];
    snprintf(percentStr, sizeof(percentStr), "OTA: %u%%", _lastOtaPercent);
    printClipped(100, 0, String(percentStr), 1);
  }

  // 1) Flèche upload (envoi)
  // Positionnée à y=63 pour éviter d'effacer la dernière ligne de texte (48-55)
  drawArrow(true, 14, 63, sendState);

  // 2) Flèche download (réception)
  drawArrow(false, 26, 63, recvState);

  // 3) Icône marée + valeur
  int tx = 38; // position x de l'icône marée
  int ty = 63; // ancré en bas (63) pour ne pas empiéter sur la ligne de texte (<=55)
  // Efface la zone (icône + 3 chiffres potentielles), bornée à y>=56
  _disp.fillRect(tx - 1, ty - 7, 40, 8, BLACK);

  if (tideDir != 0) {
    // Symbole + pour montée, - pour descente
    if (tideDir > 0) {
      // Symbole + (plus)
      _disp.drawLine(tx + 3, ty - 6, tx + 3, ty, WHITE);     // ligne verticale
      _disp.drawLine(tx, ty - 3, tx + 6, ty - 3, WHITE);     // ligne horizontale
    } else {
      // Symbole - (moins)
      _disp.drawLine(tx, ty - 3, tx + 6, ty - 3, WHITE);     // ligne horizontale
    }
  } else {
    // Signe égal : deux traits horizontaux
    _disp.drawLine(tx,     ty - 4, tx + 6, ty - 4, WHITE);
    _disp.drawLine(tx,     ty - 2, tx + 6, ty - 2, WHITE);
  }
  // Valeur numérique juste à droite (protégée contre dépassement)
  printClipped(tx + 10, ty - 7, String(diffValue), 1);

  // 4) Wi-Fi (barres) — toujours en bas-droite
  uint8_t bars = wifiBars(rssi);
  int bx = 108, by = 62; // y = bas de la plus petite barre
  // Nettoie la zone Wi-Fi (bornée à y>=56 pour éviter de mordre sur la dernière ligne texte)
  _disp.fillRect(bx - 1, by - 6, 16, 8, BLACK);
  for (uint8_t i = 0; i < 4; ++i) {
    if (i < bars)
      _disp.fillRect(bx + i * 3, by - (i * 2), 2, i * 2 + 2, WHITE);
    else
      _disp.drawRect(bx + i * 3, by - (i * 2), 2, i * 2 + 2, WHITE);
  }
  // Si pas connecté, on barre l'icône Wi-Fi
  if (rssi < -120) {
    _disp.drawLine(bx - 1, by - 6, bx + 13, by + 1, WHITE);
    _disp.drawLine(bx - 1, by + 1, bx + 13, by - 6, WHITE);
  }
  // Affiche le RSSI en texte à gauche des barres (si connecté)
  if (rssi > -120) {
    int rx = 88, ry = 56; // zone dédiée pour le texte RSSI
    // Nettoie la zone texte RSSI (largeur ~ 18px)
    _disp.fillRect(rx, ry, 20, 8, BLACK);
    char rbuf[8];
    snprintf(rbuf, sizeof(rbuf), "%d", (int)rssi);
    printClipped(rx, ry, String(rbuf), 1);
  }
  
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
  bool needsUpdate = force ? true : (sendState != _lastSendState || recvState != _lastRecvState || 
                     rssi != _lastRssi || mailBlink != _lastMailBlink || 
                     tideDir != _lastTideDir || diffValue != _lastDiffValue);
  if (!needsUpdate && !_updateMode) return;

  _lastSendState = sendState;
  _lastRecvState = recvState;
  _lastRssi = rssi;
  _lastMailBlink = mailBlink;
  _lastTideDir = tideDir;
  _lastDiffValue = diffValue;

  // Reuse original drawing logic
  // 0) Mail icon
  int ex = 0, ey = 56;
  _disp.fillRect(ex, ey, 12, 8, BLACK);
  if (mailBlink) {
    _disp.drawRect(ex, ey, 12, 8, WHITE);
    _disp.drawLine(ex, ey, ex + 6, ey + 4, WHITE);
    _disp.drawLine(ex + 6, ey + 4, ex + 12, ey, WHITE);
  }
  
  // Préserver l'overlay OTA s'il est actif
  if (_otaOverlayActive) {
    // Redessiner l'overlay OTA pour qu'il ne soit pas effacé par la barre de statut
    char percentStr[12];
    snprintf(percentStr, sizeof(percentStr), "OTA: %u%%", _lastOtaPercent);
    printClipped(100, 0, String(percentStr), 1);
  }

  // 1) Upload arrow
  auto drawArrow = [&](bool up, int x, int y, int8_t state) {
    bool filled  = (state > 0);
    bool failed  = (state < 0);
    _disp.fillRect(x - 1, y - 7, 8, 8, BLACK);
    if (up) {
      if (filled) _disp.fillTriangle(x + 3, y - 6, x, y, x + 6, y, WHITE);
      else        _disp.drawTriangle(x + 3, y - 6, x, y, x + 6, y, WHITE);
      if (filled) _disp.fillRect(x + 2, y - 2, 2, 2, WHITE);
      else        _disp.drawRect(x + 2, y - 2, 2, 2, WHITE);
    } else {
      if (filled) _disp.fillTriangle(x + 3, y, x, y - 6, x + 6, y - 6, WHITE);
      else        _disp.drawTriangle(x + 3, y, x, y - 6, x + 6, y - 6, WHITE);
      if (filled) _disp.fillRect(x + 2, y - 4, 2, 2, WHITE);
      else        _disp.drawRect(x + 2, y - 4, 2, 2, WHITE);
    }
    if (failed) {
      _disp.drawLine(x, y - 6, x + 6, y, WHITE);
      _disp.drawLine(x, y, x + 6, y - 6, WHITE);
    }
  };

  drawArrow(true, 14, 63, sendState);
  drawArrow(false, 26, 63, recvState);

  int tx = 38; int ty = 63;
  _disp.fillRect(tx - 1, ty - 7, 40, 8, BLACK);
  if (tideDir != 0) {
    if (tideDir > 0) {
      _disp.drawLine(tx + 3, ty - 6, tx + 3, ty, WHITE);
      _disp.drawLine(tx, ty - 3, tx + 6, ty - 3, WHITE);
    } else {
      _disp.drawLine(tx, ty - 3, tx + 6, ty - 3, WHITE);
    }
  } else {
    _disp.drawLine(tx, ty - 4, tx + 6, ty - 4, WHITE);
    _disp.drawLine(tx, ty - 2, tx + 6, ty - 2, WHITE);
  }
  printClipped(tx + 10, ty - 7, String(diffValue), 1);

  uint8_t bars = wifiBars(rssi);
  int bx = 108, by = 62;
  _disp.fillRect(bx - 1, by - 6, 16, 8, BLACK);
  for (uint8_t i = 0; i < 4; ++i) {
    if (i < bars) _disp.fillRect(bx + i * 3, by - (i * 2), 2, i * 2 + 2, WHITE);
    else          _disp.drawRect(bx + i * 3, by - (i * 2), 2, i * 2 + 2, WHITE);
  }
  if (rssi < -120) {
    _disp.drawLine(bx - 1, by - 6, bx + 13, by + 1, WHITE);
    _disp.drawLine(bx - 1, by + 1, bx + 13, by - 6, WHITE);
  }
  if (rssi > -120) {
    int rx = 88, ry = 56;
    _disp.fillRect(rx, ry, 20, 8, BLACK);
    char rbuf[8];
    snprintf(rbuf, sizeof(rbuf), "%d", (int)rssi);
    printClipped(rx, ry, String(rbuf), 1);
  }

  if (_updateMode) _needsFlush = true;
}

void DisplayView::showCountdown(const char* label, uint16_t secLeft){
  if(!_present || splashActive() || _isDisplaying || isLocked()) return;
  
  _isDisplaying = true;
  
  // Force un nettoyage complet et désactive les optimisations pour éviter les superpositions
  _disp.clearDisplay();
  
  // Réinitialise les caches pour éviter les conflits avec les autres affichages
  resetMainCache();
  resetStatusCache();
  
  // Désactive temporairement les optimisations pour garantir un affichage propre
  bool oldImmediateMode = _immediateMode;
  _immediateMode = true;
  
  // Affichage avec gestion cohérente des tailles de texte (protégé largeur)
  printClipped(0, 0, String(label), 2);
  
  _disp.setTextSize(3);
  _disp.setCursor(0, 24);
  if (secLeft == 0) {
    _disp.setTextSize(2);
    _disp.print("Terminé");
  } else {
    _disp.printf("%u", secLeft);
  }
  
  // Force le flush immédiat pour les décomptes (toujours fluide)
  _disp.display();
  _needsFlush = false;
  
  // Restaure le mode d'origine
  _immediateMode = oldImmediateMode;
  
  _isDisplaying = false;
}

void DisplayView::showFeedingCountdown(const char* fishType, const char* phase, uint16_t secLeft){
  if(!_present || splashActive() || _isDisplaying || isLocked()) return;
  
  _isDisplaying = true;
  
  // Force un nettoyage complet et désactive les optimisations pour éviter les superpositions
  _disp.clearDisplay();
  
  // Réinitialise les caches pour éviter les conflits avec les autres affichages
  resetMainCache();
  resetStatusCache();
  
  // Désactive temporairement les optimisations pour garantir un affichage propre
  bool oldImmediateMode = _immediateMode;
  _immediateMode = true;
  
  // Affichage avec gestion cohérente des tailles de texte
  printClipped(0, 0, String("Nourriture"), 1);
  
  printClipped(0, 10, String(fishType), 2);
  
  printClipped(0, 26, String("Temps ") + phase, 1);
  
  _disp.setTextSize(2);
  _disp.setCursor(0, 36); // Position ajustée
  if (secLeft == 0) {
    _disp.print("Terminé");
  } else {
    _disp.printf("%u", secLeft);
  }
  
  // Force le flush immédiat pour les décomptes de nourrissage (toujours fluide)
  _disp.display();
  _needsFlush = false;
  
  // Restaure le mode d'origine
  _immediateMode = oldImmediateMode;
  
  _isDisplaying = false;
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

  // Vérification supplémentaire pour cohérence runtime/compile-time
  if (!Features::OLED_ENABLED) {
    Serial.println("[OLED] OLED_ENABLED=false - OLED DÉSACTIVÉ (configuration runtime)");
    _present = false;
    return false;
  }

#ifdef OLED_DIAGNOSTIC
  Serial.println("=== DIAGNOSTIC OLED DÉTAILLÉ ===");
  Serial.printf("Pins configurés: SDA=%d, SCL=%d\n", Pins::I2C_SDA, Pins::I2C_SCL);
  Serial.printf("Adresse OLED: 0x%02X\n", Pins::OLED_ADDR);
#endif

  // Initialiser I2C avec les pins définis
  Wire.begin(Pins::I2C_SDA, Pins::I2C_SCL);
  delay(ExtendedSensorConfig::I2C_STABILIZATION_DELAY_MS); // Attendre la stabilisation I2C
  
#ifdef OLED_DIAGNOSTIC
  Serial.println("I2C initialisé, test de détection...");
#endif
  
  // Détection automatique de l'OLED
  Wire.beginTransmission(Pins::OLED_ADDR);
  byte error = Wire.endTransmission();
  
#ifdef OLED_DIAGNOSTIC
  Serial.printf("Test adresse 0x%02X - Code d'erreur: %d\n", Pins::OLED_ADDR, error);
  switch(error) {
    case 0: Serial.println("  → Succès: Périphérique détecté"); break;
    case 1: Serial.println("  → Erreur 1: Buffer trop petit"); break;
    case 2: Serial.println("  → Erreur 2: NACK sur adresse (connexion SDA/SCL)"); break;
    case 3: Serial.println("  → Erreur 3: NACK sur données"); break;
    case 4: Serial.println("  → Erreur 4: Autre erreur"); break;
    case 5: Serial.println("  → Erreur 5: Timeout"); break;
    default: Serial.printf("  → Erreur inconnue: %d\n", error);
  }
  
  // Test avec adresse alternative
  Serial.println("Test avec adresse alternative 0x3D...");
  Wire.beginTransmission(0x3D);
  byte errorAlt = Wire.endTransmission();
  Serial.printf("Test adresse 0x3D - Code d'erreur: %d\n", errorAlt);
  if (errorAlt == 0) {
    Serial.println("  → OLED détectée sur adresse 0x3D !");
  }
#endif
  
  if (error == 0) {
    // OLED détectée, essayer de l'initialiser
    if (_disp.begin(SSD1306_SWITCHCAPVCC, Pins::OLED_ADDR)) {
      _present = true;
      _disp.clearDisplay();
      _disp.setTextColor(WHITE);
#if FEATURE_OLED
      // Active le mapping CP437 pour permettre l'affichage des accents (é, è, ç, ...)
      _disp.cp437(true);
#endif

      // --- Nouveau splash centré sur trois lignes ---
      auto centerPrint = [&](const char* txt, uint8_t size, uint8_t y) {
        _disp.setTextSize(size);
        int16_t x = (_disp.width() - (strlen(txt) * 6 * size)) / 2;
        if (x < 0) x = 0;           // sécurité si texte trop long
        _disp.setCursor(x, y);
        _disp.println(txt);
      };

      centerPrint("FFP3 Init", 2, 0);     // ligne 1
      centerPrint("farmflow", 2, 20);     // ligne 2
      centerPrint("n3 project", 2, 40);   // ligne 3

      // Ajout de la version du firmware en bas de l'écran
      char vbuf[24];
      snprintf(vbuf, sizeof(vbuf), "v%s", Config::VERSION);
      centerPrint(vbuf, 1, 56);

      // Le splash doit rester visible au moins 2 secondes
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
  } else {
    _present = false;
    Serial.printf("[OLED] Non détectée sur I2C (SDA:%d, SCL:%d, ADDR:0x%02X) - Erreur: %d\n", 
                 Pins::I2C_SDA, Pins::I2C_SCL, Pins::OLED_ADDR, error);
  }
  
  return _present;
}

void DisplayView::clear() {
  if (!_present) return;
  _disp.clearDisplay();
}

void DisplayView::flush() {
  if (!_present) return;
  _disp.display();
  _needsFlush = false;
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
                           uint16_t tankLvl, uint16_t potaLvl, uint16_t lumi, const String& timeStr) {
  if (!_present || splashActive() || isLocked()) return;
  
  // Vérifier si les valeurs ont changé significativement
  bool valuesChanged = (abs(tempEau - _lastTempEau) > 0.1) || 
                      (abs(tempAir - _lastTempAir) > 0.1) ||
                      (abs(humidite - _lastHumidite) > 0.5) ||
                      (abs(aquaLvl - _lastAquaLvl) > 0) ||
                      (abs(tankLvl - _lastTankLvl) > 0) ||
                      (abs(potaLvl - _lastPotaLvl) > 0) ||
                      (abs(lumi - _lastLumi) > 1) ||
                      (timeStr != _lastTimeStr);
  
  // Mettre à jour les états mémorisés
  _lastTempEau = tempEau;
  _lastTempAir = tempAir;
  _lastHumidite = humidite;
  _lastAquaLvl = aquaLvl;
  _lastTankLvl = tankLvl;
  _lastPotaLvl = potaLvl;
  _lastLumi = lumi;
  _lastTimeStr = timeStr;
  
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
  
  _disp.setTextSize(1);
  {
    char buf[32];
    snprintf(buf, sizeof(buf), "FFP3 v%s", Config::VERSION);
    printClipped(0, 0, String(buf), 1);
  }
  if (WiFi.status() == WL_CONNECTED) {
    printClipped(0, 8, WiFi.SSID(), 1);
    printClipped(0, 16, WiFi.localIP().toString(), 1);
  } else {
    printClipped(0, 8, WiFi.softAPSSID(), 1);
    printClipped(0, 16, WiFi.softAPIP().toString(), 1);
  }
  {
    char buf[32];
    snprintf(buf, sizeof(buf), "Te:%.1f Ta:%.1f", tempEau, tempAir);
    printClipped(0, 24, String(buf), 1);
  }
  {
    char buf[32];
    // Afficher "--" pour les niveaux ultrason invalides (0)
    char aqBuf[6];
    char tkBuf[6];
    char ptBuf[6];
    if (aquaLvl == 0) strncpy(aqBuf, "--", sizeof(aqBuf)); else snprintf(aqBuf, sizeof(aqBuf), "%u", aquaLvl);
    if (tankLvl == 0) strncpy(tkBuf, "--", sizeof(tkBuf)); else snprintf(tkBuf, sizeof(tkBuf), "%u", tankLvl);
    if (potaLvl == 0) strncpy(ptBuf, "--", sizeof(ptBuf)); else snprintf(ptBuf, sizeof(ptBuf), "%u", potaLvl);
    aqBuf[sizeof(aqBuf)-1] = '\0'; tkBuf[sizeof(tkBuf)-1] = '\0'; ptBuf[sizeof(ptBuf)-1] = '\0';
    snprintf(buf, sizeof(buf), "Aq:%s Tk:%s Pt:%s", aqBuf, tkBuf, ptBuf);
    printClipped(0, 32, String(buf), 1);
  }
  {
    char buf[32];
    snprintf(buf, sizeof(buf), "H:%.1f%%  L:%u", humidite, lumi);
    printClipped(0, 40, String(buf), 1);
  }
  // Protéger l'affichage de l'heure contre le dépassement horizontal
  printClipped(0, 48, timeStr, 1);
  
  // Indicateur de dérive temporelle (petit point en haut à droite)
  // Si pas de sync NTP récente, afficher un indicateur d'avertissement
  if (::MonitoringConfig::ENABLE_DRIFT_VISUAL_INDICATOR) {
    static unsigned long lastDriftCheck = 0;
    if (millis() - lastDriftCheck > ::MonitoringConfig::DRIFT_CHECK_INTERVAL_MS) {
      lastDriftCheck = millis();
      // Vérifier si on est hors-ligne depuis longtemps (pas de WiFi ou sync NTP ancienne)
      if (WiFi.status() != WL_CONNECTED) {
        // Afficher un petit indicateur de dérive possible (point blanc)
        _disp.drawPixel(127, 0, WHITE); // Point en haut à droite
      } else {
        // Effacer l'indicateur si WiFi connecté
        _disp.drawPixel(127, 0, BLACK);
      }
    }
  }
  
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

void DisplayView::showVariables(bool pumpAqua, bool pumpTank, bool heater, bool light) {
  if (!_present || splashActive() || isLocked()) return;
  clear();
  _disp.setTextSize(1);
  printClipped(0, 0, String("Relais:"), 1);
  {
    char buf[32];
    snprintf(buf, sizeof(buf), "Paq:%d Pta:%d", pumpAqua, pumpTank);
    printClipped(0, 8, String(buf), 1);
  }
  {
    char buf[32];
    snprintf(buf, sizeof(buf), "Heat:%d Light:%d", heater, light);
    printClipped(0, 16, String(buf), 1);
  }
  if (!_updateMode) flush();
  else _needsFlush = true;
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
    String line = utf8ToCp437(msg);
    printClipped(0, _diagLine * 8, line, 1); // 8px par ligne en taille 1
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
  _disp.setTextSize(1);
  printClipped(0, 0, String("Vars:"), 1);
  {
    char buf[64];
    snprintf(buf,sizeof(buf),"Paq:%d Pta:%d R:%d L:%d",pumpAqua,pumpTank,heater,light);
    printClipped(0, 8, String(buf), 1);
    snprintf(buf,sizeof(buf),"Feed h:%u %u %u",hMat,hMid,hSoir);
    printClipped(0, 16, String(buf), 1);
    snprintf(buf,sizeof(buf),"Tps P:%u G:%u",tPetits,tGros);
    printClipped(0, 24, String(buf), 1);
    snprintf(buf,sizeof(buf),"Lim Aq:%u Ta:%u",thAq,thTank);
    printClipped(0, 32, String(buf), 1);
    snprintf(buf,sizeof(buf),"Ch:%.1f R:%u F:%u",thHeat,tRemp,limFlood);
    printClipped(0, 40, String(buf), 1);
    snprintf(buf,sizeof(buf),"W:%d Fq:%us",wakeUp?1:0,freqWake);
    printClipped(0, 48, String(buf), 1);
  }
  if (!_updateMode) flush();
  else _needsFlush = true;
} 

void DisplayView::showOtaProgress(uint8_t percent, const char* fromLabel, const char* toLabel, const char* phase){
  if(!_present || splashActive() || _isDisplaying) return;

  _isDisplaying = true;

  // Nettoyage complet et reset des caches pour un rendu propre
  _disp.clearDisplay();
  resetMainCache();
  resetStatusCache();

  bool oldImmediateMode = _immediateMode;
  _immediateMode = true;

  // En-tête (phase claire)
  printClipped(0, 0, String("OTA: ") + (phase ? utf8ToCp437(phase) : String("")), 1);

  // Lignes partitions
  if (fromLabel && *fromLabel) {
    printClipped(0, 10, String("From: ") + utf8ToCp437(fromLabel), 1);
  }
  if (toLabel && *toLabel) {
    printClipped(0, 18, String("To:   ") + utf8ToCp437(toLabel), 1);
  }

  // Pourcentage centré en grand
  char pbuf[8];
  snprintf(pbuf, sizeof(pbuf), "%u%%", (unsigned)percent);
  _disp.setTextSize(3);
  _disp.setCursor(0, 24);
  printClipped(0, 24, String(pbuf), 2);

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
  _immediateMode = oldImmediateMode;

  _isDisplaying = false;
}

void DisplayView::showOtaProgressEx(uint8_t percent, const char* fromLabel, const char* toLabel,
                         const char* phase, const char* currentVersion,
                         const char* newVersion, const char* hostLabel){
  if(!_present || splashActive() || _isDisplaying || isLocked()) return;

  _isDisplaying = true;

  _disp.clearDisplay();
  resetMainCache();
  resetStatusCache();

  bool oldImmediateMode = _immediateMode;
  _immediateMode = true;

  // En-tête
  printClipped(0, 0, String("OTA ") + (phase ? utf8ToCp437(phase) : String("")), 1);
  // Hôte ou WiFi SSID
  if (hostLabel && *hostLabel) {
    printClipped(0, 8, String("Host: ") + utf8ToCp437(hostLabel), 1);
  }
  // Versions
  if (currentVersion && *currentVersion) {
    printClipped(0, 16, String("Cur: v") + currentVersion, 1);
  }
  if (newVersion && *newVersion) {
    printClipped(72, 16, String("New: v") + newVersion, 1);
  }
  // Partitions
  if (fromLabel && *fromLabel) printClipped(0, 24, String("From:") + utf8ToCp437(fromLabel), 1);
  if (toLabel && *toLabel)     printClipped(64, 24, String("To:")   + utf8ToCp437(toLabel), 1);

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
  printClipped(0, 52, String(pbuf), 1);

  _disp.display();
  _needsFlush = false;
  _immediateMode = oldImmediateMode;

  _isDisplaying = false;
}

void DisplayView::showSleepReason(const char* cause, const char* detailLine1, const char* detailLine2,
                                  uint16_t lockMs, bool mailBlink){
  if(!_present || splashActive()) return;
  lockScreen(lockMs);
  _disp.clearDisplay();
  resetMainCache();
  resetStatusCache();
  bool oldImmediateMode = _immediateMode;
  _immediateMode = true;
  printClipped(0, 0, String("Light-sleep"), 1);
  if (cause && *cause) printClipped(0, 10, utf8ToCp437(cause), 1);
  if (detailLine1 && *detailLine1) printClipped(0, 20, utf8ToCp437(detailLine1), 1);
  if (detailLine2 && *detailLine2) printClipped(0, 30, utf8ToCp437(detailLine2), 1);
  // Statut bar with mail icon if blinking requested (force draw even when locked)
  drawStatusEx(0, 0, -127, mailBlink, 0, 0, true);
  _disp.display();
  _immediateMode = oldImmediateMode;
}

void DisplayView::showSleepInfo(const char* reason, const char* detail1, const char* detail2, uint32_t lockMs) {
  if(!_present || splashActive() || _isDisplaying) return;

  _isDisplaying = true;

  // Nettoyage et lock pour empêcher la superposition
  _disp.clearDisplay();
  resetMainCache();
  resetStatusCache();
  lockScreen(lockMs);

  bool oldImmediateMode = _immediateMode;
  _immediateMode = true;

  // Titre
  printClipped(0, 0, String("Light-sleep"), 1);
  // Raison explicite
  if (reason && *reason) {
    printClipped(0, 10, String("Raison: ") + utf8ToCp437(reason), 1);
  }
  // Détails optionnels
  if (detail1 && *detail1) {
    printClipped(0, 20, utf8ToCp437(detail1), 1);
  }
  if (detail2 && *detail2) {
    printClipped(0, 28, utf8ToCp437(detail2), 1);
  }

  // Icône lune simple
  // Note: drawCircle et fillCircle ne sont pas disponibles dans cette version d'Adafruit_SSD1306
  // Utilisation d'alternatives ou suppression si non critique
  // _disp.drawCircle(118, 8, 6, WHITE);
  // _disp.fillCircle(121, 8, 6, BLACK);

  _disp.display();
  _needsFlush = false;
  _immediateMode = oldImmediateMode;

  _isDisplaying = false;
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
  printClipped(x, y, String(percentStr), 1);
  
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