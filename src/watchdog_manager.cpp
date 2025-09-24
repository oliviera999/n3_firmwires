#include "watchdog_manager.h"

static const char* TAG = "WDT";

WatchdogManager* WatchdogManager::instance = nullptr;
WatchdogManager* watchdogManager = nullptr;

WatchdogManager::WatchdogManager() : initialized(false), startTime(0) {
    mutex = xSemaphoreCreateMutex();
    if (mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
    }
}

WatchdogManager::~WatchdogManager() {
    if (initialized) {
        esp_task_wdt_deinit();
    }
    if (mutex) {
        vSemaphoreDelete(mutex);
    }
}

WatchdogManager* WatchdogManager::getInstance() {
    if (instance == nullptr) {
        instance = new WatchdogManager();
        watchdogManager = instance; // Set global pointer
    }
    return instance;
}

bool WatchdogManager::init(uint32_t timeout_seconds, bool panic) {
    if (initialized) {
        ESP_LOGW(TAG, "Watchdog already initialized");
        return true;
    }
    
    // Configure watchdog timer
    esp_task_wdt_config_t config = {
        .timeout_ms = timeout_seconds * 1000,
        .idle_core_mask = 0,  // Don't watch idle tasks
        .trigger_panic = panic
    };
    
    esp_err_t err = esp_task_wdt_reconfigure(&config);
    if (err != ESP_OK) {
        // Try legacy init for older ESP-IDF versions
        err = esp_task_wdt_init(timeout_seconds, panic);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize watchdog: %s", esp_err_to_name(err));
            return false;
        }
    }
    
    initialized = true;
    startTime = millis();
    
    ESP_LOGI(TAG, "Watchdog initialized with %d second timeout", timeout_seconds);
    return true;
}

bool WatchdogManager::registerTask(TaskHandle_t handle, const char* name) {
    if (!initialized) {
        ESP_LOGE(TAG, "Watchdog not initialized");
        return false;
    }
    
    if (handle == NULL) {
        handle = xTaskGetCurrentTaskHandle();
    }
    
    xSemaphoreTake(mutex, portMAX_DELAY);
    
    // Check if already registered
    if (tasks.find(handle) != tasks.end() && tasks[handle].registered) {
        xSemaphoreGive(mutex);
        ESP_LOGW(TAG, "Task %s already registered", name);
        return true;
    }
    
    // Add task to watchdog
    esp_err_t err = esp_task_wdt_add(handle);
    if (err != ESP_OK && err != ESP_ERR_INVALID_ARG) { // Invalid arg means already added
        xSemaphoreGive(mutex);
        ESP_LOGE(TAG, "Failed to add task %s to watchdog: %s", name, esp_err_to_name(err));
        return false;
    }
    
    // Update task info
    TaskInfo info = {
        .handle = handle,
        .name = name,
        .lastReset = millis(),
        .resetCount = 0,
        .registered = true
    };
    tasks[handle] = info;
    
    xSemaphoreGive(mutex);
    
    ESP_LOGI(TAG, "Task %s registered with watchdog", name);
    return true;
}

bool WatchdogManager::registerCurrentTask(const char* name) {
    return registerTask(xTaskGetCurrentTaskHandle(), name);
}

bool WatchdogManager::unregisterTask(TaskHandle_t handle) {
    if (!initialized) {
        return false;
    }
    
    if (handle == NULL) {
        handle = xTaskGetCurrentTaskHandle();
    }
    
    xSemaphoreTake(mutex, portMAX_DELAY);
    
    auto it = tasks.find(handle);
    if (it == tasks.end() || !it->second.registered) {
        xSemaphoreGive(mutex);
        return false;
    }
    
    // Remove from watchdog
    esp_err_t err = esp_task_wdt_delete(handle);
    if (err != ESP_OK) {
        xSemaphoreGive(mutex);
        ESP_LOGE(TAG, "Failed to remove task from watchdog: %s", esp_err_to_name(err));
        return false;
    }
    
    // Update registration status
    it->second.registered = false;
    
    xSemaphoreGive(mutex);
    
    ESP_LOGI(TAG, "Task %s unregistered from watchdog", it->second.name);
    return true;
}

