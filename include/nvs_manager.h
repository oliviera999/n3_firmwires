#pragma once
#include <Preferences.h>
#include <Arduino.h>
#include <vector>
#include <map>
#include <string>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class NVSLockGuard;

/**
 * Gestionnaire NVS Centralisé - Version Simplifiée
 * 
 * Consolidation des namespaces NVS de 14 à 4 pour réduire la fragmentation
 * Gestion d'erreurs centralisée avec validation simplifiée (longueur clé uniquement)
 * 
 * Version: 11.169 (simplifiée - 4 namespaces, TIME/SENSORS fusionnés)
 * 
 * =============================================================================
 * REGISTRE DES CLÉS NVS
 * =============================================================================
 * 
 * Les variables synchronisées avec le serveur distant (GPIO 16-18, 100-116) sont
 * documentées dans gpio_mapping.h (GPIOMap). Les clés ci-dessous sont des
 * ÉTATS INTERNES non synchronisés avec le serveur.
 * 
 * NAMESPACE "sys" (SYSTEM):
 * -------------------------
 *   force_wake_up   (bool)   - Forcer le réveil après deep sleep
 *   rtc_epoch       (ulong)  - Epoch RTC sauvegardé avant sleep
 *   ota_update_flag (bool)   - Flag activation OTA
 *   ota_prevVer     (string) - Version précédente avant OTA
 *   ota_in_progress (bool)   - OTA en cours (transitoire)
 *   net_send_en     (bool)   - Envoi réseau activé
 *   net_recv_en     (bool)   - Réception réseau activée
 *   mig*_done       (uint8)  - Flags de migration (internes)
 * 
 * NAMESPACE "cfg" (CONFIG):
 * -------------------------
 *   remote_json     (string) - JSON config reçu du serveur distant
 *   bouffe_matin    (bool)   - Flag "déjà nourri ce matin"
 *   bouffe_midi     (bool)   - Flag "déjà nourri ce midi"
 *   bouffe_soir     (bool)   - Flag "déjà nourri ce soir"
 *   bouffe_jour     (int)    - Jour du dernier nourrissage (1-31)
 *   bf_pmp_lock     (bool)   - Verrou pompe nourrissage actif
 *   temp_last_valid (float)  - Dernière température eau valide (fallback capteur)
 *   gpio_*          (bool)   - États actuateurs (gpio_pump_aqua, gpio_heater, etc.)
 * 
 * NAMESPACE "state" (STATE):
 * --------------------------
 *   snap_pending    (bool)   - Snapshot d'état en attente de restauration
 *   snap_aqua       (bool)   - État pompe aqua au moment du snapshot
 *   snap_heater     (bool)   - État chauffage au moment du snapshot
 *   snap_light      (bool)   - État lumière au moment du snapshot
 *   state_*         (bool)   - États actuateurs persistés (state_pumpAqua, etc.)
 *   state_lastLocal (ulong)  - Timestamp dernière action locale
 *   sync_count      (int)    - Nombre d'items dans la queue de sync
 *   sync_item_*     (string) - Items de la queue de sync (sync_item_0, etc.)
 *   sync_lastSync   (ulong)  - Timestamp dernière sync réussie
 *   sync_config     (bool)   - Config en attente de sync
 * 
 * NAMESPACE "logs" (LOGS):
 * ------------------------
 *   diag_rebootCnt  (int)    - Compteur de redémarrages
 *   diag_minHeap    (ulong)  - Heap minimum observé
 *   diag_httpOk     (ulong)  - Compteur requêtes HTTP réussies
 *   diag_httpKo     (ulong)  - Compteur requêtes HTTP échouées
 *   diag_otaOk      (ulong)  - Compteur OTA réussies
 *   diag_otaKo      (ulong)  - Compteur OTA échouées
 *   diag_lastUptime (ulong)  - Dernier uptime (debug uniquement)
 *   diag_lastHeap   (ulong)  - Dernier heap (debug uniquement)
 *   diag_hasPanic   (bool)   - Flag panic détecté
 *   diag_panicCause (string) - Cause du panic
 *   alert_floodLast (ulong)  - Timestamp dernière alerte inondation
 *   crash_has       (bool)   - Flag crash détecté
 *   crash_reason    (int)    - Code raison du crash
 * 
 * NAMESPACE "wifi_saved" (géré séparément, pas par NVSManager):
 * -------------------------------------------------------------
 *   count           (blob)   - Nombre de réseaux WiFi sauvegardés
 *   net_*           (blob)   - Réseaux sauvegardés (format "SSID|password")
 * 
 * =============================================================================
 */

