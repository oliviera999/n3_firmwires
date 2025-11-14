#pragma once

// =============================================================================
// CONFIGURATION CENTRALE DU PROJET FFP5CS - MODULAIRE
// =============================================================================
// Ce fichier centralise TOUTES les configurations du projet via des modules
// Les modules sont organisés par domaine fonctionnel (<300 lignes chacun)
// Pour changer d'environnement, utiliser les flags dans platformio.ini
// =============================================================================

// Module 1: Version et identification (66 lignes)
#include "config/version_config.h"

// Module 2: Timeouts et valeurs par défaut (34 lignes)
#include "config/timeout_config.h"

// Module 3: Serveur, API et Email (55 lignes) 
#include "config/server_config.h"

// Module 4: Logs et debug (122 lignes)
#include "config/log_config.h"

// Module 5: Configuration des capteurs (75 lignes)
#include "config/sensor_config.h"

// Module 6: Timing et retry (108 lignes)
#include "config/timing_config.h"

// Module 7: Sommeil et économie d'énergie (192 lignes)
#include "config/sleep_config.h"

// Module 8: Tâches FreeRTOS (24 lignes)
#include "config/task_config.h"

// Module 9: Configuration réseau (24 lignes)
#include "config/network_config.h"

// Module 10: Mémoire, buffers et affichage (120 lignes) - VERSION OPTIMISÉE
#include "config/memory_config_optimized.h"

// Module 11: Système, actionneurs et monitoring (54 lignes)
#include "config/system_config.h"

// =============================================================================
// FIN DE LA CONFIGURATION MODULAIRE FFP5CS
// =============================================================================
// Architecture modulaire v11.117 - Chaque module < 300 lignes
// Maintenance facilitée - Organisation thématique claire
// =============================================================================