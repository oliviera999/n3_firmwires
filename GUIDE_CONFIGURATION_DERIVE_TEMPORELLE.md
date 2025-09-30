# Guide de Configuration de la Dérive Temporelle

## Problème résolu

L'ESP32 a une dérive temporelle naturelle due à son oscillateur interne (±20-50 PPM typiquement). Quand l'appareil est hors-ligne, l'heure peut prendre de l'avance de plusieurs minutes en moins d'une heure.

## Configuration dans project_config.h

### 1. Correction de dérive par défaut

```cpp
// Dans namespace SleepConfig (ligne ~349)
constexpr float DEFAULT_DRIFT_CORRECTION_PPM = -25.0f;  // Correction par défaut (-25 PPM)
constexpr bool ENABLE_DEFAULT_DRIFT_CORRECTION = true;   // Activer la correction par défaut
```

**Paramètres :**
- `DEFAULT_DRIFT_CORRECTION_PPM` : Valeur de correction en PPM (Parts Per Million)
  - **Valeurs négatives** : Compensent une dérive positive (heure qui avance)
  - **Valeurs positives** : Compensent une dérive négative (heure qui retarde)
  - **Valeur par défaut** : `-25.0f` (correction de 25 PPM vers l'arrière)

- `ENABLE_DEFAULT_DRIFT_CORRECTION` : Active/désactive la correction automatique
  - `true` : Correction automatique activée
  - `false` : Correction désactivée

### 2. Indicateur visuel sur OLED

```cpp
// Dans namespace MonitoringConfig (ligne ~978)
constexpr bool ENABLE_DRIFT_VISUAL_INDICATOR = true;    // Activer l'indicateur visuel
constexpr uint32_t DRIFT_CHECK_INTERVAL_MS = 10000;     // Intervalle de vérification (10s)
```

**Paramètres :**
- `ENABLE_DRIFT_VISUAL_INDICATOR` : Active/désactive l'indicateur visuel
  - `true` : Point blanc en haut à droite quand hors-ligne
  - `false` : Pas d'indicateur visuel

- `DRIFT_CHECK_INTERVAL_MS` : Fréquence de vérification de l'état WiFi
  - Valeur par défaut : `10000` (10 secondes)

## Comment ajuster la correction

### 1. Test initial
- Laisser l'ESP32 hors-ligne pendant 1 heure
- Noter la dérive observée (avance/retard en secondes)
- Calculer la dérive en PPM : `(dérive_secondes / temps_écoulé_secondes) * 1000000`

### 2. Ajustement de la correction
- Si l'heure **avance** : utiliser une valeur **négative** plus forte
  - Exemple : si dérive de +30 PPM, utiliser `-30.0f`
- Si l'heure **retarde** : utiliser une valeur **positive**
  - Exemple : si dérive de -20 PPM, utiliser `+20.0f`

### 3. Exemples de configuration

```cpp
// Pour une dérive positive de 30 PPM (heure avance)
constexpr float DEFAULT_DRIFT_CORRECTION_PPM = -30.0f;

// Pour une dérive négative de 15 PPM (heure retarde)
constexpr float DEFAULT_DRIFT_CORRECTION_PPM = +15.0f;

// Pour désactiver la correction
constexpr bool ENABLE_DEFAULT_DRIFT_CORRECTION = false;
```

## Fonctionnement

### Quand la correction s'applique
- **Seulement** quand l'ESP32 est hors-ligne (`WiFi.status() != WL_CONNECTED`)
- **Seulement** si aucune synchronisation NTP récente n'a eu lieu
- **Seulement** si la correction calculée est ≥ 1 seconde

### Fréquence de correction
- Toutes les 5 minutes (`DRIFT_CORRECTION_INTERVAL_MS = 300000`)
- La correction est cumulative (s'ajoute au temps écoulé depuis le démarrage)

### Logs de debug
```
[Power] Correction de dérive par défaut appliquée: -15 s (dérive configurée: -25.00 PPM)
```

## Recommandations

1. **Test progressif** : Commencer avec `-25.0f` et ajuster selon les observations
2. **Monitoring** : Surveiller les logs pour voir les corrections appliquées
3. **Synchronisation NTP** : Maintenir une connexion WiFi périodique pour une précision optimale
4. **Température** : La dérive peut varier avec la température ambiante

## Désactivation complète

Pour désactiver complètement la correction de dérive :

```cpp
constexpr bool ENABLE_DEFAULT_DRIFT_CORRECTION = false;
constexpr bool ENABLE_DRIFT_VISUAL_INDICATOR = false;
```

## Impact sur les performances

- **CPU** : Négligeable (calcul simple toutes les 5 minutes)
- **Mémoire** : Négligeable (quelques variables statiques)
- **Flash** : Négligeable (sauvegarde périodique de l'heure)
- **Précision** : Amélioration significative de la précision temporelle hors-ligne
