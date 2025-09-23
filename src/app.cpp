#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// Include project headers
#include "project_config.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "sensors.h"
#include "actuators.h"
#include "automatism.h"
#include "display_view.h"
#include "ota_manager.h"
#include "time_drift_monitor.h"
#include "diagnostics.h"
#include "event_log.h"
#include "power.h"
#include "mailer.h"
#include "web_client.h"
#include "realtime_websocket.h"
#include "sensor_cache.h"
#include "pump_stats_cache.h"
#include "psram_optimizer.h"
#include "config_manager.h"

// Global objects
Preferences preferences;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
SemaphoreHandle_t xSensorMutex;
SemaphoreHandle_t xConfigMutex;
TaskHandle_t sensorTaskHandle = NULL;
TaskHandle_t webTaskHandle = NULL;
TaskHandle_t automatismTaskHandle = NULL;

// System state
bool systemInitialized = false;
unsigned long lastHeapReport = 0;
const unsigned long HEAP_REPORT_INTERVAL = 30000; // 30 seconds

// FreeRTOS Tasks
void sensorTask(void *parameter) {
    const TickType_t xFrequency = pdMS_TO_TICKS(5000); // 5 seconds
    TickType_t xLastWakeTime = xTaskGetTickCount();
    
    while (true) {
        if (xSemaphoreTake(xSensorMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            // Read all sensors
            readSensors();
            
            // Update cache
            updateSensorCache();
            
            // Check thresholds
            checkSensorThresholds();
            
            xSemaphoreGive(xSensorMutex);
        }
        
        // Feed watchdog
        esp_task_wdt_reset();
        
        // Wait for next cycle
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void webServerTask(void *parameter) {
    while (true) {
        // Process web server events
        processWebServerEvents();
        
        // Handle WebSocket cleanup
        ws.cleanupClients();
        
        // Send real-time data to connected clients
        if (ws.count() > 0) {
            sendRealtimeData();
        }
        
        vTaskDelay(pdMS_TO_TICKS(100)); // 100ms
    }
}

void automatismTask(void *parameter) {
    const TickType_t xFrequency = pdMS_TO_TICKS(1000); // 1 second
    TickType_t xLastWakeTime = xTaskGetTickCount();
    
    while (true) {
        // Process automation rules
        processAutomatismRules();
        
        // Update actuator states
        updateActuatorStates();
        
        // Check safety conditions
        checkSafetyConditions();
        
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 5000) {
        delay(10);
    }
    
    Serial.println(F("\n=== ESP32 System Starting ==="));
    Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
    
    // Initialize watchdog
    esp_task_wdt_init(30, true); // 30 second timeout
    esp_task_wdt_add(NULL);
    
    // Initialize PSRAM if available
    #ifdef BOARD_HAS_PSRAM
    if (psramInit()) {
        Serial.printf("PSRAM initialized. Size: %d bytes\n", ESP.getPsramSize());
        initPsramOptimizer();
    }
    #endif
    
    // Initialize NVS
    preferences.begin("system", false);
    
    // Load configuration from NVS
    loadConfiguration();
    
    // Initialize SPIFFS
    if (!SPIFFS.begin(true)) {
        Serial.println(F("SPIFFS Mount Failed"));
        ESP.restart();
    }
    Serial.printf("SPIFFS: %d bytes used of %d\n", SPIFFS.usedBytes(), SPIFFS.totalBytes());
    
    // Create mutexes
    xSensorMutex = xSemaphoreCreateMutex();
    xConfigMutex = xSemaphoreCreateMutex();
    
    if (xSensorMutex == NULL || xConfigMutex == NULL) {
        Serial.println(F("Failed to create mutex"));
        ESP.restart();
    }
    
    // Initialize WiFi
    initWiFiManager();
    
    // Initialize components
    initSensors();
    initActuators();
    initDisplay();
    initWebServer(&server, &ws);
    initOTA();
    initTimeDriftMonitor();
    initDiagnostics();
    initEventLog();
    initMailer();
    
    // Create FreeRTOS tasks
    xTaskCreatePinnedToCore(
        sensorTask,
        "SensorTask",
        8192,
        NULL,
        2,
        &sensorTaskHandle,
        0
    );
    
    xTaskCreatePinnedToCore(
        webServerTask,
        "WebTask",
        16384,
        NULL,
        1,
        &webTaskHandle,
        1
    );
    
    xTaskCreatePinnedToCore(
        automatismTask,
        "AutoTask",
        8192,
        NULL,
        1,
        &automatismTaskHandle,
        0
    );
    
    // Start web server
    server.begin();
    
    systemInitialized = true;
    Serial.println(F("=== System Initialized Successfully ==="));
}

void loop() {
    // Main loop handles system monitoring
    
    // Feed watchdog
    esp_task_wdt_reset();
    
    // Handle OTA updates
    handleOTA();
    
    // Monitor heap memory
    unsigned long now = millis();
    if (now - lastHeapReport > HEAP_REPORT_INTERVAL) {
        lastHeapReport = now;
        size_t freeHeap = ESP.getFreeHeap();
        size_t minFreeHeap = ESP.getMinFreeHeap();
        
        Serial.printf("Heap - Free: %d, Min: %d", freeHeap, minFreeHeap);
        
        #ifdef BOARD_HAS_PSRAM
        Serial.printf(", PSRAM Free: %d", ESP.getFreePsram());
        #endif
        
        Serial.println();
        
        // Check for low memory
        if (freeHeap < 20000) {
            Serial.println(F("WARNING: Low memory detected!"));
            logEvent("LOW_MEMORY", String(freeHeap));
            
            // Try to free memory
            ws.cleanupClients();
            
            if (freeHeap < 10000) {
                Serial.println(F("CRITICAL: Very low memory! Restarting..."));
                ESP.restart();
            }
        }
    }
    
    // Process time drift monitoring
    processTimeDrift();
    
    // Process diagnostics
    processDiagnostics();
    
    // Send data to remote server
    if (WiFi.isConnected()) {
        sendDataToServer();
    }
    
    // Small delay to prevent watchdog issues
    delay(10);
}

void loadConfiguration() {
    // Load all configuration from NVS [[memory:5580064]]
    Serial.println(F("Loading configuration from NVS..."));
    
    // WiFi configuration
    String ssid = preferences.getString("wifi_ssid", "");
    String password = preferences.getString("wifi_pass", "");
    
    // Server configuration  
    String serverUrl = preferences.getString("server_url", "");
    String apiKey = preferences.getString("api_key", "");
    
    // Sensor configuration
    float tempOffset = preferences.getFloat("temp_offset", 0.0);
    float humOffset = preferences.getFloat("hum_offset", 0.0);
    
    // Actuator configuration
    int pumpDuration = preferences.getInt("pump_duration", 60);
    int fanThreshold = preferences.getInt("fan_threshold", 28);
    
    // System configuration
    bool otaEnabled = preferences.getBool("ota_enabled", true);
    int logLevel = preferences.getInt("log_level", 2);
    
    Serial.println(F("Configuration loaded successfully"));
}

void sendDataToServer() {
    static unsigned long lastSend = 0;
    const unsigned long SEND_INTERVAL = 300000; // 5 minutes
    
    if (millis() - lastSend > SEND_INTERVAL) {
        lastSend = millis();
        
        // Prepare data according to PHP script requirements [[memory:5580054]]
        JsonDocument doc;
        
        // Add all required fields in exact order
        doc["timestamp"] = millis();
        doc["temperature"] = getCurrentTemperature();
        doc["humidity"] = getCurrentHumidity();
        doc["pressure"] = getCurrentPressure();
        doc["light"] = getCurrentLight();
        doc["soil_moisture"] = getSoilMoisture();
        doc["water_level"] = getWaterLevel();
        doc["pump_state"] = getPumpState();
        doc["fan_state"] = getFanState();
        doc["light_state"] = getLightState();
        doc["free_heap"] = ESP.getFreeHeap();
        doc["uptime"] = millis() / 1000;
        doc["wifi_rssi"] = WiFi.RSSI();
        doc["error_count"] = getErrorCount();
        doc["last_error"] = getLastError();
        
        // Send to server
        if (sendJsonToServer(doc)) {
            Serial.println(F("Data sent to server successfully"));
        } else {
            Serial.println(F("Failed to send data to server"));
        }
    }
}