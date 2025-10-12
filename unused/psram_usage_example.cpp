#include "psram_allocator.h"
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

// Example: Using PSRAM for large JSON documents
class PSRAMJsonDocument {
private:
    DynamicJsonDocument* doc;
    void* buffer;
    size_t bufferSize;
    
public:
    PSRAMJsonDocument(size_t size) : doc(nullptr), buffer(nullptr), bufferSize(size) {
        // Allocate buffer in PSRAM
        buffer = psram_malloc(bufferSize, "JsonDoc");
        
        if (buffer) {
            // Create JsonDocument using PSRAM buffer
            doc = new(buffer) DynamicJsonDocument(bufferSize);
            ESP_LOGI("PSRAMJson", "Created JSON document of %d bytes in PSRAM", bufferSize);
        } else {
            ESP_LOGE("PSRAMJson", "Failed to allocate PSRAM for JSON document");
            // Fallback to regular heap
            doc = new DynamicJsonDocument(bufferSize);
        }
    }
    
    ~PSRAMJsonDocument() {
        if (doc && buffer) {
            doc->~DynamicJsonDocument();
            psram_free(buffer);
        } else if (doc) {
            delete doc;
        }
    }
    
    DynamicJsonDocument& operator*() { return *doc; }
    DynamicJsonDocument* operator->() { return doc; }
    
    bool isInPSRAM() const { return buffer != nullptr; }
};

// Example: Web response buffer in PSRAM
class PSRAMWebBuffer {
private:
    char* buffer;
    size_t size;
    size_t used;
    
public:
    PSRAMWebBuffer(size_t bufferSize = 16384) : size(bufferSize), used(0) {
        buffer = (char*)psram_malloc(size, "WebBuffer");
        if (!buffer) {
            ESP_LOGE("PSRAMWeb", "Failed to allocate web buffer, using heap");
            buffer = (char*)malloc(size);
        }
    }
    
    ~PSRAMWebBuffer() {
        if (buffer) {
            if (heap_caps_get_allocated_size(buffer) & MALLOC_CAP_SPIRAM) {
                psram_free(buffer);
            } else {
                free(buffer);
            }
        }
    }
    
    bool append(const char* data, size_t len) {
        if (used + len > size) {
            // Need to grow buffer
            size_t newSize = size * 2;
            char* newBuffer = (char*)psram_realloc(buffer, newSize, "WebBuffer");
            if (!newBuffer) {
                return false;
            }
            buffer = newBuffer;
            size = newSize;
        }
        
        memcpy(buffer + used, data, len);
        used += len;
        return true;
    }
    
    bool appendString(const String& str) {
        return append(str.c_str(), str.length());
    }
    
    void clear() {
        used = 0;
    }
    
    const char* data() const { return buffer; }
    size_t length() const { return used; }
    size_t capacity() const { return size; }
};

// Example: Sensor data circular buffer in PSRAM
template<typename T>
class PSRAMCircularBuffer {
private:
    T* buffer;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
    SemaphoreHandle_t mutex;
    
public:
    PSRAMCircularBuffer(size_t size) : capacity(size), head(0), tail(0), count(0) {
        size_t bufferSize = sizeof(T) * capacity;
        buffer = (T*)psram_malloc(bufferSize, "CircularBuffer");
        
        if (!buffer) {
            ESP_LOGE("PSRAMBuffer", "Failed to allocate circular buffer in PSRAM");
            // Fallback to smaller size in regular heap
            capacity = size / 4;
            buffer = (T*)malloc(sizeof(T) * capacity);
        }
        
        mutex = xSemaphoreCreateMutex();
        
        ESP_LOGI("PSRAMBuffer", "Created circular buffer for %d items in %s", 
                capacity, (heap_caps_get_allocated_size(buffer) & MALLOC_CAP_SPIRAM) ? "PSRAM" : "RAM");
    }
    
    ~PSRAMCircularBuffer() {
        if (buffer) {
            if (heap_caps_get_allocated_size(buffer) & MALLOC_CAP_SPIRAM) {
                psram_free(buffer);
            } else {
                free(buffer);
            }
        }
        if (mutex) {
            vSemaphoreDelete(mutex);
        }
    }
    
    bool push(const T& item) {
        xSemaphoreTake(mutex, portMAX_DELAY);
        
        buffer[head] = item;
        head = (head + 1) % capacity;
        
        if (count < capacity) {
            count++;
        } else {
            tail = (tail + 1) % capacity; // Overwrite oldest
        }
        
        xSemaphoreGive(mutex);
        return true;
    }
    
    bool pop(T& item) {
        xSemaphoreTake(mutex, portMAX_DELAY);
        
        if (count == 0) {
            xSemaphoreGive(mutex);
            return false;
        }
        
        item = buffer[tail];
        tail = (tail + 1) % capacity;
        count--;
        
        xSemaphoreGive(mutex);
        return true;
    }
    
    size_t size() const { return count; }
    size_t getCapacity() const { return capacity; }
    bool isEmpty() const { return count == 0; }
    bool isFull() const { return count == capacity; }
};

// Example usage in web server for large responses
void handleLargeDataRequest(AsyncWebServerRequest *request) {
    // Use PSRAM for large JSON response
    PSRAMJsonDocument jsonDoc(8192);
    
    if (!jsonDoc.isInPSRAM()) {
        request->send(500, "application/json", "{\"error\":\"Insufficient memory\"}");
        return;
    }
    
    // Build large response
    JsonObject root = jsonDoc->to<JsonObject>();
    root["timestamp"] = millis();
    root["device"] = "ESP32-S3";
    
    // Add sensor data array
    JsonArray sensors = root.createNestedArray("sensors");
    for (int i = 0; i < 100; i++) {
        JsonObject sensor = sensors.createNestedObject();
        sensor["id"] = i;
        sensor["value"] = random(0, 100);
        sensor["unit"] = "°C";
    }
    
    // Serialize to PSRAM buffer
    PSRAMWebBuffer responseBuffer(16384);
    serializeJson(*jsonDoc, responseBuffer.data(), responseBuffer.capacity());
    
    // Send response
    request->send(200, "application/json", responseBuffer.data());
}

// Initialize PSRAM usage
void initPSRAMUsage() {
    if (!psramFound()) {
        ESP_LOGE("PSRAM", "PSRAM not found!");
        return;
    }
    
    ESP_LOGI("PSRAM", "PSRAM found. Size: %d KB", ESP.getPsramSize() / 1024);
    ESP_LOGI("PSRAM", "Free PSRAM: %d KB", ESP.getFreePsram() / 1024);
    
    // Initialize PSRAM allocator
    PSRAMAllocator::getInstance();
    
    // Print initial memory map
    PSRAMAllocator::getInstance()->printMemoryMap();
}