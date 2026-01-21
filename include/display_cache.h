#pragma once

#include <Arduino.h>
#include <math.h>

class DisplayCache {
 public:
  struct StatusCache {
    bool update(int8_t sendState,
                int8_t recvState,
                int8_t rssi,
                bool mailBlink,
                int8_t tideDir,
                int diffValue,
                bool force = false) {
      bool changed = force ||
                     sendState != _sendState ||
                     recvState != _recvState ||
                     rssi != _rssi ||
                     mailBlink != _mailBlink ||
                     tideDir != _tideDir ||
                     diffValue != _diffValue;

      _sendState = sendState;
      _recvState = recvState;
      _rssi = rssi;
      _mailBlink = mailBlink;
      _tideDir = tideDir;
      _diffValue = diffValue;

      return changed;
    }

    void reset() {
      _sendState = -2;
      _recvState = -2;
      _rssi = -128;
      _mailBlink = false;
      _tideDir = 0;
      _diffValue = 0;
    }

   private:
    int8_t _sendState{-2};
    int8_t _recvState{-2};
    int8_t _rssi{-128};
    bool   _mailBlink{false};
    int8_t _tideDir{0};
    int    _diffValue{0};
  };

  struct MainCache {
    bool update(float tempEau,
                float tempAir,
                float humidite,
                uint16_t aquaLvl,
                uint16_t tankLvl,
                uint16_t potaLvl,
                uint16_t lumi,
                const char* timeStr) {
      bool changed = (fabsf(tempEau - _tempEau) > 0.1f) ||
                     (fabsf(tempAir - _tempAir) > 0.1f) ||
                     (fabsf(humidite - _humidite) > 0.5f) ||
                     (aquaLvl != _aquaLvl) ||
                     (tankLvl != _tankLvl) ||
                     (potaLvl != _potaLvl) ||
                     (abs(static_cast<int>(lumi) - static_cast<int>(_lumi)) > 1) ||
                     (timeStr == nullptr || _timeStr[0] == '\0' ? (strlen(timeStr ? timeStr : "") != strlen(_timeStr)) : strcmp(timeStr, _timeStr) != 0);

      _tempEau = tempEau;
      _tempAir = tempAir;
      _humidite = humidite;
      _aquaLvl = aquaLvl;
      _tankLvl = tankLvl;
      _potaLvl = potaLvl;
      _lumi = lumi;
      if (timeStr) {
        strncpy(_timeStr, timeStr, sizeof(_timeStr) - 1);
        _timeStr[sizeof(_timeStr) - 1] = '\0';
      } else {
        _timeStr[0] = '\0';
      }

      return changed;
    }

    void reset() {
      _tempEau = -999.0f;
      _tempAir = -999.0f;
      _humidite = -999.0f;
      _aquaLvl = 999;
      _tankLvl = 999;
      _potaLvl = 999;
      _lumi = 999;
      _timeStr[0] = '\0';
    }

   private:
    float     _tempEau{-999.0f};
    float     _tempAir{-999.0f};
    float     _humidite{-999.0f};
    uint16_t  _aquaLvl{999};
    uint16_t  _tankLvl{999};
    uint16_t  _potaLvl{999};
    uint16_t  _lumi{999};
    char      _timeStr[64];
  };

  struct VariablesCache {
    bool update(bool pumpAqua,
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
                uint16_t limFlood,
                bool force = false) {
      bool changed = force ||
                     pumpAqua != _pumpAqua ||
                     pumpTank != _pumpTank ||
                     heater != _heater ||
                     light != _light ||
                     hMat != _hMat ||
                     hMid != _hMid ||
                     hSoir != _hSoir ||
                     tPetits != _tPetits ||
                     tGros != _tGros ||
                     thAq != _thAq ||
                     thTank != _thTank ||
                     (fabsf(thHeat - _thHeat) > 0.05f) ||
                     limFlood != _limFlood;

      _pumpAqua = pumpAqua;
      _pumpTank = pumpTank;
      _heater = heater;
      _light = light;
      _hMat = hMat;
      _hMid = hMid;
      _hSoir = hSoir;
      _tPetits = tPetits;
      _tGros = tGros;
      _thAq = thAq;
      _thTank = thTank;
      _thHeat = thHeat;
      _limFlood = limFlood;

      return changed;
    }

    void reset() {
      _pumpAqua = false;
      _pumpTank = false;
      _heater = false;
      _light = false;
      _hMat = 0;
      _hMid = 0;
      _hSoir = 0;
      _tPetits = 0;
      _tGros = 0;
      _thAq = 0;
      _thTank = 0;
      _thHeat = 0.0f;
      _limFlood = 0;
    }

   private:
    bool _pumpAqua{false};
    bool _pumpTank{false};
    bool _heater{false};
    bool _light{false};
    uint8_t _hMat{0};
    uint8_t _hMid{0};
    uint8_t _hSoir{0};
    uint16_t _tPetits{0};
    uint16_t _tGros{0};
    uint16_t _thAq{0};
    uint16_t _thTank{0};
    float _thHeat{0.0f};
    uint16_t _limFlood{0};
  };

  StatusCache& status() { return _status; }
  MainCache& main() { return _main; }
  VariablesCache& variables() { return _variables; }

  const StatusCache& status() const { return _status; }
  const MainCache& main() const { return _main; }
  const VariablesCache& variables() const { return _variables; }

  void resetStatus() { _status.reset(); }
  void resetMain() { _main.reset(); }
  void resetVariables() { _variables.reset(); }

 private:
  StatusCache _status;
  MainCache _main;
  VariablesCache _variables;
};


