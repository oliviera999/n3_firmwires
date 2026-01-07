# Rapport d'Audit Technique - Projet FFP5CS (ESP32)

**Date**: 28/11/2025
**Version analysée**: v11.117 (estimée)
**Cible Hardware**: ESP32-WROOM-32 (Production), ESP32-S3 (Supporté)

Ce rapport synthétise l'analyse statique du code source, de l'architecture et de la configuration du projet.

---

## 1. Synthèse Globale

Le projet souffre d'une **suringénierie significative** qui complexifie la maintenance et masque les risques réels (stabilité, mémoire). Bien que fonctionnel, le code contient de nombreuses abstractions inutiles pour la plateforme cible (WROOM) et des mécanismes de "sur-qualité" (monitoring excessif) qui consomment des ressources inutilement.

**Note Globale**: 🟠 **Dette Technique Élevée**

| Domaine | État | Observations |
| :--- | :---: | :--- |
| **Architecture** | 🟠 | Trop fragmentée (Controllers, Managers, Helpers dispersés). |
| **Configuration** | 🔴 | **Critique**. Éclatée sur 20+ fichiers/namespaces. Illisible. |
| **Mémoire** | 🟠 | Risques de Stack Overflow (gros buffers en local). Fragmentation `String`. |
| **Performance** | 🟢 | Correcte, mais ralentie par des abstractions inutiles (JSON Pool, Cache). |
| **Stabilité** | 🟡 | Blocages potentiels dans le WiFi. Watchdog géré mais boucles synchrones présentes. |
| **Code Mort** | 🔴 | Classes entières inutiles (`PSRAMOptimizer` sur WROOM, `NetworkOptimizer`). |

---

## 2. Problèmes Critiques & Risques (🔴)

### 2.1. Configuration "Explosée"
Le fichier `include/project_config.h` n'est qu'un point d'entrée vers **11 autres fichiers** de configuration (`config/*.h`).
*   **Problème**: Il est impossible d'avoir une vue d'ensemble des timeouts, des pins ou des seuils.
*   **Risque**: Incohérences (ex: `TimingConfig` vs `TimingConfigExtended`), difficulté de maintenance, et erreurs lors des changements d'environnement.

### 2.2. Risques de Stack Overflow
Dans `src/app.cpp`, une allocation massive est faite sur la stack à l'intérieur d'une lambda :
```cpp
// src/app.cpp:176
char bodyBuffer[BufferConfig::EMAIL_DIGEST_MAX_SIZE_BYTES]; 
```
Si `EMAIL_DIGEST_MAX_SIZE_BYTES` dépasse 2-3 KB (taille par défaut de stack FreeRTOS ~4-8KB), cela provoquera un **Guru Meditation (Stack Canary)** aléatoire lors de l'envoi de mails.
**Recommendation**: Passer ce buffer en variable statique ou globale, ou l'allouer sur le tas (Heap).

### 2.3. Blocages WiFi
La méthode `WifiManager::connect` utilise une boucle d'attente semi-active :
```cpp
// src/wifi_manager.cpp:107
while (WiFi.status() != WL_CONNECTED && millis() - start < _timeoutMs) {
  vTaskDelay(pdMS_TO_TICKS(100)); 
}
```
Bien que le `vTaskDelay` empêche le déclenchement du Watchdog matériel, cette boucle **bloque le thread principal** pendant toute la durée du timeout (ex: 10-15s). Cela retarde les autres traitements critiques.

### 2.4. Endpoints Dangereux en Production
Le serveur Web expose `/nvs/set`, `/nvs/erase` et `/fs/format` si `FFP_ENABLE_DANGEROUS_ENDPOINTS` est défini.
*   **Problème**: Ces endpoints permettent de corrompre la mémoire non-volatile ou d'effacer le système de fichiers sans authentification forte (juste un accès réseau).

---

## 3. Suringénierie & Code Mort (🟡)

### 3.1. Optimiseurs "Fantômes"
*   **`PSRAMOptimizer`**: Cette classe (195 lignes) est compilée et incluse partout, mais ses méthodes retournent `false` ou `0` sur ESP32-WROOM (cible principale). C'est du bruit cognitif et du code mort.
*   **`JsonPool`**: Implémente un pool d'objets `DynamicJsonDocument` avec mutex.
    *   *Analyse*: La gestion mémoire de l'ESP32 est suffisante pour des allocations/désallocations ponctuelles. Le pooling ajoute de la complexité (risques de deadlocks, leaks si non release) pour un gain de performance négligeable sur un serveur web domotique qui traite < 1 req/sec.
*   **`SensorCache`**: Ajoute une couche de complexité pour éviter de relire les capteurs. Une simple variable globale `lastReadTime` suffirait.

### 3.2. Monitoring Excessif
*   **`TimeDriftMonitor`**: (469 lignes) Calcule la dérive PPM de l'horloge. Pour un système qui synchronise NTP toutes les heures, une dérive de quelques millisecondes est non-critique.
*   **`TaskMonitor`**: Capture des snapshots d'état des tâches FreeRTOS. Utile en debug profond, lourd en production.

### 3.3. Abstractions Web
*   **`WebServerContext`**: Une classe entière pour passer des références (`autoCtrl`, `mailer`, `config`...) aux routes. Ces objets sont déjà globaux ou singletons. Passer `g_appContext` ou utiliser les singletons directement simplifierait le code.

---

## 4. Qualité du Code & Architecture

### 4.1. Points Positifs
*   Usage extensif de `ArduinoJson` (v7 moderne).
*   Séparation claire entre `SystemSensors`, `SystemActuators` et `Automatism`.
*   Gestion asynchrone (AsyncWebServer) généralement respectée.

### 4.2. Points à Améliorer
*   **God Class `Automatism`**: Cette classe semble gérer trop de choses (Alertes, Display, Refill, Feeding, Network). Le découpage en "controllers" (`automatism_*.cpp`) est artificiel car ils sont fortement couplés (friend classes).
*   **Logique Dispersée**: Le parsing des paramètres HTTP pour la configuration se fait manuellement dans `web_server.cpp` (`/dbvars/update`), dupliquant potentiellement la logique de validation présente ailleurs.

---

## 5. Plan de Remédiation Recommandé

### Phase 1 : Nettoyage (Immédiat)
1.  **Supprimer `PSRAMOptimizer`** et `include/psram_optimizer.h`. Remplacer par des appels directs à `malloc` ou `new` (l'OS gère le fallback RAM interne).
2.  **Supprimer `JsonPool`**. Allouer les `DynamicJsonDocument` localement (stack ou heap simple).
3.  **Sécuriser le buffer `emailDigest`** : Le déplacer en variable statique globale pour protéger la stack.

### Phase 2 : Simplification (Court terme)
4.  **Fusionner la Configuration** : Regrouper tous les fichiers `config/*.h` en un seul `config.h` structuré ou 2-3 fichiers max (Pins, Settings, Secrets).
5.  **Simplifier le Monitoring** : Supprimer `TimeDriftMonitor` complet. Garder juste un log "NTP Sync OK".
6.  **Nettoyer `WebServerManager`** : Retirer les endpoints `/nvs` et `/fs` des builds de production (via `#ifdef`).

### Phase 3 : Refactoring (Moyen terme)
7.  **Revoir `WifiManager`** : Implémenter une machine à états non-bloquante pour la connexion.
8.  **Standardiser `Automatism`** : Transformer les "controllers" amis en vrais composants indépendants (ex: `FeedingService`, `WaterService`).

---
*Fin du rapport*




