#pragma once

// IMPORTANT: Ce fichier contient des informations sensibles
// Ne jamais commiter ce fichier dans le dépôt git !

// Inclure explicitement WifiManager pour le type Credential, sans contrainte d'ordre externe
#include "wifi_manager.h"

namespace Secrets {
// Email pour les notifications
constexpr const char* AUTHOR_EMAIL = "arnould.svt@gmail.com";
constexpr const char* AUTHOR_PASSWORD = "ddbfvlkssfleypdr";

// Liste des réseaux WiFi connus
inline constexpr WifiManager::Credential WIFI_LIST[] = {
  { "AP-Techno-T06", "Techno2024!" },
  { "inwi Home 4G 8306D9", "5KBB52W62M" },
  { "AndroidAP", "123456789" },
  //{ "raspN3", "n3LLrasp" },
  //{ "dlink", "n3LLdlink" } 
};

inline constexpr size_t WIFI_COUNT = sizeof(WIFI_LIST) / sizeof(WIFI_LIST[0]);
}