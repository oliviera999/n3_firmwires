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

void Feeder::setRestAngle(int angle) {
  if (angle < 0) angle = 0;
  if (angle > 180) angle = 180;
  _rest = angle;
  if (_isAttached) {
    _servo.write(_rest);
  }
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
  // v14.02 (m4): durée 0 -> au moins 1 s. Une consigne 0 (valeur NVS corrompue ou
  // setter sans validation) armerait des esp_timer à 0 µs (comportement indéfini) et
  // finaliserait le cycle immédiatement. On borne au minimum distribuable.
  if (durationSec == 0) durationSec = 1;
  if (!_isAttached) begin();
  _intermediateAngle = intermediateAngle; // stockage de l'angle intermédiaire
  _servo.write(feedAngle); // position de nourrissage
  // v15.08 (point 2): tracer l'ordre de distribution pour lever l'ambiguïté « ordre non
  // émis » vs « ordre émis mais non exécuté » lors du diagnostic servo.
  LOG(LOG_INFO, "Servo GPIO%d -> distribution %d° (maintien %us)", _gpio, feedAngle, durationSec);

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
  esp_err_t interErr = esp_timer_start_once(_intermediateTimer,
                                            static_cast<uint64_t>(durationSec) * 1000000ULL);

  // Timer pour le retour au repos (après durationSec + durationSec/2)
  uint64_t totalDuration = static_cast<uint64_t>(durationSec + durationSec/2) * 1000000ULL;
  esp_err_t restErr = esp_timer_start_once(_timer, totalDuration);

  // v15.08 (point 3): fail-safe. Sans ces timers, le servo resterait bloqué en position de
  // distribution (trappe ouverte) indéfiniment. Si l'un des deux ne s'arme pas, on annule
  // et on ramène immédiatement au repos plutôt que de laisser la trappe ouverte.
  if (interErr != ESP_OK || restErr != ESP_OK) {
    LOG(LOG_ERROR, "Servo GPIO%d : échec armement timers (inter=%d rest=%d) -> retour repos",
        _gpio, static_cast<int>(interErr), static_cast<int>(restErr));
    esp_timer_stop(_intermediateTimer);
    esp_timer_stop(_timer);
    returnToRest();
  }
}

void Feeder::goToIntermediate() {
  _servo.write(_intermediateAngle);
  // v15.08 (point 2): tracer la transition vers l'intermédiaire.
  LOG(LOG_INFO, "Servo GPIO%d -> intermédiaire %d°", _gpio, _intermediateAngle);
}

void Feeder::returnToRest() {
  _servo.write(_rest);
  // v15.08 (point 2): tracer le retour au repos (fin de cycle de distribution).
  LOG(LOG_INFO, "Servo GPIO%d -> repos %d°", _gpio, _rest);
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
  // v11.190: Ne pas attacher si pin invalide (255 = "no pin" ESP32, ou hors plage 0-39)
  if (_gpio == 255 || _gpio > 39) {
    return;
  }
  // Annuler tout detach programmé pour éviter les conflits LEDC
  if (_detachTimer != nullptr) {
    esp_timer_stop(_detachTimer);
  }
  if (!_isAttached) {
    // P2: Libérer état LEDC résiduel avant attach (évite "Pin already attached to LEDC")
    _servo.detach();
    vTaskDelay(pdMS_TO_TICKS(50));
    _servo.attach(_gpio, 500, 2500);  // Min/Max pulse width optimisés
    // v15.08 (point 1): ne marquer « attaché » que si le driver LEDC a réellement pris la
    // main. Auparavant _isAttached passait à true inconditionnellement : sur un attach
    // échoué (LEDC indisponible), le premier write — l'angle de distribution — était perdu
    // silencieusement, ne laissant voir que l'ordre intermédiaire émis plus tard.
    _isAttached = _servo.attached();
    if (!_isAttached) {
      LOG(LOG_ERROR, "Servo GPIO%d : échec attach (LEDC indisponible)", _gpio);
      return;
    }
    LOG(LOG_INFO, "Servo GPIO%d attaché (500-2500μs)", _gpio);
    // Laisse quelques trames PWM s'établir après l'attach avant le premier ordre de
    // position, pour que la position commandée (distribution) soit effectivement prise.
    vTaskDelay(pdMS_TO_TICKS(20));
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