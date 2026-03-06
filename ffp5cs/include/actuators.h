#pragma once
#include <Arduino.h>
#include <ESP32Servo.h>
#include <esp_timer.h>
#include "pins.h"
#include "log.h" // Pour LOG

class PumpController {
 public:
  PumpController(int gpio) : _gpio(gpio) { pinMode(_gpio, OUTPUT); off(); }
  void on()  { digitalWrite(_gpio, HIGH); _state = true; LOG(LOG_INFO, "Pump GPIO%d ON", _gpio); }
  void off() { digitalWrite(_gpio, LOW);  _state = false; LOG(LOG_INFO, "Pump GPIO%d OFF", _gpio); }
  bool state() const { return _state; }
 private:
  int  _gpio;
  bool _state{false};
};

class HeaterController {
 public:
  HeaterController(int gpio) : _gpio(gpio) { pinMode(_gpio, OUTPUT); off(); }
  void on()  { digitalWrite(_gpio, HIGH); _state = true; LOG(LOG_INFO, "Heater GPIO%d ON", _gpio); }
  void off() { digitalWrite(_gpio, LOW);  _state = false; LOG(LOG_INFO, "Heater GPIO%d OFF", _gpio); }
  bool state() const { return _state; }
 private:
  int _gpio;
  bool _state{false};
};

class LightController {
 public:
  LightController(int gpio) : _gpio(gpio) { pinMode(_gpio, OUTPUT); off(); }
  void on()  { digitalWrite(_gpio, HIGH); _state = true; LOG(LOG_INFO, "Light GPIO%d ON", _gpio); }
  void off() { digitalWrite(_gpio, LOW);  _state = false; LOG(LOG_INFO, "Light GPIO%d OFF", _gpio); }
  bool state() const { return _state; }
 private:
  int _gpio;
  bool _state{false};
};

class Feeder {
 public:
  Feeder(int gpio, int restAngle = 88) : _gpio(gpio), _rest(restAngle) {}
  void begin();
  void dispense(int angle, uint16_t durationSec);
  void dispenseWithIntermediate(int feedAngle, int intermediateAngle, uint16_t durationSec);
  void returnToRest();
  void goToIntermediate();
  void detach();
  bool isAttached() const { return _isAttached; }
 private:
  int _gpio;
  int _rest;
  int _intermediateAngle{45}; // angle par défaut pour la position intermédiaire
  Servo _servo;
  bool _isAttached{false};
  esp_timer_handle_t _timer{nullptr};
  esp_timer_handle_t _intermediateTimer{nullptr};
  esp_timer_handle_t _detachTimer{nullptr};
}; 