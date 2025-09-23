#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>
#include <WiFiClient.h>
#include <DNSServer.h>
#include <Preferences.h>

class WiFiManager {
private:
    String ssid;
    String password;
    String hostname;
    bool apMode;
    bool connected;
    unsigned long lastReconnectAttempt;
    int reconnectCount;
    DNSServer* dnsServer;
    Preferences* prefs;
    
    static WiFiManager* instance;
    WiFiManager();
    
public:
    static WiFiManager* getInstance();
    
    bool init();
    bool connect(const char* ssid = nullptr, const char* password = nullptr);
    void disconnect();
    bool isConnected();
    void startAP(const char* apSSID, const char* apPassword = nullptr);
    void stopAP();
    bool isAPMode();
    void handleClient();
    void setHostname(const char* hostname);
    String getIP();
    int getRSSI();
    String getSSID();
    void saveCredentials(const char* ssid, const char* password);
    bool loadCredentials();
    void clearCredentials();
    bool autoConnect();
    void enableAutoReconnect(bool enable);
    void checkConnection();
    
    // Event callbacks
    void onConnect(std::function<void()> callback);
    void onDisconnect(std::function<void()> callback);
    void onAPStart(std::function<void()> callback);
    void onAPStop(std::function<void()> callback);
};

// Global instance
extern WiFiManager* wifiManager;

// Helper functions
void initWiFiManager();
bool connectWiFi(const char* ssid = nullptr, const char* password = nullptr);
bool isWiFiConnected();
String getWiFiIP();
int getWiFiRSSI();

#endif // WIFI_MANAGER_H