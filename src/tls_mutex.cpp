/**
 * @file tls_mutex.cpp
 * @brief Implémentation du mutex global TLS
 * 
 * Ce fichier définit les variables statiques du mutex TLS utilisé pour
 * sérialiser les connexions SMTP et HTTPS afin d'éviter les crashs par
 * manque de mémoire.
 */

#include "tls_mutex.h"

// Variable statique du mutex
SemaphoreHandle_t TLSMutex::_mutex = nullptr;

// Flag global indiquant la transition vers light sleep
// Initialisé à false, mis à true par PowerManager::goToLightSleep()
volatile bool g_enteringLightSleep = false;
