#include "camera_remote.h"

#include <ArduinoJson.h>

#include "config.h"
#include "credentials.h"
#include "n3_data.h"

static bool readBoolValue(JsonVariantConst value, bool defaultValue) {
  if (value.isNull()) return defaultValue;
  if (value.is<bool>()) return value.as<bool>();
  if (value.is<int>()) return value.as<int>() == 1;
  if (value.is<const char*>()) {
    const char* raw = value.as<const char*>();
    if (!raw) return defaultValue;
    String s(raw);
    s.toLowerCase();
    return (s == "1" || s == "true" || s == "checked" || s == "on" || s == "oui");
  }
  return defaultValue;
}

static uint32_t readU32Value(JsonVariantConst value, uint32_t defaultValue) {
  if (value.isNull()) return defaultValue;
  if (value.is<unsigned int>()) return value.as<unsigned int>();
  if (value.is<int>()) {
    int v = value.as<int>();
    return v > 0 ? static_cast<uint32_t>(v) : defaultValue;
  }
  if (value.is<const char*>()) {
    const char* raw = value.as<const char*>();
    if (!raw || raw[0] == '\0') return defaultValue;
    long v = atol(raw);
    return v > 0 ? static_cast<uint32_t>(v) : defaultValue;
  }
  return defaultValue;
}

bool cameraRemoteFetchConfig(CameraRemoteConfig& outConfig, unsigned int* outHttpCode) {
  outConfig.mail = "";
  outConfig.mailNotif = false;
  outConfig.forceWakeUp = false;
  outConfig.sleepTimeSeconds = TIME_TO_SLEEP;
  outConfig.resetMode = false;

  String payload = n3DataGet(REMOTE_OUTPUTS_STATE_URL, outHttpCode);
  if (outHttpCode && *outHttpCode != 200U) {
    return false;
  }
  if (payload.length() == 0) {
    return false;
  }

  StaticJsonDocument<1024> doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("[REMOTE] JSON invalide: %s\n", err.c_str());
    return false;
  }

  JsonVariantConst mailVal = doc["102"];
  if (!mailVal.isNull()) {
    if (mailVal.is<const char*>()) {
      const char* raw = mailVal.as<const char*>();
      if (raw) outConfig.mail = raw;
    } else {
      outConfig.mail = String(mailVal.as<int>());
    }
  }

  outConfig.mailNotif = readBoolValue(doc["103"], false);
  outConfig.forceWakeUp = readBoolValue(doc["104"], false);
  outConfig.sleepTimeSeconds = readU32Value(doc["105"], TIME_TO_SLEEP);
  if (outConfig.sleepTimeSeconds < REMOTE_SLEEP_MIN_SECONDS) outConfig.sleepTimeSeconds = REMOTE_SLEEP_MIN_SECONDS;
  if (outConfig.sleepTimeSeconds > REMOTE_SLEEP_MAX_SECONDS) outConfig.sleepTimeSeconds = REMOTE_SLEEP_MAX_SECONDS;
  outConfig.resetMode = readBoolValue(doc["106"], false);

  return true;
}

int cameraRemotePostFirmwareVersion(const char* targetName) {
  N3DataField fields[] = {
    {"api_key", API_KEY},
    {"version", FIRMWARE_VERSION},
    {"board", String(REMOTE_BOARD_ID)},
    {"sensor", String(targetName ? targetName : "cam")},
  };

  N3PostConfig cfg = {};
  cfg.url = REMOTE_VERSION_POST_URL;
  cfg.apiKey = API_KEY;
  cfg.fields = fields;
  cfg.fieldCount = sizeof(fields) / sizeof(fields[0]);
  cfg.onSending = nullptr;
  cfg.onResult = nullptr;

  return n3DataPost(cfg);
}

