#pragma once
//
// app_tasks_internal.h — Interface interne partagée d'app_tasks.
//
// Première étape de la refonte du god-file app_tasks.cpp (audit v13.93) :
// permet de répartir les corps de tâches FreeRTOS sur plusieurs .cpp tout en
// gardant l'état partagé. Les DÉFINITIONS restent dans app_tasks.cpp (ordre
// d'initialisation et valeurs inchangés) ; seules les déclarations extern et
// les prototypes de tâches déplacées vivent ici.
//
#include "config.h"   // FEATURE_OTA / FEATURE_HTTP_OTA
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

struct AppContext;

// Contexte applicatif partagé (défini dans app_tasks.cpp, fixé par AppTasks::start()).
extern AppContext* g_ctx;

// File capteurs : producteur sensorTask, consommateur automationTask.
extern QueueHandle_t g_sensorQueue;

// Fire-and-forget POST : message + file (producteur netTask, consommateur postSenderTask).
enum class PostSenderType : uint8_t { PostData = 0, Heartbeat = 1 };
struct PostSenderMsg {
  PostSenderType type;
  char payload[BufferConfig::POST_PAYLOAD_MAX_SIZE];
  uint32_t sdSeqNum;
  bool hasSdQueueEntry;
};
extern QueueHandle_t g_postSenderQueue;

#if FEATURE_OTA && FEATURE_OTA != 0 && FEATURE_HTTP_OTA && FEATURE_HTTP_OTA != 0
// File + handle OTA : créés par AppTasks::start(), produits par netTask /
// triggerOtaCheck, consommés par otaTask (app_tasks_ota.cpp).
extern QueueHandle_t g_otaTriggerQueue;
extern TaskHandle_t g_otaTaskHandle;
#endif

// --- Corps de tâches déplacés hors d'app_tasks.cpp ---
void webTask(void* pv);          // app_tasks_web.cpp
void sensorTask(void* pv);       // app_tasks_sensor.cpp
void postSenderTask(void* pv);   // app_tasks_post.cpp
void automationTask(void* pv);   // app_tasks_automation.cpp
#if FEATURE_OTA && FEATURE_OTA != 0 && FEATURE_HTTP_OTA && FEATURE_HTTP_OTA != 0
void otaTask(void* pv);    // app_tasks_ota.cpp
#endif
