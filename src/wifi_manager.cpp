#include "wifi_manager.h"
#include "project_config.h"
#include <esp_wifi.h>
#include <mutex>

WiFiManager* WiFiManager::instance = nullptr;
WiFiManager* wifiManager = nullptr;
static std::mutex instanceMutex;

WiFiManager::WiFiManager() 
    : apMode(false), connected(false), lastReconnectAttempt(0), reconnectCount(0) {
    dnsServer = new DNSServer();
    prefs = new Preferences();
    
    // Initialize mutex for thread safety
    stateMutex = xSemaphoreCreateMutex();
    if (stateMutex == NULL) {
        ESP_LOGE("WiFiManager", "Failed to create mutex");
    }
}

WiFiManager* WiFiManager::getInstance() {
    // Thread-safe singleton implementation
    std::lock_guard<std::mutex> lock(instanceMutex);
    if (instance == nullptr) {
        instance = new WiFiManager();
    }
    return instance;
}

bool WiFiManager::init() {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);  // Disable WiFi sleep for better response
    
    // Set WiFi TX power to max
    esp_wifi_set_max_tx_power(78);  // 19.5dBm
    
    prefs->begin("wifi", false);
    
    // Load saved credentials
    if (loadCredentials()) {
        Serial.println("WiFi credentials loaded from NVS");
        return autoConnect();
    }
    
    return false;
}

bool WiFiManager::connect(const char* ssid, const char* password) {
    // Use fixed buffers instead of String
    static char ssidBuffer[33];
    static char passBuffer[65];
    
    if (ssid == nullptr) {
        // Use stored credentials
        xSemaphoreTake(stateMutex, portMAX_DELAY);
        strncpy(ssidBuffer, this->ssidFixed, sizeof(ssidBuffer));
        strncpy(passBuffer, this->passwordFixed, sizeof(passBuffer));
        xSemaphoreGive(stateMutex);
        
        if (strlen(ssidBuffer) == 0) {
            return false;
        }
        ssid = ssidBuffer;
        password = passBuffer;
    }
    
    ESP_LOGI("WiFiManager", "Connecting to WiFi: %s", ssid);
    
    WiFi.begin(ssid, password);
    
    // Non-blocking connection with timeout
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && 
           millis() - startTime < WIFI_CONNECT_TIMEOUT) {
        vTaskDelay(pdMS_TO_TICKS(100)); // Use FreeRTOS delay
        if ((millis() - startTime) % 1000 == 0) {
            ESP_LOGI("WiFiManager", "Connecting...");
        }
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        xSemaphoreTake(stateMutex, portMAX_DELAY);
        connected = true;
        xSemaphoreGive(stateMutex);
        
        ESP_LOGI("WiFiManager", "WiFi connected! IP: %s, RSSI: %d dBm", 
                WiFi.localIP().toString().c_str(), WiFi.RSSI());
        
        // Save credentials if new
        if (strcmp(ssid, ssidFixed) != 0) {
            saveCredentials(ssid, password);
        }
        
        return true;
    }
    
    ESP_LOGE("WiFiManager", "WiFi connection failed!");
    return false;
}

void WiFiManager::disconnect() {
    WiFi.disconnect();
    connected = false;
}

bool WiFiManager::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

void WiFiManager::startAP(const char* apSSID, const char* apPassword) {
    WiFi.mode(WIFI_AP_STA);
    
    if (apPassword && strlen(apPassword) >= 8) {
        WiFi.softAP(apSSID, apPassword);
    } else {
        WiFi.softAP(apSSID);
    }
    
    apMode = true;
    
    // Start DNS server for captive portal
    dnsServer->start(53, "*", WiFi.softAPIP());
    
    Serial.printf("AP started: %s, IP: %s\n", apSSID, WiFi.softAPIP().toString().c_str());
}

void WiFiManager::stopAP() {
    dnsServer->stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    apMode = false;
}

bool WiFiManager::isAPMode() {
    return apMode;
}

void WiFiManager::handleClient() {
    if (apMode) {
        dnsServer->processNextRequest();
    }
    
    // Auto-reconnect logic
    if (!connected && millis() - lastReconnectAttempt > WIFI_RETRY_DELAY) {
        lastReconnectAttempt = millis();
        reconnectCount++;
        
        if (reconnectCount <= WIFI_MAX_RETRIES) {
            Serial.printf("Reconnection attempt %d/%d\n", reconnectCount, WIFI_MAX_RETRIES);
            connect();
        } else if (!apMode) {
            // Start AP mode after max retries
            startAP(DEVICE_NAME, "12345678");
        }
    }
    
    // Reset reconnect count if connected
    if (connected && WiFi.status() == WL_CONNECTED) {
        reconnectCount = 0;
    } else if (connected && WiFi.status() != WL_CONNECTED) {
        connected = false;
    }
}

void WiFiManager::saveCredentials(const char* ssid, const char* password) {
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    
    // Use fixed buffers
    strncpy(ssidFixed, ssid, sizeof(ssidFixed) - 1);
    ssidFixed[sizeof(ssidFixed) - 1] = '\0';
    strncpy(passwordFixed, password, sizeof(passwordFixed) - 1);
    passwordFixed[sizeof(passwordFixed) - 1] = '\0';
    
    // Save to NVS [[memory:5580064]]
    prefs->putBytes("ssid", ssidFixed, strlen(ssidFixed) + 1);
    prefs->putBytes("password", passwordFixed, strlen(passwordFixed) + 1);
    
    xSemaphoreGive(stateMutex);
    
    ESP_LOGI("WiFiManager", "WiFi credentials saved to NVS");
}

bool WiFiManager::loadCredentials() {
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    
    size_t ssidLen = sizeof(ssidFixed);
    size_t passLen = sizeof(passwordFixed);
    
    prefs->getBytes("ssid", ssidFixed, ssidLen);
    prefs->getBytes("password", passwordFixed, passLen);
    
    bool hasCredentials = strlen(ssidFixed) > 0;
    
    xSemaphoreGive(stateMutex);
    
    return hasCredentials;
}

void WiFiManager::clearCredentials() {
    prefs->remove("ssid");
    prefs->remove("password");
    ssid = "";
    password = "";
}

bool WiFiManager::autoConnect() {
    if (ssid.length() > 0) {
        return connect(ssid.c_str(), password.c_str());
    }
    return false;
}

String WiFiManager::getIP() {
    if (apMode) {
        return WiFi.softAPIP().toString();
    }
    return WiFi.localIP().toString();
}

int WiFiManager::getRSSI() {
    return WiFi.RSSI();
}

String WiFiManager::getSSID() {
    return WiFi.SSID();
}

// Global helper functions
void initWiFiManager() {
    wifiManager = WiFiManager::getInstance();
    wifiManager->init();
}

bool connectWiFi(const char* ssid, const char* password) {
    return wifiManager->connect(ssid, password);
}

bool isWiFiConnected() {
    return wifiManager->isConnected();
}

String getWiFiIP() {
    return wifiManager->getIP();
}

int getWiFiRSSI() {
    return wifiManager->getRSSI();
}