#pragma once

// =============================================================================
// CONFIGURATION SOMMEIL ET ÉCONOMIE D'ÉNERGIE
// =============================================================================
// Module: Configuration complète du sommeil, économie d'énergie et modem sleep
// Taille: ~192 lignes (SleepConfig + ModemSleepConfig)
// =============================================================================

#include <Arduino.h>

// =============================================================================
// CONFIGURATION SOMMEIL ET ÉCONOMIE D'ÉNERGIE
// =============================================================================
namespace SleepConfig {
    // ========================================
    // DÉLAIS D'INACTIVITÉ AVANT SOMMEIL
    // ========================================
    
    // SUPPRIMÉ: INACTIVITY_DELAY_MS obsolète - remplacé par le système adaptatif
    
    // Délai minimum absolu depuis le dernier réveil
    // Protection : le système reste éveillé au moins ce temps après chaque réveil
    constexpr uint32_t MIN_AWAKE_TIME_MS = 480000;          // 8 minutes minimum éveillé (augmenté de 2.5 à 8 min)
    
    // Délai d'inactivité web avant autorisation de sommeil
    // Si activité web récente, attendre ce délai avant de permettre le sommeil
    constexpr uint32_t WEB_INACTIVITY_TIMEOUT_MS = 600000;   // 10 minutes d'inactivité web
    
    // ========================================
    // DÉLAIS D'INACTIVITÉ ADAPTATIFS
    // ========================================
    // IMPORTANT: Ces constantes contrôlent les DÉLAIS D'INACTIVITÉ avant autorisation de sommeil,
    // PAS la durée de sommeil elle-même (qui est contrôlée par le serveur distant via freqWakeSec)
    
    // Délai d'inactivité minimum (en secondes)
    // Utilisé quand des erreurs récentes sont détectées (surveillance accrue)
    constexpr uint32_t MIN_INACTIVITY_DELAY_SEC = 300;             // 5 minutes minimum d'inactivité (augmenté de 3 à 5 min)
    
    // Délai d'inactivité maximum (en secondes)
    // Limite supérieure pour éviter des délais trop longs avant sommeil
    constexpr uint32_t MAX_INACTIVITY_DELAY_SEC = 3600;            // 1 heure maximum d'inactivité
    
    // Délai d'inactivité normal (en secondes)
    // Utilisé en fonctionnement standard - augmenté à 8 min pour permettre plus de temps aux marées
    constexpr uint32_t NORMAL_INACTIVITY_DELAY_SEC = 480;          // 8 minutes normal d'inactivité (augmenté de 5 à 8 min)
    
    // Délai d'inactivité en cas d'erreurs (en secondes)
    // Réduit pour surveillance accrue quand des problèmes sont détectés
    constexpr uint32_t ERROR_INACTIVITY_DELAY_SEC = 180;            // 3 minutes si erreurs
    
    // Délai d'inactivité nocturne (en secondes)
    // Réduit la nuit pour dormir plus rapidement (22h-6h)
    // Calcul: NORMAL_INACTIVITY_DELAY_SEC ÷ 2 = 480 ÷ 2 = 240s (4 min)
    constexpr uint32_t NIGHT_INACTIVITY_DELAY_SEC = 240;          // 4 minutes la nuit (augmenté de 2.5 à 4 min)
    
    // ========================================
    // CONTRÔLE MULTIPLICATIF DU SOMMEIL
    // ========================================
    // IMPORTANT: Logique multiplicative respectant les ajustements du serveur
    // La durée nocturne est toujours freqWakeSec × NIGHT_SLEEP_MULTIPLIER
    
    // Multiplicateur nocturne pour la durée de sommeil
    // La nuit, le système dort freqWakeSec × 3 pour économie d'énergie maximale
    constexpr uint8_t NIGHT_SLEEP_MULTIPLIER = 3;                // ×3 la nuit
    
    // Activation du contrôle multiplicatif de la durée de sommeil
    // Si true: nuit = freqWakeSec × NIGHT_SLEEP_MULTIPLIER, jour = freqWakeSec
    // Si false: utilise toujours freqWakeSec du serveur (jour et nuit)
    constexpr bool LOCAL_SLEEP_DURATION_CONTROL = true;           // Contrôle multiplicatif activé
    
