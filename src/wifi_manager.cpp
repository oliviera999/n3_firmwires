#include "wifi_manager.h"
#include "project_config.h"
#include <esp_wifi.h>

WiFiManager* WiFiManager::instance = nullptr;
WiFiManager* wifiManager = nullptr;

WiFiManager::WiFiManager() 
    : apMode(false), connected(false), lastReconnectAttempt(0), reconnectCount(0) {
    dnsServer = new DNSServer();
    prefs = new Preferences();
}

WiFiManager* WiFiManager::getInstance() {
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
    if (ssid == nullptr) {
        if (this->ssid.length() == 0) {
            return false;
        }
        ssid = this->ssid.c_str();
        password = this->password.c_str();
    }
    
    Serial.printf("Connecting to WiFi: %s\n", ssid);
    
    WiFi.begin(ssid, password);
    
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < WIFI_CONNECT_TIMEOUT) {
        delay(500);
        Serial.print(".");
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        connected = true;
        Serial.printf("\nWiFi connected! IP: %s, RSSI: %d dBm\n", 
                     WiFi.localIP().toString().c_str(), WiFi.RSSI());
        
        // Save credentials if new
        if (strcmp(ssid, this->ssid.c_str()) != 0) {
            saveCredentials(ssid, password);
        }
        
        return true;
    }
    
    Serial.println("\nWiFi connection failed!");
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
    prefs->putString("ssid", ssid);
    prefs->putString("password", password);
    this->ssid = ssid;
    this->password = password;
    Serial.println("WiFi credentials saved to NVS");
}

bool WiFiManager::loadCredentials() {
    ssid = prefs->getString("ssid", "");
    password = prefs->getString("password", "");
    return ssid.length() > 0;
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