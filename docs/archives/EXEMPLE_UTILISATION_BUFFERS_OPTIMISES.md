# 📘 GUIDE D'UTILISATION - BUFFERS OPTIMISÉS

**Version:** FFP5CS v11.117  
**Module:** Configuration mémoire optimisée

## 🔄 MIGRATION RAPIDE

### Avant (ancien code)
```cpp
// Taille fixe pour tous les JSON
StaticJsonDocument<4096> doc;
StaticJsonDocument<4096> response;
StaticJsonDocument<4096> config;

// Buffers HTTP fixes
char httpBuffer[4096];

// Email sans contrôle
String emailContent;
emailContent.reserve(6000);
```

### Après (code optimisé)
```cpp
// Tailles adaptées selon l'usage
StaticJsonDocument<JsonSize::SMALL> statusDoc;    // 512 bytes
StaticJsonDocument<JsonSize::MEDIUM> configDoc;   // 1536 bytes
StaticJsonDocument<JsonSize::LARGE> dataDoc;      // 3072 bytes (WROOM) ou 4096 (S3)

// Buffers adaptatifs
char httpBuffer[BufferConfig::HTTP_BUFFER_SIZE];  // 2KB (WROOM) ou 4KB (S3)

// Email avec limite board-aware
char emailBuffer[BufferConfig::EMAIL_MAX_SIZE_BYTES];  // 3KB (WROOM) ou 6KB (S3)
```

## 📊 EXEMPLES PRATIQUES

### 1. Lecture de configuration depuis serveur
```cpp
void fetchRemoteConfig() {
    // Monitoring mémoire avant
    MemoryMonitor::logStats("Before fetchConfig");
    
    // Buffer HTTP adapté au board
    char url[256];
    ServerConfig::getPostDataUrl(url, sizeof(url));
    
    // JSON de taille moyenne pour config
    StaticJsonDocument<JsonSize::MEDIUM> configDoc;
    
    HTTPClient http;
    http.begin(url);
    
    // Buffer de réception optimisé
    String payload;
    payload.reserve(JsonSize::MEDIUM);
    
    int httpCode = http.GET();
    if (httpCode == HTTP_OK_CODE) {
        payload = http.getString();
        
        DeserializationError error = deserializeJson(configDoc, payload);
        if (!error) {
            // Traiter la config
            applyConfig(configDoc);
        }
    }
    
    http.end();
    
    // Monitoring mémoire après
    MemoryMonitor::logStats("After fetchConfig");
}
```

### 2. Envoi de données capteurs
```cpp
void sendSensorData() {
    // JSON petit pour données capteurs simples
    StaticJsonDocument<JsonSize::SMALL> sensorDoc;
    
    // Remplir avec données capteurs
    sensorDoc["temp"] = 25.3;
    sensorDoc["humidity"] = 65.2;
    sensorDoc["water_level"] = 180;
    sensorDoc["timestamp"] = millis();
    
    // Buffer de sérialisation
    char buffer[JsonSize::SMALL];
    size_t len = serializeJson(sensorDoc, buffer, sizeof(buffer));
    
    // Vérifier si le JSON tient dans le buffer
    if (sensorDoc.overflowed()) {
        LOG_WARN("JSON overflow! Need larger buffer");
        // Fallback vers buffer plus grand
        StaticJsonDocument<JsonSize::MEDIUM> largerDoc = sensorDoc;
        char largerBuffer[JsonSize::MEDIUM];
        len = serializeJson(largerDoc, largerBuffer, sizeof(largerBuffer));
        // Envoyer largerBuffer...
    } else {
        // Envoyer buffer normal
        sendToServer(buffer, len);
    }
}
```

### 3. Gestion d'état système complet
```cpp
void getSystemState(JsonDocument& output) {
    // Vérifier la mémoire disponible
    MemoryMonitor::Stats memStats;
    memStats.update();
    
    if (memStats.isCriticalMemory()) {
        // Mode dégradé: JSON minimal
        StaticJsonDocument<JsonSize::SMALL> minimalDoc;
        minimalDoc["status"] = "low_memory";
        minimalDoc["free"] = memStats.freeHeap;
        output = minimalDoc;
        return;
    }
    
    // Mode normal: JSON complet
    StaticJsonDocument<JsonSize::LARGE> fullDoc;
    
    // Ajouter toutes les données
    JsonObject system = fullDoc.createNestedObject("system");
    system["version"] = ProjectConfig::VERSION;
    system["uptime"] = millis() / 1000;
    system["freeHeap"] = memStats.freeHeap;
    system["fragmentation"] = memStats.fragmentation;
    
    JsonObject sensors = fullDoc.createNestedObject("sensors");
    // ... ajouter données capteurs
    
    output = fullDoc;
}
```

