# Guide de Configuration du Sommeil - FFP3CS4

Ce guide explique comment configurer tous les paramètres de sommeil et d'économie d'énergie dans le fichier `include/project_config.h`.

## 📁 Localisation

Toutes les configurations de sommeil sont centralisées dans :
```
include/project_config.h
namespace SleepConfig { ... }
```

## ⚙️ Configuration des Délais d'Inactivité

### Délais Avant Sommeil

```cpp
// Délai d'inactivité général avant autorisation de sommeil
constexpr uint32_t INACTIVITY_DELAY_MS = 300000;         // 5 minutes d'inactivité générale

// Délai minimum absolu depuis le dernier réveil
constexpr uint32_t MIN_AWAKE_TIME_MS = 60000;           // 60 secondes minimum éveillé

// Délai d'inactivité web avant autorisation de sommeil
constexpr uint32_t WEB_INACTIVITY_TIMEOUT_MS = 600000;   // 10 minutes d'inactivité web
```

**Explication :**
- `INACTIVITY_DELAY_MS` : Temps d'inactivité générale avant de pouvoir dormir
- `MIN_AWAKE_TIME_MS` : Protection minimale après chaque réveil
- `WEB_INACTIVITY_TIMEOUT_MS` : Délai spécifique pour l'activité web

## 🕐 Configuration des Délais d'Inactivité Adaptatifs

### ⚠️ **IMPORTANT : Distinction Cruciale**

**Ces constantes contrôlent les DÉLAIS D'INACTIVITÉ avant autorisation de sommeil, PAS la durée de sommeil elle-même !**

- **Délais d'inactivité** : Configurés localement (ci-dessous)
- **Durée de sommeil** : Contrôlée par le serveur distant via `freqWakeSec`

### Délais Adaptatifs

```cpp
// Délai d'inactivité minimum (surveillance accrue)
constexpr uint32_t MIN_INACTIVITY_DELAY_SEC = 300;             // 5 minutes minimum d'inactivité

// Délai d'inactivité maximum (limite supérieure)
constexpr uint32_t MAX_INACTIVITY_DELAY_SEC = 3600;            // 1 heure maximum d'inactivité

// Délai d'inactivité normal (fonctionnement standard)
constexpr uint32_t NORMAL_INACTIVITY_DELAY_SEC = 600;          // 10 minutes normal d'inactivité

// Délai d'inactivité en cas d'erreurs (surveillance accrue)
constexpr uint32_t ERROR_INACTIVITY_DELAY_SEC = 300;            // 5 minutes si erreurs

// Délai d'inactivité nocturne (économie d'énergie)
constexpr uint32_t NIGHT_INACTIVITY_DELAY_SEC = 300;          // 5 minutes la nuit
```

