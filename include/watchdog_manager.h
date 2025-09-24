#ifndef WATCHDOG_MANAGER_H
#define WATCHDOG_MANAGER_H

#include <Arduino.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <map>
#include <vector>

// Watchdog configuration
#define WDT_TIMEOUT_SECONDS 10
#define WDT_PANIC_REBOOT true

class WatchdogManager {
private:
    struct TaskInfo {
        TaskHandle_t handle;
        const char* name;
        unsigned long lastReset;
        unsigned long resetCount;
        bool registered;
    };
    
    std::map<TaskHandle_t, TaskInfo> tasks;
    SemaphoreHandle_t mutex;
    bool initialized;
    unsigned long startTime;
    
    static WatchdogManager* instance;
    WatchdogManager();
    
public:
    static WatchdogManager* getInstance();
    ~WatchdogManager();
    
    // Initialize watchdog timer
    bool init(uint32_t timeout_seconds = WDT_TIMEOUT_SECONDS, bool panic = WDT_PANIC_REBOOT);
    
    // Register a task with the watchdog
    bool registerTask(TaskHandle_t handle, const char* name);
    bool registerCurrentTask(const char* name);
    
    // Unregister a task
    bool unregisterTask(TaskHandle_t handle);
    
    // Reset watchdog for a specific task
    bool resetTask(TaskHandle_t handle);
    bool resetCurrentTask();
    
    // Get task statistics
    size_t getRegisteredTaskCount();
    std::vector<String> getTaskNames();
    unsigned long getTaskResetCount(TaskHandle_t handle);
    
    // Check if all tasks are healthy
    bool areAllTasksHealthy();
    
    // Emergency functions
    void forceReset();
    void disable();
    
    // Debugging
    void printStatus();
};

// Global instance
extern WatchdogManager* watchdogManager;

// Helper macros for easy use
#define WDT_INIT() WatchdogManager::getInstance()->init()
#define WDT_REGISTER(name) WatchdogManager::getInstance()->registerCurrentTask(name)
#define WDT_RESET() WatchdogManager::getInstance()->resetCurrentTask()
#define WDT_STATUS() WatchdogManager::getInstance()->printStatus()

#endif // WATCHDOG_MANAGER_H