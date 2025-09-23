#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <ESPAsyncWebServer.h>
#include <AsyncWebSocket.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <map>
#include <functional>

// Request handler types
enum RequestType {
    GET_REQUEST,
    POST_REQUEST,
    PUT_REQUEST,
    DELETE_REQUEST,
    WS_MESSAGE
};

// Web server class
class WebServerManager {
private:
    AsyncWebServer* server;
    AsyncWebSocket* ws;
    std::map<String, std::function<void(AsyncWebServerRequest*)>> routes;
    std::map<String, std::function<void(uint8_t*, size_t)>> wsHandlers;
    bool corsEnabled;
    String corsOrigin;
    bool authEnabled;
    String authUsername;
    String authPassword;
    unsigned long requestCount;
    unsigned long errorCount;
    
    static WebServerManager* instance;
    WebServerManager();
    
    // Internal handlers
    void handleNotFound(AsyncWebServerRequest* request);
    void handleOptions(AsyncWebServerRequest* request);
    bool checkAuth(AsyncWebServerRequest* request);
    void sendJSON(AsyncWebServerRequest* request, JsonDocument& doc, int code = 200);
    void sendError(AsyncWebServerRequest* request, const String& message, int code = 400);
    
public:
    static WebServerManager* getInstance();
    ~WebServerManager();
    
    bool init(AsyncWebServer* server, AsyncWebSocket* ws);
    void start();
    void stop();
    
    // Route management
    void addRoute(const String& path, RequestType type, std::function<void(AsyncWebServerRequest*)> handler);
    void removeRoute(const String& path);
    void serveStatic(const String& path, const String& fsPath);
    
    // WebSocket management
    void addWSHandler(const String& event, std::function<void(uint8_t*, size_t)> handler);
    void broadcastWS(const String& event, JsonDocument& data);
    void sendWS(uint32_t clientId, const String& event, JsonDocument& data);
    void cleanupWS();
    int getWSClientCount();
    
    // CORS configuration
    void enableCORS(bool enable, const String& origin = "*");
    
    // Authentication
    void enableAuth(bool enable, const String& username = "", const String& password = "");
    
    // API endpoints
    void setupAPIEndpoints();
    void handleGetStatus(AsyncWebServerRequest* request);
    void handleGetSensors(AsyncWebServerRequest* request);
    void handleGetActuators(AsyncWebServerRequest* request);
    void handlePostConfig(AsyncWebServerRequest* request);
    void handleGetLogs(AsyncWebServerRequest* request);
    void handlePostCommand(AsyncWebServerRequest* request);
    
    // File upload
    void handleFileUpload(AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final);
    
    // Statistics
    unsigned long getRequestCount() const { return requestCount; }
    unsigned long getErrorCount() const { return errorCount; }
    void resetStatistics();
};

// WebSocket event handler
class WSEventHandler {
private:
    AsyncWebSocket* ws;
    std::map<uint32_t, unsigned long> clientLastSeen;
    std::map<uint32_t, String> clientInfo;
    
public:
    WSEventHandler(AsyncWebSocket* ws);
    
    void onEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, 
                 AwsEventType type, void* arg, uint8_t* data, size_t len);
    void handleConnect(AsyncWebSocketClient* client);
    void handleDisconnect(AsyncWebSocketClient* client);
    void handleMessage(AsyncWebSocketClient* client, uint8_t* data, size_t len);
    void handleError(AsyncWebSocketClient* client, void* arg);
    void cleanupClients();
};

// Global functions
void initWebServer(AsyncWebServer* server, AsyncWebSocket* ws);
void processWebServerEvents();
void sendRealtimeData();
bool sendJsonToServer(JsonDocument& doc);

#endif // WEB_SERVER_H