    // ========================================
    // PROTECTION ANTI-SPAM DÉBORDEMENT
    // ========================================
    
    // Paramètres de protection contre le spam d'emails de débordement
    constexpr uint32_t FLOOD_DEBOUNCE_MIN = 3;              // Délai avant validation du trop-plein (minutes)
    constexpr uint32_t FLOOD_COOLDOWN_MIN = 180;            // Délai entre emails d'alerte (minutes) 
    constexpr uint16_t FLOOD_HYST_CM = 5;                   // Hystérésis de sortie (cm au-dessus de limFlood)
    constexpr uint32_t FLOOD_RESET_STABLE_MIN = 15;         // Stabilité avant réarmement (minutes)
    
    // ========================================
    // OPTIMISATION WIFI ET CONNEXION
    // ========================================
    
    // Seuils RSSI pour qualité de connexion optimale
    constexpr int8_t WIFI_RSSI_EXCELLENT = -30;             // Excellent signal (> -30 dBm)
    constexpr int8_t WIFI_RSSI_GOOD = -50;                  // Bon signal (-30 à -50 dBm)
    constexpr int8_t WIFI_RSSI_FAIR = -70;                  // Signal acceptable (-50 à -70 dBm)
    constexpr int8_t WIFI_RSSI_POOR = -80;                  // Signal faible (-70 à -80 dBm)
    constexpr int8_t WIFI_RSSI_MINIMUM = -85;               // Signal minimum acceptable (-80 à -85 dBm)
    constexpr int8_t WIFI_RSSI_CRITICAL = -90;              // Signal critique, reconnexion recommandée
    
    // Paramètres de reconnexion intelligente
    constexpr uint32_t WIFI_RECONNECT_DELAY_MS = 2000;      // Délai entre tentatives de reconnexion (ms)
    constexpr uint32_t WIFI_MAX_RECONNECT_ATTEMPTS = 5;     // Nombre max de tentatives de reconnexion
    constexpr uint32_t WIFI_STABILITY_CHECK_INTERVAL_MS = 30000; // Vérification stabilité toutes les 30s
    constexpr uint32_t WIFI_WEAK_SIGNAL_THRESHOLD_MS = 60000;    // Seuil faible signal avant reconnexion (60s)
    
    // ========================================
    // GESTION TEMPORELLE ROBUSTE
    // ========================================
    
    // Validation des epochs temporels
    constexpr time_t EPOCH_MIN_VALID = 1600000000;          // Epoch minimum valide (2020-09-13)
    constexpr time_t EPOCH_MAX_VALID = 2000000000;          // Epoch maximum valide (2033-05-18)
    constexpr time_t EPOCH_DEFAULT_FALLBACK = 1704067200;   // Epoch par défaut (2024-01-01 00:00:00)
    constexpr time_t EPOCH_COMPILE_TIME = 1735689600;       // Epoch de compilation (2025-01-01 00:00:00)
    
    // Paramètres de correction de dérive
    constexpr float DRIFT_CORRECTION_THRESHOLD_PPM = 50.0f; // Seuil pour appliquer correction (50 PPM)
    constexpr uint32_t DRIFT_CORRECTION_INTERVAL_MS = 300000; // Intervalle correction (5 minutes)
    constexpr float DRIFT_CORRECTION_FACTOR = 0.8f;         // Facteur de correction (80% de la dérive)
    
    // Correction de dérive par défaut (quand pas de sync NTP)
    // Valeur négative pour compenser la dérive positive naturelle de l'ESP32
    // Ajuster selon les observations : -25 PPM = correction de 25 PPM vers l'arrière
    constexpr float DEFAULT_DRIFT_CORRECTION_PPM = -25.0f;  // Correction par défaut (-25 PPM)
    constexpr bool ENABLE_DEFAULT_DRIFT_CORRECTION = false;  // Désactivé pour observation

    // Interrupteur global pour désactiver toute correction de dérive
    constexpr bool ENABLE_DRIFT_CORRECTION = false;          // OFF: pas de correction appliquée
    
    // Paramètres de sauvegarde intelligente
    constexpr time_t MIN_TIME_DIFF_FOR_SAVE_SEC = 60;       // Différence minimum pour sauvegarde (1 minute)
    constexpr uint32_t MAX_SAVE_INTERVAL_MS = 1800000;      // Sauvegarde forcée toutes les 30 minutes
    constexpr uint32_t MIN_SAVE_INTERVAL_MS = 300000;       // Sauvegarde minimum toutes les 5 minutes
    
