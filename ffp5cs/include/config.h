#pragma once

#include <Arduino.h>
#include <cmath>  // Pour isnan() dans SensorValidation
#include "board_traits.h"  // BoardTraits:: (capacités carte compile-time, remplace des #ifdef de valeurs)
#include "gpio_mapping.h"  // Pour GPIODefaults (source de vérité des valeurs par défaut)
#include "server_url_config.h"

// =============================================================================
// CONFIGURATION UNIFIÉE DU PROJET FFP5CS
// =============================================================================
// Le contenu (≈25 namespaces) est réparti physiquement en sous-headers cohésifs,
// ré-inclus ci-dessous DANS L'ORDRE D'ORIGINE. Cet ordre n'est pas arbitraire :
// il satisfait les dépendances inter-sections — Secrets → réseau (ApiConfig/
// WebAuth/Email), SystemConfig → SleepConfig (alias EPOCH_*), BufferConfig →
// NVSConfig.
//
// `#include "config.h"` reste l'INTERFACE UNIQUE : aucun .cpp n'a besoin de
// changer (les sous-headers peuvent aussi être inclus seuls, ils sont
// auto-suffisants). La macro `#define Serial` (config_logging.h) doit rester
// en DERNIER pour s'appliquer à tout le code en aval.
// =============================================================================
#include "config_secrets.h"   // Secrets, SecretsValidation (+ static_assert API_KEY prod)
#include "config_system.h"    // ProjectConfig, Utils, SystemConfig, TimingConfig, MonitoringConfig, FEATURE_* diag, DailyRebootConfig
#include "config_network.h"   // NetworkConfig, ApFallbackConfig, WebAuthConfig, ServerConfig, ApiConfig, EmailConfig
#include "config_buffers.h"   // BufferConfig, HeapConfig, AsyncTaskConfig, NVSConfig
#include "config_sensors.h"   // SensorConfig, SensorValidation, ActuatorConfig
#include "config_display.h"   // DisplayConfig
#include "config_tasks.h"     // SleepConfig (+ WIFI_APPLY_MODEM_SLEEP), TaskConfig
#include "config_logging.h"   // LogConfig + macros LOG_*/SENSOR_LOG_* + NullSerial + #define Serial (EN DERNIER)
