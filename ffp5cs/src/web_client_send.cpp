// web_client_send.cpp — opérations API sortantes de WebClient :
// sendMeasurements / tryFetchConfigFromServer / tryPushStatusToServer.
// Extrait de web_client.cpp (audit : découpe god-file). Méthodes membres,
// comportement identique ; délèguent à postRaw()/fetchRemoteState() (déclarés
// dans web_client.h, définis dans web_client.cpp).
#include "web_client.h"        // Arduino, HTTPClient, ArduinoJson, config.h, WiFi
#include "config_manager.h"    // ConfigManager (config.isRemote*Enabled)
#include "gpio_mapping.h"      // SensorMap, GPIOMap (noms POST centralisés)

extern ConfigManager config;   // défini dans app.cpp


bool WebClient::sendMeasurements(const Measurements& m, bool includeReset) {
  // Validation des mesures
  float tempWater = m.tempWater;
  float tempAir = m.tempAir;
  float humidity = m.humidity;

  if (isnan(tempWater) || tempWater <= 0.0f || tempWater >= 60.0f) {
    tempWater = NAN;
  }
  if (isnan(tempAir) || tempAir <= SensorConfig::AirSensor::TEMP_MIN || tempAir >= SensorConfig::AirSensor::TEMP_MAX) {
    tempAir = NAN;
  }
  if (isnan(humidity) || humidity < SensorConfig::AirSensor::HUMIDITY_MIN || humidity > SensorConfig::AirSensor::HUMIDITY_MAX) {
    humidity = NAN;
  }

  auto fmtFloat = [](float v, char* buf, size_t bufSize) {
    if (isnan(v)) {
      buf[0] = '\0';
    } else {
      snprintf(buf, bufSize, "%.1f", v);
    }
  };
  auto fmtUltrasonic = [](uint16_t v, char* buf, size_t bufSize) {
    if (v == 0) {
      buf[0] = '\0';
    } else {
      snprintf(buf, bufSize, "%u", (unsigned)v);
    }
  };

  char payload[512];
  payload[0] = '\0';
  size_t offset = 0;
  bool truncated = false;
  
  auto appendKV = [&](const char* key, const char* value) {
    if (!value || value[0] == '\0') {
      return;
    }
    if (truncated || offset >= sizeof(payload) - 1) {
      truncated = true;
      return;
    }
    size_t remaining = sizeof(payload) - offset;
    int written = 0;
    if (offset > 0) {
      written = snprintf(payload + offset, remaining, "&");
      if (written < 0 || static_cast<size_t>(written) >= remaining) {
        truncated = true;
        return;
      }
      offset += static_cast<size_t>(written);
      remaining = sizeof(payload) - offset;
    }
    written = snprintf(payload + offset, remaining, "%s=%s", key, value);
    if (written < 0 || static_cast<size_t>(written) >= remaining) {
      truncated = true;
      return;
    }
    offset += static_cast<size_t>(written);
  };
  
  char buf_tempAir[16], buf_humid[16], buf_tempWater[16];
  char buf_wlPota[8], buf_wlAqua[8], buf_wlTank[8];
  char buf_diffMaree[16], buf_lum[16];
  char buf_pumpAqua[2], buf_pumpTank[2], buf_heat[2], buf_uv[2];
  
  fmtFloat(tempAir, buf_tempAir, sizeof(buf_tempAir));
  fmtFloat(humidity, buf_humid, sizeof(buf_humid));
  fmtFloat(tempWater, buf_tempWater, sizeof(buf_tempWater));
  fmtUltrasonic(m.wlPota, buf_wlPota, sizeof(buf_wlPota));
  fmtUltrasonic(m.wlAqua, buf_wlAqua, sizeof(buf_wlAqua));
  fmtUltrasonic(m.wlTank, buf_wlTank, sizeof(buf_wlTank));
  snprintf(buf_diffMaree, sizeof(buf_diffMaree), "%d", m.diffMaree);
  snprintf(buf_lum, sizeof(buf_lum), "%u", m.luminosite);
  snprintf(buf_pumpAqua, sizeof(buf_pumpAqua), "%d", m.pumpAqua ? 1 : 0);
  snprintf(buf_pumpTank, sizeof(buf_pumpTank), "%d", m.pumpTank ? 1 : 0);
  snprintf(buf_heat, sizeof(buf_heat), "%d", m.heater ? 1 : 0);
  snprintf(buf_uv, sizeof(buf_uv), "%d", m.light ? 1 : 0);

  // Noms POST centralisés: SensorMap (capteurs), GPIOMap (actionneurs)
  appendKV("version", ProjectConfig::VERSION);
  appendKV(SensorMap::TEMP_AIR.serverPostName, buf_tempAir);
  appendKV(SensorMap::HUMIDITY.serverPostName, buf_humid);
  appendKV(SensorMap::TEMP_WATER.serverPostName, buf_tempWater);
  appendKV(SensorMap::WL_POTA.serverPostName, buf_wlPota);
  appendKV(SensorMap::WL_AQUA.serverPostName, buf_wlAqua);
  appendKV(SensorMap::WL_TANK.serverPostName, buf_wlTank);
  appendKV(SensorMap::DIFF_MAREE.serverPostName, buf_diffMaree);
  appendKV(SensorMap::LUMINOSITY.serverPostName, buf_lum);
  appendKV(GPIOMap::PUMP_AQUA.serverPostName, buf_pumpAqua);
  appendKV(GPIOMap::PUMP_TANK.serverPostName, buf_pumpTank);
  appendKV(GPIOMap::HEATER.serverPostName, buf_heat);
  appendKV(GPIOMap::LIGHT.serverPostName, buf_uv);

  if (includeReset) {
    appendKV(GPIOMap::RESET_CMD.serverPostName, "0");
  }

  if (truncated) {
    return false;
  }
  
  return postRaw(payload);
}

int WebClient::tryFetchConfigFromServer(JsonDocument& doc) {
  if (WiFi.status() != WL_CONNECTED) return 0;
  bool recvEn = config.isRemoteRecvEnabled();
  if (LogConfig::SERIAL_ENABLED) {
    Serial.printf("[HTTP] tryFetchConfig: réception distante %s\n",
                  recvEn ? "activée → GET outputs/state" : "désactivée (skip)");
  }
  if (!recvEn) return 0;
  return fetchRemoteState(doc);
}

bool WebClient::tryPushStatusToServer(const char* payload) {
  if (WiFi.status() != WL_CONNECTED) {
    if (LogConfig::SERIAL_ENABLED) {
      Serial.println(F("[HTTP] tryPushStatusToServer: WiFi déconnecté — envoi annulé"));
    }
    return false;
  }
  if (!config.isRemoteSendEnabled()) {
    if (LogConfig::SERIAL_ENABLED) {
      Serial.println(F("[HTTP] tryPushStatusToServer: envoi distant désactivé (config) — annulé"));
    }
    return false;
  }
  if (payload == nullptr) return false;
  return postRaw(payload);
}