    // ========================================
    // CONFIGURATION ADAPTATIVE
    // ========================================
    
    // Activation du sommeil adaptatif
    // Si true : délais ajustés selon conditions (erreurs, nuit, échecs)
    // Si false : délai fixe à NORMAL_SLEEP_TIME_SEC
    constexpr bool ADAPTIVE_SLEEP_ENABLED = true;            // Sleep adaptatif activé
    
    // Heures de nuit pour sommeil prolongé (format 24h)
    constexpr uint8_t NIGHT_START_HOUR = 22;                 // Début nuit : 22h
    constexpr uint8_t NIGHT_END_HOUR = 6;                   // Fin nuit : 6h
    
    // ========================================
    // DÉTECTION D'ACTIVITÉ ET MARÉE
    // ========================================
    
    // Seuil de déclenchement marée montante (en cm)
    // Si différence de niveau > ce seuil, sommeil anticipé autorisé
    constexpr int16_t TIDE_TRIGGER_THRESHOLD_CM = 1;         // 1 cm de différence
    
    // ========================================
    // VALIDATION SYSTÈME AVANT SOMMEIL
    // ========================================
    
    // Vérifier que les capteurs répondent avant sommeil
    constexpr bool VALIDATE_SENSORS_BEFORE_SLEEP = true;     // Validation capteurs
    
    // Vérifier la connexion WiFi avant sommeil
    constexpr bool VALIDATE_WIFI_BEFORE_SLEEP = true;        // Validation WiFi
    
    // Vérifier les niveaux d'eau avant sommeil
    constexpr bool VALIDATE_WATER_LEVELS_BEFORE_SLEEP = true; // Validation niveaux
    
    // ========================================
    // GESTION DES ÉCHECS DE RÉVEIL
    // ========================================
    
    // Seuil d'alerte pour échecs consécutifs
    constexpr uint8_t WAKEUP_FAILURE_ALERT_THRESHOLD = 3;    // Alerte après 3 échecs
    
    // Facteur de multiplication du délai en cas d'échecs
    constexpr uint8_t WAKEUP_FAILURE_DELAY_MULTIPLIER = 2;   // Double le délai
    
    // ========================================
    // OPTIMISATIONS ÉNERGÉTIQUES
    // ========================================
    
    // Sauvegarder l'heure avant sommeil
    constexpr bool SAVE_TIME_BEFORE_SLEEP = true;            // Sauvegarde heure
    
    // Sauvegarder les identifiants WiFi avant sommeil
    constexpr bool SAVE_WIFI_CREDENTIALS_BEFORE_SLEEP = true; // Sauvegarde WiFi
    
    // Déconnecter WiFi avant sommeil pour économie
    constexpr bool DISCONNECT_WIFI_BEFORE_SLEEP = true;      // Déconnexion WiFi
    
    // Reconnexion automatique WiFi au réveil
    constexpr bool AUTO_RECONNECT_WIFI_AFTER_SLEEP = true;  // Reconnexion auto
}

// =============================================================================
// CONFIGURATION MODEM SLEEP + LIGHT SLEEP
// =============================================================================
namespace ModemSleepConfig {
    // Activation du modem sleep avec light sleep automatique
    constexpr bool ENABLE_MODEM_SLEEP = true;              // Activer le modem sleep
    
    // Test automatique de compatibilité DTIM
    constexpr bool AUTO_TEST_DTIM = true;                  // Test automatique DTIM
    
    // Intervalle de test DTIM (en millisecondes)
    constexpr unsigned long DTIM_TEST_INTERVAL_MS = 300000; // 5 minutes
    
    // Configuration DTIM optimale
    constexpr int DTIM_POWER_SAVE_MODE = 1; // WIFI_PS_MIN_MODEM equivalent
    
    // Logs détaillés pour debug
    constexpr bool ENABLE_DETAILED_LOGS = true;            // Logs détaillés
    
    // Fallback automatique vers light sleep classique si modem sleep échoue
    constexpr bool ENABLE_AUTO_FALLBACK = true;            // Fallback automatique
}