### 4. Email avec contrôle de taille
```cpp
bool sendAlertEmail(const char* subject, const char* message) {
    // Vérifier la longueur avant allocation
    size_t totalSize = strlen(subject) + strlen(message) + 200; // Headers
    
    if (totalSize > BufferConfig::EMAIL_MAX_SIZE_BYTES) {
        LOG_ERROR("Email too large: %u > %u bytes", 
                  totalSize, BufferConfig::EMAIL_MAX_SIZE_BYTES);
        
        // Tronquer le message si nécessaire
        char truncatedMsg[BufferConfig::EMAIL_MAX_SIZE_BYTES - 300];
        snprintf(truncatedMsg, sizeof(truncatedMsg), 
                 "%.100s... [TRUNCATED]", message);
        
        return sendEmail(subject, truncatedMsg);
    }
    
    return sendEmail(subject, message);
}
```

### 5. Pool JSON avec monitoring
```cpp
class ManagedJsonPool {
private:
    JsonPool pool;
    uint32_t acquisitions = 0;
    uint32_t releases = 0;
    uint32_t failures = 0;
    
public:
    ArduinoJson::DynamicJsonDocument* acquire(size_t size) {
        // Sélectionner la taille standard appropriée
        size_t optimalSize = JsonSize::getOptimalSize(size);
        
        auto* doc = pool.acquire(optimalSize);
        if (doc) {
            acquisitions++;
        } else {
            failures++;
            LOG_WARN("JsonPool: Failed to acquire %u bytes", optimalSize);
            
            // Monitoring si trop d'échecs
            if (failures % 10 == 0) {
                MemoryMonitor::logStats("JsonPool failures");
            }
        }
        return doc;
    }
    
    void release(ArduinoJson::DynamicJsonDocument* doc) {
        if (doc) {
            pool.release(doc);
            releases++;
        }
    }
    
    void logStats() {
        LOG_INFO("JsonPool: Acquired=%u, Released=%u, Failed=%u, Active=%u",
                 acquisitions, releases, failures, acquisitions - releases);
    }
};
```

## 🔍 MONITORING ET DEBUG

### Macro pour debug mémoire
```cpp
#define MEM_CHECK(operation) do { \
    MemoryMonitor::logStats("Before " #operation); \
    operation; \
    MemoryMonitor::logStats("After " #operation); \
} while(0)

// Utilisation
MEM_CHECK(loadConfiguration());
MEM_CHECK(processSensorData());
```

### Vérification périodique
```cpp
void memoryHealthCheck() {
    static uint32_t lastCheck = 0;
    
    if (millis() - lastCheck > 60000) {  // Toutes les minutes
        MemoryMonitor::Stats stats;
        stats.update();
        
        if (stats.fragmentation > 50) {
            LOG_WARN("High fragmentation: %u%%. Consider reboot.", 
                     stats.fragmentation);
        }
        
        if (stats.freeHeap < stats.minFreeHeap * 1.1) {
            LOG_WARN("Memory leak suspected. Min heap declining.");
        }
        
        lastCheck = millis();
    }
}
```

## ⚡ OPTIMISATIONS AVANCÉES

### 1. Allocation paresseuse
```cpp
template<size_t SIZE>
class LazyJsonDocument {
private:
    StaticJsonDocument<SIZE>* doc = nullptr;
    
public:
    JsonDocument& get() {
        if (!doc) {
            doc = new StaticJsonDocument<SIZE>();
        }
        return *doc;
    }
    
    void release() {
        if (doc) {
            delete doc;
            doc = nullptr;
        }
    }
    
    ~LazyJsonDocument() {
        release();
    }
};
```

### 2. Buffer tournant pour logs
```cpp
class CircularLogBuffer {
private:
    static constexpr size_t BUFFER_SIZE = 2048;
    char buffer[BUFFER_SIZE];
    size_t writePos = 0;
    
public:
    void append(const char* msg) {
        size_t len = strlen(msg);
        if (len >= BUFFER_SIZE) return;
        
        if (writePos + len > BUFFER_SIZE) {
            writePos = 0;  // Wrap around
        }
        
        memcpy(&buffer[writePos], msg, len);
        writePos += len;
    }
};
```

## ✅ CHECKLIST DE MIGRATION

- [ ] Remplacer `memory_config.h` par `memory_config_optimized.h`
- [ ] Identifier tous les `StaticJsonDocument<4096>`
- [ ] Remplacer par `JsonSize::SMALL/MEDIUM/LARGE` approprié
- [ ] Ajouter `MemoryMonitor::logStats()` aux endroits critiques
- [ ] Tester sur ESP32-WROOM avec monitoring actif
- [ ] Vérifier heap minimum > 30KB en fonctionnement
- [ ] Valider sur ESP32-S3 (si disponible)

---

*Guide créé le 14/11/2025 - FFP5CS v11.117*
