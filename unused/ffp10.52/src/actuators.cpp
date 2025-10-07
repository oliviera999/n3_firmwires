#include "actuators.h"
#include <esp_timer.h>

static void servoTimerCallback(void* arg) {
  auto* feeder = static_cast<Feeder*>(arg);
  if (feeder) feeder->returnToRest();
}

static void intermediateTimerCallback(void* arg) {
  auto* feeder = static_cast<Feeder*>(arg);
  if (feeder) feeder->goToIntermediate();
}

static void servoDetachTimerCallback(void* arg) {
  auto* feeder = static_cast<Feeder*>(arg);
  if (feeder) feeder->detach();
}

void Feeder::dispense(int angle, uint16_t durationSec) {
  if (!_isAttached) begin();
  _servo.write(angle); // mouvement aller

  // (re)création du timer si nécessaire
  if (_timer == nullptr) {
    esp_timer_create_args_t args = {
        .callback = servoTimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "servoFeeder"};
    esp_timer_create(&args, &_timer);
  }
  esp_timer_stop(_timer); // au cas où il serait déjà armé
  esp_timer_start_once(_timer, static_cast<uint64_t>(durationSec) * 1000000ULL);
}

void Feeder::dispenseWithIntermediate(int feedAngle, int intermediateAngle, uint16_t durationSec) {
  if (!_isAttached) begin();
  _intermediateAngle = intermediateAngle; // stockage de l'angle intermédiaire
  _servo.write(feedAngle); // position de nourrissage

  // (re)création des timers si nécessaire
  if (_timer == nullptr) {
    esp_timer_create_args_t args = {
        .callback = servoTimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "servoFeeder"};
    esp_timer_create(&args, &_timer);
  }
  
  if (_intermediateTimer == nullptr) {
    esp_timer_create_args_t args = {
        .callback = intermediateTimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "servoFeederIntermediate"};
    esp_timer_create(&args, &_intermediateTimer);
  }
  
  // Arrêt des timers au cas où ils seraient déjà armés
  esp_timer_stop(_timer);
  esp_timer_stop(_intermediateTimer);
  
  // Timer pour la position intermédiaire (après durationSec)
  esp_timer_start_once(_intermediateTimer, static_cast<uint64_t>(durationSec) * 1000000ULL);
  
  // Timer pour le retour au repos (après durationSec + durationSec/2)
  uint64_t totalDuration = static_cast<uint64_t>(durationSec + durationSec/2) * 1000000ULL;
  esp_timer_start_once(_timer, totalDuration);
}

void Feeder::goToIntermediate() {
  _servo.write(_intermediateAngle);
}

void Feeder::returnToRest() {
  _servo.write(_rest);
  // Planifie un détachement après 400ms pour réduire jitter/consommation
  if (_detachTimer == nullptr) {
    esp_timer_create_args_t args = {
        .callback = servoDetachTimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "servoFeederDetach"};
    esp_timer_create(&args, &_detachTimer);
  }
  esp_timer_stop(_detachTimer);
  esp_timer_start_once(_detachTimer, 400000ULL); // 400 ms
}

void Feeder::begin() {
  if (!_isAttached) {
    _servo.attach(_gpio, 500, 2500); // Min/Max pulse width optimisés
    _isAttached = true;
    LOG(LOG_INFO, "Servo GPIO%d attaché (500-2500μs)", _gpio);
  }
  _servo.write(_rest);
}

void Feeder::detach() {
  if (_isAttached) {
    _servo.detach();
    _isAttached = false;
    LOG(LOG_INFO, "Servo GPIO%d détaché", _gpio);
  }
} 