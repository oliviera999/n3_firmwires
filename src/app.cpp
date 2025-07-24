#include <Arduino.h>
#include "wifi_manager.h"
#include "system_sensors.h"
#include "system_actuators.h"
#include "display_view.h"
#include "mailer.h"
#include "power.h"
#include "diagnostics.h"
#include "pins.h"
#include "constants.h"
#include "secrets.h"
#include "config.h"
#include "automatism.h"
#include "web_client.h"
#include "web_server.h"
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <esp_task_wdt.h>

// ---------- Globals ----------
// Timeout connexion WiFi ramené à 12 s (deux tentatives successives couvrent plus de cas)
WifiManager wifi(Secrets::WIFI_LIST, Secrets::WIFI_COUNT, 12000);
DisplayView oled;
PowerManager power;
Mailer mailer;
WebClient web;
ConfigManager config;

SystemSensors sensors;
SystemActuators acts;
Diagnostics diag;
WebServerManager webSrv(sensors, acts, diag);
Automatism autoCtrl(sensors, acts, web, oled, power, mailer, config);

QueueHandle_t sensorQueue;

void sensorTask(void* pv) {
  SensorReadings r;
  static bool wdtReg=false;
  if(!wdtReg){ esp_task_wdt_add(NULL); wdtReg=true; }
  for (;;) {
    r = sensors.read();
    if (sensorQueue) xQueueSendToBack(sensorQueue, &r, 0);
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(1000)); // 1 s
  }
}

void automationTask(void* pv) {
  SensorReadings r;
  unsigned long lastHb=0;
  for (;;) {
    if (xQueueReceive(sensorQueue, &r, portMAX_DELAY) == pdTRUE) {
      static bool wdtReg=false;
      if(!wdtReg){ esp_task_wdt_add(NULL); wdtReg=true; }
      autoCtrl.update(r);
      power.resetWatchdog();
      diag.update();
      unsigned long now = millis();
      if (now - lastHb > 30000) {
        web.sendHeartbeat(diag);
        lastHb = now;
      }
    }
  }
}

void setup() {
  // Configure the native TWDT to 120 s before anything lengthy
  esp_task_wdt_deinit();
  esp_task_wdt_config_t cfg = {};
  cfg.timeout_ms = 120000;
  cfg.idle_core_mask = (1 << 0) | (1 << 1);
  cfg.trigger_panic = true;
  esp_task_wdt_init(&cfg);
  delay(200);
  Serial.begin(115200);
  
  Serial.printf("[App] Démarrage FFP3CS v%s\n", Config::VERSION);
  
  // Initialisation de la gestion du temps (avec restauration depuis la flash)
  power.initTime();
  
  // Configuration NTP
  power.setNTPConfig(3600, 0, "pool.ntp.org");
  
  // Init OLED early
  oled.begin();
  oled.showDiagnostic("WiFi...");

  // Connexion WiFi
  if (!wifi.connect(&oled)) {
    Serial.println(F("[App] WiFi non connecté - lancement AP de secours"));
    oled.showDiagnostic("AP secours");
    wifi.startFallbackAP();
  } else {
    // Synchronisation NTP si WiFi disponible
    oled.showDiagnostic("NTP sync");
    power.syncTimeFromNTP();

    // Initialisation ArduinoOTA classique
    ArduinoOTA.setPort(3232);
    ArduinoOTA.setHostname("ffp3");
    ArduinoOTA
      .onStart([]() { Serial.println("[OTA] Début de la mise à jour"); })
      .onEnd([]() { Serial.println("[OTA] Fin de la mise à jour"); })
      .onError([](ota_error_t error) { Serial.printf("[OTA] Erreur %u\n", error); });
    ArduinoOTA.begin();
  }
  
  // Initialisation des composants (déjà OLED ok)
  oled.showDiagnostic("Sensors");
  sensors.begin();
  oled.showDiagnostic("Actuators");
  acts.begin();
  oled.showDiagnostic("WebSrv");
  webSrv.begin();
  oled.showDiagnostic("Diag");
  diag.begin();
  
  // Initialisation des systèmes
  oled.showDiagnostic("Systems");
  power.initWatchdog();
  mailer.begin();
  autoCtrl.begin();

  // --- Première synchronisation distante -----------------------------
  if (WiFi.status() == WL_CONNECTED) {
    // 1) Récupération des variables distantes
    StaticJsonDocument<4096> tmp;  // taille confortable
    web.fetchRemoteState(tmp);

    // 2) Première mesure envoyée, incluant resetMode=0 pour acquitter le flag distant
    SensorReadings rs = sensors.read();
    Measurements ms{};
    ms.tempAir   = rs.tempAir;
    ms.humid     = rs.humidity;
    ms.tempWater = rs.tempWater;
    ms.wlPota    = rs.wlPota;
    ms.wlAqua    = rs.wlAqua;
    ms.wlTank    = rs.wlTank;
    ms.diffMaree = sensors.diffMaree(rs.wlAqua);
    ms.luminosite = rs.luminosite;
    ms.voltage   = rs.voltageMv;
    ms.pumpAqua  = acts.isAquaPumpRunning();
    ms.pumpTank  = acts.isTankPumpRunning();
    ms.heater    = acts.isHeaterOn();
    ms.light     = acts.isLightOn();
    web.sendMeasurements(ms, true);
  }

  oled.showDiagnostic("Init ok");
  
  // Chargement des configurations persistantes
  config.loadBouffeFlags();
  
  // Création queue & tasks FreeRTOS
  sensorQueue = xQueueCreate(5, sizeof(SensorReadings));
  if (sensorQueue) {
    xTaskCreatePinnedToCore(sensorTask, "sensorTask", 4096, nullptr, 1, nullptr, 0); // core 0
    xTaskCreatePinnedToCore(automationTask, "autoTask", 6144, nullptr, 1, nullptr, 1);
  }
  
  Serial.println(F("[App] Initialisation terminée"));
}

void loop() {
  power.updateTime();
  ArduinoOTA.handle();
  wifi.loop(&oled);
  power.resetWatchdog();
  vTaskDelay(pdMS_TO_TICKS(50));
} 