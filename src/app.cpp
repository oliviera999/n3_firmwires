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
#include <atomic>

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
#include "watchdog_manager.h"

// Global objects
Preferences preferences;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
SemaphoreHandle_t xSensorMutex;
SemaphoreHandle_t xConfigMutex;
TaskHandle_t sensorTaskHandle = NULL;
TaskHandle_t webTaskHandle = NULL;
TaskHandle_t automatismTaskHandle = NULL;

// System state - Thread safe
std::atomic<bool> systemInitialized(false);
volatile unsigned long lastHeapReport = 0;
const unsigned long HEAP_REPORT_INTERVAL = 30000; // 30 seconds

// Fixed size buffers to avoid String fragmentation
static char logBuffer[256];
static char dataBuffer[512];
static const char* TAG = "APP";

// FreeRTOS Tasks with improved stability
void sensorTask(void *parameter) {
    const TickType_t xFrequency = pdMS_TO_TICKS(5000); // 5 seconds
    TickType_t xLastWakeTime = xTaskGetTickCount();
    
    // Register with watchdog
    esp_task_wdt_add(NULL);
    
    while (true) {
        // Improved mutex handling with timeout recovery
        BaseType_t mutexTaken = xSemaphoreTake(xSensorMutex, pdMS_TO_TICKS(5000));
        
        if (mutexTaken == pdTRUE) {
            // Read all sensors
            readSensors();
            
            // Update cache
            updateSensorCache();
            
            // Check thresholds
            checkSensorThresholds();
            
            xSemaphoreGive(xSensorMutex);
        } else {
            // Mutex timeout - log and attempt recovery
            ESP_LOGE(TAG, "Sensor mutex timeout - attempting recovery");
            // Force give mutex in case it's stuck
            xSemaphoreGive(xSensorMutex);
            vTaskDelay(pdMS_TO_TICKS(100));
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
    
    // Initialize watchdog with proper timeout
    esp_err_t wdt_err = esp_task_wdt_init(10, true); // 10 second timeout
    if (wdt_err == ESP_OK) {
        esp_task_wdt_add(NULL); // Add main task
        ESP_LOGI(TAG, "Watchdog initialized with 10s timeout");
    } else {
        ESP_LOGE(TAG, "Failed to init watchdog: %s", esp_err_to_name(wdt_err));
    }
    
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
    
    // Create FreeRTOS tasks with increased stack for stability
    BaseType_t taskCreated;
    
    taskCreated = xTaskCreatePinnedToCore(
        sensorTask,
        "SensorTask",
        12288,  // Increased from 8192
        NULL,
        2,
        &sensorTaskHandle,
        0
    );
    if (taskCreated != pdPASS) {
        ESP_LOGE(TAG, "Failed to create SensorTask");
        ESP.restart();
    }
    
    taskCreated = xTaskCreatePinnedToCore(
        webServerTask,
        "WebTask",
        16384,
        NULL,
        1,
        &webTaskHandle,
        1
    );
    if (taskCreated != pdPASS) {
        ESP_LOGE(TAG, "Failed to create WebTask");
        ESP.restart();
    }
    
    taskCreated = xTaskCreatePinnedToCore(
        automatismTask,
        "AutoTask",
        10240,  // Increased from 8192
        NULL,
        1,
        &automatismTaskHandle,
        0
    );
    if (taskCreated != pdPASS) {
        ESP_LOGE(TAG, "Failed to create AutoTask");
        ESP.restart();
    }
    
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

// Configuration with fixed buffers instead of String
struct SystemConfig {
    char wifi_ssid[33];      // Max SSID length + null
    char wifi_pass[65];      // Max password length + null
    char server_url[128];
    char api_key[65];
    float temp_offset;
    float hum_offset;
    int pump_duration;
    int fan_threshold;
    bool ota_enabled;
    int log_level;
};

static SystemConfig config;
static SemaphoreHandle_t xConfigMutex = NULL;

void loadConfiguration() {
    // Load all configuration from NVS [[memory:5580064]]
    ESP_LOGI(TAG, "Loading configuration from NVS...");
    
    if (xConfigMutex == NULL) {
        xConfigMutex = xSemaphoreCreateMutex();
    }
    
    xSemaphoreTake(xConfigMutex, portMAX_DELAY);
    
    // WiFi configuration - using fixed buffers
    size_t len = sizeof(config.wifi_ssid);
    preferences.getBytes("wifi_ssid", config.wifi_ssid, len);
    len = sizeof(config.wifi_pass);
    preferences.getBytes("wifi_pass", config.wifi_pass, len);
    
    // Server configuration
    len = sizeof(config.server_url);
    preferences.getBytes("server_url", config.server_url, len);
    len = sizeof(config.api_key);
    preferences.getBytes("api_key", config.api_key, len);
    
    // Sensor configuration
    config.temp_offset = preferences.getFloat("temp_offset", 0.0);
    config.hum_offset = preferences.getFloat("hum_offset", 0.0);
    
    // Actuator configuration
    config.pump_duration = preferences.getInt("pump_duration", 60);
    config.fan_threshold = preferences.getInt("fan_threshold", 28);
    
    // System configuration
    config.ota_enabled = preferences.getBool("ota_enabled", true);
    config.log_level = preferences.getInt("log_level", 2);
    
    xSemaphoreGive(xConfigMutex);
    
    ESP_LOGI(TAG, "Configuration loaded successfully");
}

void sendDataToServer() {
    static unsigned long lastSend = 0;
    const unsigned long SEND_INTERVAL = 300000; // 5 minutes
    
    // Check for millis() overflow
    unsigned long currentTime = millis();
    if (currentTime < lastSend) {
        lastSend = currentTime; // Reset on overflow
    }
    
    if (currentTime - lastSend < SEND_INTERVAL) {
        return;
    }
    
    lastSend = currentTime;
    
    // Use StaticJsonDocument to avoid heap fragmentation
    StaticJsonDocument<1024> doc;
    
    // Prepare data according to PHP script requirements [[memory:5580054]]
    // Add all required fields in exact order
    doc["timestamp"] = currentTime;
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
    doc["uptime"] = currentTime / 1000;
    doc["wifi_rssi"] = WiFi.RSSI();
    doc["error_count"] = getErrorCount();
    doc["last_error"] = getLastError();
    
    // Send with retry mechanism
    int retries = 3;
    bool success = false;
    
    while (retries-- > 0 && !success) {
        success = sendJsonToServer(doc);
        if (!success && retries > 0) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    
    if (success) {
        ESP_LOGI(TAG, "Data sent to server successfully");
    } else {
        ESP_LOGE(TAG, "Failed to send data after 3 attempts");
        // Log to SPIFFS for later transmission
        logFailedTransmission(doc);
    }
}