// Namespaces consolidés (réduction de 14 à 4)
namespace NVS_NAMESPACES {
    extern const char* SYSTEM;      // sys: ota, net, reset, force_wake_up, rtc_epoch
    extern const char* CONFIG;      // cfg: bouffe, remoteVars, gpio, temp_last_valid
    extern const char* STATE;       // state: actSnap, actState, pendingSync
    extern const char* LOGS;        // logs: diagnostics, alerts, crash
    // NOTE: TIME et SENSORS supprimés (fusionnés dans SYSTEM et CONFIG)
}

// Types d'erreurs NVS
enum class NVSError {
    SUCCESS = 0,
    NAMESPACE_NOT_FOUND = 1,
    KEY_NOT_FOUND = 2,
    INSUFFICIENT_SPACE = 3,
    CORRUPTION_DETECTED = 4,
    INVALID_KEY = 5,
    INVALID_VALUE = 6,
    WRITE_FAILED = 7,
    READ_FAILED = 8
};

// Statistiques d'utilisation NVS (simplifiées)
struct NVSUsageStats {
    size_t totalBytes;
    size_t usedBytes;
    size_t freeBytes;
    float usagePercent;
    size_t namespaceCount;
    size_t keyCount;
    
    NVSUsageStats() : totalBytes(0), usedBytes(0), freeBytes(0), 
                      usagePercent(0.0f), namespaceCount(0), keyCount(0) {}
};

class NVSManager {
private:
    friend class NVSLockGuard;
    Preferences _preferences;
    SemaphoreHandle_t _mutex;
    bool _initialized;
    
    // Méthodes privées
    NVSError openNamespace(const char* ns, bool readOnly = true);
    void closeNamespace();
    NVSError validateKey(const char* key);  // Validation simplifiée: longueur max 15 chars
    void logError(NVSError error, const char* context,
                  const char* ns = nullptr, const char* key = nullptr);
    bool lock(TickType_t timeout = portMAX_DELAY);
    void unlock();
    
public:
    NVSManager();
    ~NVSManager();
    
    // Initialisation
    bool begin();
    void end();
    
    // Opérations de base
    NVSError saveString(const char* ns, const char* key, const char* value);
    NVSError loadString(const char* ns, const char* key, char* value,
                       size_t valueSize, const char* defaultValue = "");
    NVSError saveBool(const char* ns, const char* key, bool value);
    NVSError loadBool(const char* ns, const char* key, bool& value, bool defaultValue = false);
    NVSError saveInt(const char* ns, const char* key, int value);
    NVSError loadInt(const char* ns, const char* key, int& value, int defaultValue = 0);
    NVSError saveFloat(const char* ns, const char* key, float value);
    NVSError loadFloat(const char* ns, const char* key, float& value, float defaultValue = 0.0f);
    NVSError saveULong(const char* ns, const char* key, unsigned long value);
    NVSError loadULong(const char* ns, const char* key, unsigned long& value,
                      unsigned long defaultValue = 0);
    
    // Utilitaires
    bool keyExists(const char* ns, const char* key);
    NVSError removeKey(const char* ns, const char* key);
    NVSError clearNamespace(const char* ns);
    
    // Migration depuis l'ancien système
    NVSError migrateFromOldSystem();
    
    // Statistiques et monitoring (simplifiées)
    NVSUsageStats getUsageStats();
    void logUsageStats();
    
    // Debug
    void printNamespaceContents(const char* ns);
    
    // Vérification d'initialisation
    bool isInitialized() const { return _initialized; }
};

// Instance globale
extern NVSManager g_nvsManager;
