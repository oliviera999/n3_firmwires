#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <type_traits>

#include "web_client.h"
#include "config_manager.h"
#include "system_sensors.h"
#include "system_actuators.h"
#include "data_queue.h"

class Automatism;

class AutomatismNetwork {
 public:
  AutomatismNetwork(WebClient& web, ConfigManager& cfg);

  bool begin();

  bool fetchRemoteState(ArduinoJson::JsonDocument& doc);

  bool sendFullUpdate(
      const SensorReadings& readings,
      SystemActuators& acts,
      int diffMaree,
      uint8_t bouffeMatin, uint8_t bouffeMidi, uint8_t bouffeSoir,
      uint16_t tempsGros, uint16_t tempsPetits,
      const String& bouffePetits, const String& bouffeGros,
      bool forceWakeUp, uint16_t freqWakeSec,
      uint32_t refillDurationSec,
      const char* extraPairs = nullptr);

  void applyConfigFromJson(const ArduinoJson::JsonDocument& doc);

  bool pollRemoteState(ArduinoJson::JsonDocument& doc, uint32_t currentMillis, Automatism& autoCtrl);

  bool handleResetCommand(const ArduinoJson::JsonDocument& doc, Automatism& autoCtrl);

  void parseRemoteConfig(const ArduinoJson::JsonDocument& doc, Automatism& autoCtrl);

  void handleRemoteFeedingCommands(const ArduinoJson::JsonDocument& doc, Automatism& autoCtrl);

  void handleRemoteActuators(const ArduinoJson::JsonDocument& doc, Automatism& autoCtrl);

  void checkCriticalChanges(const SensorReadings& readings);

  uint16_t replayQueuedData();

  bool sendCommandAck(const char* command, const char* status);

  void logRemoteCommandExecution(const char* command, bool success);

            void setEmailAddress(const char* address);
  void setEmailEnabled(bool enabled) { _emailEnabled = enabled; }
  void setFreqWakeSec(uint16_t freq) { _freqWakeSec = freq; }

            const char* getEmailAddress() const { return _emailAddress; }
  bool isEmailEnabled() const { return _emailEnabled; }
  uint16_t getFreqWakeSec() const { return _freqWakeSec; }
  uint16_t getLimFlood() const { return _limFlood; }
  uint16_t getAqThresholdCm() const { return _aqThresholdCm; }
  uint16_t getTankThresholdCm() const { return _tankThresholdCm; }
  float getHeaterThresholdC() const { return _heaterThresholdC; }
  bool isServerOk() const { return _serverOk; }
  int8_t getSendState() const { return _sendState; }
  int8_t getRecvState() const { return _recvState; }
  int8_t getLastSendState() const { return _sendState; }

 private:
  static constexpr uint16_t QUEUE_HIGH_WATER = 25;
  static constexpr uint16_t QUEUE_MAX_ENTRIES = 40;
  static constexpr size_t MAX_PAYLOAD_BYTES = 960;
  static constexpr uint32_t BASE_BACKOFF_MS = 2000;
  static constexpr uint32_t MAX_BACKOFF_MS = 60000;
  static constexpr unsigned long SEND_INTERVAL_MS = 120000;
  static constexpr unsigned long REMOTE_FETCH_INTERVAL_MS = 4000;

  WebClient& _web;
  ConfigManager& _config;
  DataQueue _dataQueue;

            char _emailAddress[EmailConfig::MAX_EMAIL_LENGTH];
  bool _emailEnabled;
  uint16_t _freqWakeSec;

  uint16_t _limFlood;
  uint16_t _aqThresholdCm;
  uint16_t _tankThresholdCm;
  float _heaterThresholdC;

  unsigned long _lastSend;
  unsigned long _lastRemoteFetch;

  bool _serverOk;
  int8_t _sendState;
  int8_t _recvState;

  bool _prevPumpTank;
  bool _prevPumpAqua;
  bool _prevBouffeMatin;
  bool _prevBouffeMidi;
  bool _prevBouffeSoir;

  uint8_t _consecutiveSendFailures{0};
  uint32_t _currentBackoffMs{0};
  uint32_t _lastSendAttemptMs{0};
  uint32_t _lastBackoffLogMs{0};
  uint32_t _lastSendDurationMs{0};
  uint16_t _queueHighWaterMark{0};
  uint32_t _maxPayloadBytesSeen{0};
  uint32_t _lastHeapBeforeSend{0};
  uint32_t _lastHeapAfterSend{0};
  uint32_t _totalSendAttempts{0};
  uint32_t _totalSendSuccesses{0};
  uint32_t _totalSendFailures{0};
  uint32_t _totalPayloadBytesSent{0};
  uint32_t _totalSendDurationMs{0};

  bool canAttemptSend(uint32_t nowMs) const;
  void registerSendResult(bool success, size_t payloadBytes, uint32_t durationMs, uint32_t heapBefore, uint32_t heapAfter);
  void logQueueState(const char* reason, uint16_t size) const;

  static bool isTrue(ArduinoJson::JsonVariantConst v);
  static bool isFalse(ArduinoJson::JsonVariantConst v);

  template<typename T>
  static void assignIfPresent(const ArduinoJson::JsonDocument& doc, const char* key, T& var) {
    if (!doc.containsKey(key)) return;
    T value = doc[key].as<T>();
    if constexpr (std::is_arithmetic_v<T>) {
      if (value == 0) return;
    }
    var = value;
  }
};
