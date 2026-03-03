#pragma once

#include <Arduino.h>
#include "config.h"
#include "display_cache.h"

#if FEATURE_OLED
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#else
class TwoWire { 
 public:
  void begin(int, int) {}
  void beginTransmission(uint8_t) {}
  uint8_t endTransmission() { return 0; }
};
// Color constants and defines used by SSD1306 API
#ifndef WHITE
#define WHITE 1
#endif
#ifndef BLACK
#define BLACK 0
#endif
#ifndef SSD1306_SWITCHCAPVCC
#define SSD1306_SWITCHCAPVCC 0
#endif
// Minimal stubs to satisfy references when OLED is disabled
class Adafruit_SSD1306 {
 public:
  Adafruit_SSD1306(uint8_t, uint8_t, TwoWire*, int) {}
  int16_t width() const { return 128; }
  void   clearDisplay() {}
  void   display() {}
  void   setTextColor(uint16_t) {}
  void   setTextSize(uint8_t) {}
  void   setCursor(int16_t, int16_t) {}
  void   println(const char*) {}
  template <typename T> void println(const T&) {}
  void   print(const char*) {}
  template <typename T> void print(const T&) {}
  void   printf(const char*, ...) {}
  void   fillRect(int16_t, int16_t, int16_t, int16_t, uint16_t) {}
  void   drawRect(int16_t, int16_t, int16_t, int16_t, uint16_t) {}
  void   drawTriangle(int16_t,int16_t,int16_t,int16_t,int16_t,int16_t,uint16_t) {}
  void   fillTriangle(int16_t,int16_t,int16_t,int16_t,int16_t,int16_t,uint16_t) {}
  void   drawLine(int16_t,int16_t,int16_t,int16_t,uint16_t) {}
  bool   begin(uint8_t, uint8_t) { return false; }
};
static TwoWire Wire;
#endif

struct StatusBarParams {
  int8_t sendState;
  int8_t recvState;
  int8_t rssi;
  bool mailBlink;
  int8_t tideDir;
  int diffValue;
  bool otaOverlayActive;
  uint8_t otaPercent;
};

class DisplayView {
 public:
  explicit DisplayView(uint8_t addr = 0x3C, uint8_t w = 128, uint8_t h = 64);
  bool begin();
  /** À appeler quand le mutex I2C est déjà tenu (ex. juste après runI2cScan). Pas de flush(). */
  bool beginHoldingMutex();
  bool isPresent() const { return _present; }

  // Affichages standards
  void showMain(float tempEau, float tempAir, float humidite, uint16_t aquaLvl,
                uint16_t tankLvl, uint16_t potaLvl, uint16_t lumi, const char* timeStr);
  void showVariables(bool pumpAqua,
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
                     uint16_t limFlood);
  void showDiagnostic(const char* msg);
  void showServerVars(bool pumpAqua, bool pumpTank, bool heater, bool light,
                      uint8_t hMat, uint8_t hMid, uint8_t hSoir,
                      uint16_t tPetits, uint16_t tGros,
                      uint16_t thAq, uint16_t thTank, float thHeat,
                      uint16_t tRemp, uint16_t limFlood,
                      bool wakeUp, uint16_t freqWake);
  // Nouveaux affichages
  // sendState / recvState : -1 = erreur (icône barrée), 0 = en cours (vide), 1 = OK (plein)
  // tideDir : -1 = descente, 0 = stable, 1 = montée
  // diffValue : variation (cm)
  void drawStatus(int8_t sendState, int8_t recvState, int8_t rssi, bool mailBlink,
                  int8_t tideDir = 0, int diffValue = 0);
  // Variante avec forçage pour dessiner la barre d'état même en mode verrouillé
  void drawStatusEx(int8_t sendState, int8_t recvState, int8_t rssi, bool mailBlink,
                    int8_t tideDir = 0, int diffValue = 0, bool force = false);
  void showCountdown(const char* label, uint16_t secLeft, bool isManual = false);
  void showFeedingCountdown(const char* fishType, const char* phase,
                           uint16_t secLeft, bool isManual = false);
  // Affichage spécifique OTA: progression + partitions
  void showOtaProgress(uint8_t percent, const char* fromLabel,
                      const char* toLabel, const char* phase = nullptr);
  // Version enrichie: affiche aussi versions et hôte
  void showOtaProgressEx(uint8_t percent, const char* fromLabel, const char* toLabel,
                         const char* phase, const char* currentVersion,
                         const char* newVersion, const char* hostLabel);
  
  // Affichage du pourcentage OTA en overlay (haut droite) pendant le téléchargement
  void showOtaProgressOverlay(uint8_t percent);
  void hideOtaProgressOverlay();

  // Affichage clair de la raison d'entrée en light-sleep
  void showSleepReason(const char* cause, const char* detailLine1 = nullptr, const char* detailLine2 = nullptr,
                       uint16_t lockMs = 2000, bool mailBlink = false);

  // Écran dédié d'information de mise en veille (raison claire)
  void showSleepInfo(const char* reason, const char* detail1 = nullptr, const char* detail2 = nullptr, uint32_t lockMs = 2000);

  void clear();
  void flush();
  
