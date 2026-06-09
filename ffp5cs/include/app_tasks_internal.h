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
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

struct AppContext;

// Contexte applicatif partagé (défini dans app_tasks.cpp, fixé par AppTasks::start()).
extern AppContext* g_ctx;

// File capteurs : producteur sensorTask, consommateur automationTask.
extern QueueHandle_t g_sensorQueue;

// --- Corps de tâches déplacés hors d'app_tasks.cpp ---
void webTask(void* pv);    // app_tasks_web.cpp
void sensorTask(void* pv); // app_tasks_sensor.cpp
