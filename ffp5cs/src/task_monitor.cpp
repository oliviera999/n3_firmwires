#include "task_monitor.h"
#include "app_tasks.h"
#include "config.h"

namespace TaskMonitor {

// Helper pour récupérer les stats d'une tâche
static TaskStats getTaskStats(const char* name, TaskHandle_t handle) {
  TaskStats stats;
  stats.name = name;
  if (handle) {
    stats.state = eTaskGetState(handle);
    stats.highWaterMark = uxTaskGetStackHighWaterMark(handle);
  } else {
    stats.state = eDeleted;
    stats.highWaterMark = 0;
  }
  return stats;
}

Snapshot collectSnapshot() {
  Snapshot snap;
  AppTasks::Handles handles = AppTasks::getHandles();

  snap.sensor = getTaskStats("Sensor", handles.sensor);
  snap.web = getTaskStats("Web", handles.web);
  snap.automation = getTaskStats("Auto", handles.automation);
  snap.display = getTaskStats("Display", handles.display);  // déprécié v13.65+
  snap.net = getTaskStats("Net", handles.net);
  // v13.70 (audit): ajout postSender + ota.
  snap.postSender = getTaskStats("Post", handles.postSender);
  snap.ota = getTaskStats("Ota", handles.ota);

  return snap;
}

void logSnapshot(const Snapshot& s, const char* stage) {
  // v13.70 : log compact étendu (S=sensor W=web A=auto N=net P=postSender O=ota).
  // D=display retiré (task supprimée). Format compatible analyse_log.ps1.
  LOG_INFO("Tasks [%s] HWM: S=%u W=%u A=%u N=%u P=%u O=%u",
           stage,
           s.sensor.highWaterMark,
           s.web.highWaterMark,
           s.automation.highWaterMark,
           s.net.highWaterMark,
           s.postSender.highWaterMark,
           s.ota.highWaterMark);
}

void logDiff(const Snapshot& before, const Snapshot& after, const char* stage) {
  int dS = (int)after.sensor.highWaterMark - (int)before.sensor.highWaterMark;
  int dW = (int)after.web.highWaterMark - (int)before.web.highWaterMark;
  int dA = (int)after.automation.highWaterMark - (int)before.automation.highWaterMark;
  int dN = (int)after.net.highWaterMark - (int)before.net.highWaterMark;
  int dP = (int)after.postSender.highWaterMark - (int)before.postSender.highWaterMark;
  int dO = (int)after.ota.highWaterMark - (int)before.ota.highWaterMark;

  LOG_INFO("Tasks Diff [%s]: S=%d W=%d A=%d N=%d P=%d O=%d", stage, dS, dW, dA, dN, dP, dO);
}

bool detectAnomalies(const Snapshot& s, const char* stage) {
  bool anomaly = false;
  const uint32_t LOW_STACK_THRESHOLD = 100; // Mots

  auto check = [&](const TaskStats& t) {
    if (t.state != eDeleted && t.highWaterMark < LOW_STACK_THRESHOLD) {
      LOG_WARN("Task Alert [%s]: %s low stack (%u)", stage, t.name, t.highWaterMark);
      anomaly = true;
    }
  };

  check(s.sensor);
  check(s.web);
  check(s.automation);
  check(s.net);
  check(s.postSender);  // v13.70 (audit)
  check(s.ota);         // v13.70 (audit)

  return anomaly;
}

} // namespace TaskMonitor
