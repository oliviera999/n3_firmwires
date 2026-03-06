#pragma once

/**
 * Mock NVS pour tests unitaires
 * v11.166: Ajouté suite à l'audit général
 * 
 * Simule les opérations NVS de base sans dépendance ESP32
 */

#ifdef UNIT_TEST

#include <cstdint>
#include <cstring>
#include <map>
#include <string>

// Types NVS simulés
typedef uint32_t nvs_handle_t;
typedef int esp_err_t;

#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NVS_NOT_FOUND 0x1102

#define NVS_READONLY 0
#define NVS_READWRITE 1

// Stockage mock en mémoire
namespace NVSMock {
    // Clé composite: namespace + "/" + key
    inline std::map<std::string, std::string> g_storage;
    inline nvs_handle_t g_nextHandle = 1;
    inline std::map<nvs_handle_t, std::string> g_openNamespaces;
    
    inline void reset() {
        g_storage.clear();
        g_openNamespaces.clear();
        g_nextHandle = 1;
    }
    
    inline std::string makeKey(const char* ns, const char* key) {
        return std::string(ns) + "/" + key;
    }
}

// Mock des fonctions NVS
inline esp_err_t nvs_open(const char* ns, int mode, nvs_handle_t* handle) {
    *handle = NVSMock::g_nextHandle++;
    NVSMock::g_openNamespaces[*handle] = ns;
    return ESP_OK;
}

inline void nvs_close(nvs_handle_t handle) {
    NVSMock::g_openNamespaces.erase(handle);
}

inline esp_err_t nvs_get_str(nvs_handle_t handle, const char* key, char* out, size_t* length) {
    auto nsIt = NVSMock::g_openNamespaces.find(handle);
    if (nsIt == NVSMock::g_openNamespaces.end()) return ESP_FAIL;
    
    std::string fullKey = NVSMock::makeKey(nsIt->second.c_str(), key);
    auto it = NVSMock::g_storage.find(fullKey);
    
    if (it == NVSMock::g_storage.end()) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    
    if (out == nullptr) {
        // Mode: obtenir la taille requise
        *length = it->second.length() + 1;
        return ESP_OK;
    }
    
    // Mode: copier la valeur
    size_t copyLen = it->second.length();
    if (copyLen >= *length) copyLen = *length - 1;
    strncpy(out, it->second.c_str(), copyLen);
    out[copyLen] = '\0';
    *length = copyLen + 1;
    return ESP_OK;
}

inline esp_err_t nvs_set_str(nvs_handle_t handle, const char* key, const char* value) {
    auto nsIt = NVSMock::g_openNamespaces.find(handle);
    if (nsIt == NVSMock::g_openNamespaces.end()) return ESP_FAIL;
    
    std::string fullKey = NVSMock::makeKey(nsIt->second.c_str(), key);
    NVSMock::g_storage[fullKey] = value;
    return ESP_OK;
}

inline esp_err_t nvs_commit(nvs_handle_t handle) {
    return ESP_OK;
}

inline esp_err_t nvs_erase_key(nvs_handle_t handle, const char* key) {
    auto nsIt = NVSMock::g_openNamespaces.find(handle);
    if (nsIt == NVSMock::g_openNamespaces.end()) return ESP_FAIL;
    
    std::string fullKey = NVSMock::makeKey(nsIt->second.c_str(), key);
    NVSMock::g_storage.erase(fullKey);
    return ESP_OK;
}

#endif // UNIT_TEST