**Logique d'adaptation :**
- **Erreurs détectées** → `ERROR_INACTIVITY_DELAY_SEC` (5 min d'inactivité)
- **Fonctionnement normal** → `NORMAL_INACTIVITY_DELAY_SEC` (10 min d'inactivité)
- **Période nocturne** → `NIGHT_INACTIVITY_DELAY_SEC` (5 min d'inactivité - économie d'énergie)
- **Échecs répétés** → Multiplication par `WAKEUP_FAILURE_DELAY_MULTIPLIER`

## 🌙 Configuration Nocturne

```cpp
// Activation du sommeil adaptatif
constexpr bool ADAPTIVE_SLEEP_ENABLED = true;            // Sleep adaptatif activé

// Heures de nuit pour sommeil prolongé (format 24h)
constexpr uint8_t NIGHT_START_HOUR = 22;                 // Début nuit : 22h
constexpr uint8_t NIGHT_END_HOUR = 6;                   // Fin nuit : 6h
```

**Période nocturne :** 22h00 → 06h00 (configurable)

## 🌊 Configuration Marée

```cpp
// Seuil de déclenchement marée montante (en cm)
constexpr int16_t TIDE_TRIGGER_THRESHOLD_CM = 1;         // 1 cm de différence
```

**Fonctionnement :**
- Si différence de niveau > `TIDE_TRIGGER_THRESHOLD_CM` → Sommeil anticipé autorisé
- Permet d'économiser l'énergie pendant les montées de niveau

## ✅ Validation Système

```cpp
// Vérifier que les capteurs répondent avant sommeil
constexpr bool VALIDATE_SENSORS_BEFORE_SLEEP = true;     // Validation capteurs

// Vérifier la connexion WiFi avant sommeil
constexpr bool VALIDATE_WIFI_BEFORE_SLEEP = true;        // Validation WiFi

// Vérifier les niveaux d'eau avant sommeil
constexpr bool VALIDATE_WATER_LEVELS_BEFORE_SLEEP = true; // Validation niveaux
```

**Sécurité :** Le système valide l'état avant de dormir pour éviter les problèmes.

## ⚠️ Gestion des Échecs

```cpp
// Seuil d'alerte pour échecs consécutifs
constexpr uint8_t WAKEUP_FAILURE_ALERT_THRESHOLD = 3;    // Alerte après 3 échecs

// Facteur de multiplication du délai en cas d'échecs
constexpr uint8_t WAKEUP_FAILURE_DELAY_MULTIPLIER = 2;   // Double le délai
```

**Comportement :**
- Après 3 échecs → Alerte dans les logs
- Chaque échec → Délai multiplié par 2 (jusqu'au maximum)

## 🔋 Optimisations Énergétiques

```cpp
// Sauvegarder l'heure avant sommeil
constexpr bool SAVE_TIME_BEFORE_SLEEP = true;            // Sauvegarde heure

// Sauvegarder les identifiants WiFi avant sommeil
constexpr bool SAVE_WIFI_CREDENTIALS_BEFORE_SLEEP = true; // Sauvegarde WiFi

// Déconnecter WiFi avant sommeil pour économie
constexpr bool DISCONNECT_WIFI_BEFORE_SLEEP = true;      // Déconnexion WiFi

// Reconnexion automatique WiFi au réveil
constexpr bool AUTO_RECONNECT_WIFI_AFTER_SLEEP = true;  // Reconnexion auto
```

**Économie d'énergie :**
- Déconnexion WiFi pendant le sommeil
- Sauvegarde des données critiques
- Reconnexion automatique au réveil

## 📊 Exemples de Configuration

### Configuration Économie Maximale
```cpp
constexpr uint32_t NORMAL_INACTIVITY_DELAY_SEC = 1800;    // 30 min d'inactivité normale
constexpr uint32_t NIGHT_INACTIVITY_DELAY_SEC = 3600;     // 1h d'inactivité la nuit
constexpr uint8_t NIGHT_START_HOUR = 20;                  // Nuit dès 20h
constexpr uint8_t NIGHT_END_HOUR = 8;                    // Jusqu'à 8h
```

### Configuration Surveillance Accrue
```cpp
constexpr uint32_t NORMAL_INACTIVITY_DELAY_SEC = 300;     // 5 min d'inactivité normale
constexpr uint32_t ERROR_INACTIVITY_DELAY_SEC = 180;      // 3 min d'inactivité si erreurs
constexpr uint32_t INACTIVITY_DELAY_MS = 120000;          // 2 min d'inactivité générale
```

### Configuration Équilibre
```cpp
constexpr uint32_t NORMAL_INACTIVITY_DELAY_SEC = 600;     // 10 min d'inactivité normale
constexpr uint32_t NIGHT_INACTIVITY_DELAY_SEC = 1800;     // 30 min d'inactivité la nuit
constexpr uint32_t INACTIVITY_DELAY_MS = 300000;          // 5 min d'inactivité générale
```

## 🔄 **Contrôle Hybride du Sommeil**

### **Délais d'Inactivité (Local)**
- Configurés dans `project_config.h`
- Contrôlent **quand** le système peut dormir
- Adaptatifs selon les conditions

### **Durée de Sommeil (Multiplicative)**
- **Par défaut** : Contrôlée par le serveur via `freqWakeSec`
- **Logique multiplicative** : Si `LOCAL_SLEEP_DURATION_CONTROL = true`
  - **Jour** : `freqWakeSec` (serveur normal)
  - **Nuit** : `freqWakeSec × NIGHT_SLEEP_MULTIPLIER` (serveur × 3)

### **Nouvelle Logique Nocturne Multiplicative**
- **Inactivité** : 5 min (÷2 par rapport au jour)
- **Sommeil** : `freqWakeSec × 3` (multiplicatif selon serveur)
- **Flexibilité** : S'adapte aux changements du serveur !
- **Exemples** :
  - Serveur = 5 min → Nuit = 15 min (75% économie)
  - Serveur = 10 min → Nuit = 30 min (86% économie)
  - Serveur = 15 min → Nuit = 45 min (90% économie)

## 📊 **Exemples Concrets de Fonctionnement**

### **Scénario 1 : Serveur Standard (10 min)**
```
Jour:  5 min inactivité + 10 min sommeil = 15 min cycle (67% économie)
Nuit:  5 min inactivité + 30 min sommeil = 35 min cycle (86% économie)
```

### **Scénario 2 : Serveur Rapide (5 min)**
```
Jour:  5 min inactivité + 5 min sommeil  = 10 min cycle (50% économie)
Nuit:  5 min inactivité + 15 min sommeil = 20 min cycle (75% économie)
```

### **Scénario 3 : Serveur Lent (15 min)**
```
Jour:  5 min inactivité + 15 min sommeil = 20 min cycle (75% économie)
Nuit:  5 min inactivité + 45 min sommeil = 50 min cycle (90% économie)
```

## 🔧 Modification des Paramètres

1. **Ouvrir** `include/project_config.h`
2. **Localiser** `namespace SleepConfig`
3. **Modifier** les valeurs selon vos besoins
4. **Compiler** le projet
5. **Tester** le comportement

## 📝 Logs de Debug

Le système affiche des logs détaillés :
```
[Auto] Sleep adaptatif: délai réduit (erreurs récentes)
[Auto] Sleep adaptatif: délai augmenté (nuit)
[Auto] Sleep: inactivité (pompeReserv=OFF, nourrissage=NON, decompteActif=NON)
[Auto] Sleep: marée montante (~10s, +3 cm)
```

## ⚡ Impact sur l'Économie d'Énergie

| Configuration | Consommation | Réactivité |
|---------------|--------------|------------|
| **Économie max** | ⭐⭐⭐⭐⭐ | ⭐⭐ |
| **Équilibre** | ⭐⭐⭐ | ⭐⭐⭐⭐ |
| **Surveillance** | ⭐ | ⭐⭐⭐⭐⭐ |

## 🚨 Points d'Attention

- **Ne pas descendre** en dessous de 60 secondes pour `MIN_AWAKE_TIME_MS`
- **Tester** les modifications en environnement contrôlé
- **Surveiller** les logs après modification
- **Vérifier** la stabilité du WiFi avec les nouveaux délais

---

*Configuration centralisée dans `include/project_config.h` - Version 10.02*