bool WatchdogManager::resetTask(TaskHandle_t handle) {
    if (!initialized) {
        return false;
    }
    
    if (handle == NULL) {
        handle = xTaskGetCurrentTaskHandle();
    }
    
    // Reset watchdog for this task
    esp_err_t err = esp_task_wdt_reset();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset watchdog: %s", esp_err_to_name(err));
        return false;
    }
    
    // Update statistics
    xSemaphoreTake(mutex, portMAX_DELAY);
    
    auto it = tasks.find(handle);
    if (it != tasks.end()) {
        it->second.lastReset = millis();
        it->second.resetCount++;
    }
    
    xSemaphoreGive(mutex);
    
    return true;
}

bool WatchdogManager::resetCurrentTask() {
    return resetTask(xTaskGetCurrentTaskHandle());
}

size_t WatchdogManager::getRegisteredTaskCount() {
    xSemaphoreTake(mutex, portMAX_DELAY);
    
    size_t count = 0;
    for (const auto& [handle, info] : tasks) {
        if (info.registered) {
            count++;
        }
    }
    
    xSemaphoreGive(mutex);
    return count;
}

std::vector<String> WatchdogManager::getTaskNames() {
    std::vector<String> names;
    
    xSemaphoreTake(mutex, portMAX_DELAY);
    
    for (const auto& [handle, info] : tasks) {
        if (info.registered) {
            names.push_back(String(info.name));
        }
    }
    
    xSemaphoreGive(mutex);
    return names;
}

unsigned long WatchdogManager::getTaskResetCount(TaskHandle_t handle) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    
    unsigned long count = 0;
    auto it = tasks.find(handle);
    if (it != tasks.end()) {
        count = it->second.resetCount;
    }
    
    xSemaphoreGive(mutex);
    return count;
}

bool WatchdogManager::areAllTasksHealthy() {
    if (!initialized) {
        return false;
    }
    
    xSemaphoreTake(mutex, portMAX_DELAY);
    
    unsigned long now = millis();
    bool allHealthy = true;
    
    for (const auto& [handle, info] : tasks) {
        if (info.registered) {
            // Check if task hasn't reset in too long
            unsigned long timeSinceReset = now - info.lastReset;
            if (timeSinceReset > (WDT_TIMEOUT_SECONDS * 1000 * 0.8)) { // 80% of timeout
                ESP_LOGW(TAG, "Task %s hasn't reset in %lu ms", info.name, timeSinceReset);
                allHealthy = false;
            }
        }
    }
    
    xSemaphoreGive(mutex);
    return allHealthy;
}

void WatchdogManager::forceReset() {
    ESP_LOGE(TAG, "Forcing system reset via watchdog");
    
    // Disable all task resets
    xSemaphoreTake(mutex, portMAX_DELAY);
    for (auto& [handle, info] : tasks) {
        info.registered = false;
    }
    xSemaphoreGive(mutex);
    
    // Wait for watchdog timeout
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void WatchdogManager::disable() {
    if (!initialized) {
        return;
    }
    
    ESP_LOGW(TAG, "Disabling watchdog");
    
    // Unregister all tasks
    xSemaphoreTake(mutex, portMAX_DELAY);
    
    for (auto& [handle, info] : tasks) {
        if (info.registered) {
            esp_task_wdt_delete(handle);
            info.registered = false;
        }
    }
    
    xSemaphoreGive(mutex);
    
    // Deinitialize watchdog
    esp_task_wdt_deinit();
    initialized = false;
}

void WatchdogManager::printStatus() {
    if (!initialized) {
        ESP_LOGI(TAG, "Watchdog not initialized");
        return;
    }
    
    xSemaphoreTake(mutex, portMAX_DELAY);
    
    unsigned long uptime = (millis() - startTime) / 1000;
    ESP_LOGI(TAG, "=== Watchdog Status ===");
    ESP_LOGI(TAG, "Uptime: %lu seconds", uptime);
    ESP_LOGI(TAG, "Registered tasks: %d", getRegisteredTaskCount());
    
    for (const auto& [handle, info] : tasks) {
        if (info.registered) {
            unsigned long timeSinceReset = millis() - info.lastReset;
            ESP_LOGI(TAG, "  %s: resets=%lu, last=%lu ms ago", 
                    info.name, info.resetCount, timeSinceReset);
        }
    }
    
    ESP_LOGI(TAG, "======================");
    
    xSemaphoreGive(mutex);
}