  // Nouvelles méthodes d'optimisation
  void beginUpdate();  // Début d'une mise à jour d'affichage
  void endUpdate();    // Fin d'une mise à jour d'affichage (flush automatique)
  void setUpdateMode(bool immediate); // Mode mise à jour immédiate ou différée
  bool needsRefresh() const; // Vérifie si un rafraîchissement est nécessaire
  void resetStatusCache(); // Réinitialise le cache des états pour forcer un redessin complet
  void resetMainCache(); // Réinitialise le cache de l'affichage principal
  void resetVariablesCache(); // Réinitialise le cache des variables pour forcer un redessin complet
  void forceClear(); // Force un nettoyage complet de l'écran

  // Verrouillage temporaire de l'écran pour éviter toute superposition
  void lockScreen(uint32_t durationMs); // Verrouille l'affichage pendant durationMs
  bool isLocked() const;                // Indique si l'écran est verrouillé
  
  // Gestion du splash screen
  void forceEndSplash();                // Force la fin du splash screen immédiatement

 private:
  class DisplaySession {
   public:
    DisplaySession(DisplayView& view, bool immediateMode, uint32_t lockMs = 0, bool setDisplaying = false);
    ~DisplaySession();

   private:
    DisplayView& _view;
    bool _prevImmediate;
    bool _setDisplaying;
    bool _prevDisplaying{false};
  };

  // Impression protégée contre le dépassement horizontal (ajoute "..." si besoin)
  void printClipped(int16_t x, int16_t y, const char* text, uint8_t size);
  
  // Vérification santé I2C avant opération d'affichage
  bool checkI2cHealth();
  void reportI2cError();
  
  // Méthodes de rendu (anciennement dans les classes renderer)
  void renderMainScreen(float tempEau, float tempAir, float humidite,
                        uint16_t aquaLvl, uint16_t tankLvl, uint16_t potaLvl,
                        uint16_t lumi, const char* timeStr, bool wifiConnected,
                        const char* stationSsid, const char* stationIp,
                        const char* apSsid, const char* apIp);
  void renderCountdown(const char* label, uint16_t secondsLeft, bool isManual);
  void renderFeedingCountdown(const char* fishType, const char* phase,
                              uint16_t secondsLeft, bool isManual);
  void renderVariables(bool pumpAqua, bool pumpTank, bool heater, bool light,
                       uint8_t hMat, uint8_t hMid, uint8_t hSoir,
                       uint16_t tPetits, uint16_t tGros,
                       uint16_t thAq, uint16_t thTank, float thHeat,
                       uint16_t limFlood);
  void renderServerVars(bool pumpAqua, bool pumpTank, bool heater, bool light,
                        uint8_t hMat, uint8_t hMid, uint8_t hSoir,
                        uint16_t tPetits, uint16_t tGros,
                        uint16_t thAq, uint16_t thTank, float thHeat,
                        uint16_t tRemp, uint16_t limFlood,
                        bool wakeUp, uint16_t freqWake);
  void renderStatusBar(const StatusBarParams& params);
  void appendDiagnosticLine(const char* line, uint8_t lineIndex);
  
  // Helpers de formatage
  static void formatLevel(uint16_t value, char* buffer, size_t size);
  static void formatTemp(float value, char* buffer, size_t size);
  static void formatHumidity(float value, char* buffer, size_t size);
  static const char* onOffLabel(bool value);
  
  bool _present{false};
  Adafruit_SSD1306 _disp;
  
  // Protection contre spam I2C - désactive l'écran si trop d'erreurs
  uint8_t _i2cErrorCount{0};
  static constexpr uint8_t I2C_ERROR_LIMIT = 5;  // Désactive après 5 erreurs consécutives
  unsigned long _lastI2cCheck{0};
  static constexpr unsigned long I2C_CHECK_INTERVAL_MS = 30000;  // Revérifier toutes les 30s
  // Index de la prochaine ligne (base 0, en lignes de texte de 8px de hauteur) utilisé par showDiagnostic
  uint8_t _diagLine{0};
  // Durée pendant laquelle le splash initial reste affiché
  mutable unsigned long _splashUntil{0};
  // Nouvelles variables d'optimisation
  bool _updateMode{false};     // Mode mise à jour en cours
  bool _needsFlush{false};     // Flag indiquant qu'un flush est nécessaire
  bool _immediateMode{true};   // Mode flush immédiat (par défaut pour compatibilité)
  bool _isDisplaying{false};   // Protection contre les appels simultanés d'affichage

  // Verrouillage d'écran (mode exclusif)
  mutable bool _locked{false};
  mutable unsigned long _lockUntil{0};

  DisplayCache _cache;

  // Etat de l'overlay OTA
  bool      _otaOverlayActive{false};
  uint8_t   _lastOtaPercent{0};

  bool splashActive() const { 
    if (_splashUntil == 0) return false;
    unsigned long now = millis();
    // Gestion du overflow de millis() (arrive après ~49 jours)
    if (now < _splashUntil) {
      return true; // Splash encore actif
    }
    // Splash expiré - remettre _splashUntil à 0 pour éviter les comparaisons futures
#if (defined(ENABLE_SERIAL_MONITOR) && (ENABLE_SERIAL_MONITOR == 1)) || !defined(PROFILE_PROD)
    Serial.printf("[OLED] Splash screen expiré (now=%lu, was=%lu)\n", now, _splashUntil);
#endif
    _splashUntil = 0;
    return false;
  }
}; 