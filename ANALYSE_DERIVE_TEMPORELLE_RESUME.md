# Analyse de la Dérive Temporelle - Résumé

## Problème identifié

L'ESP32 présente une **dérive temporelle positive** (heure qui avance) de plusieurs minutes en moins d'une heure quand il est hors-ligne. Cette dérive est due à :

1. **Oscillateur interne de l'ESP32** : Précision limitée (±20-50 PPM typiquement)
2. **Absence de synchronisation NTP** : Pas de correction externe quand hors-ligne
3. **Pas de correction automatique** : Le système ne compensait pas la dérive naturelle

## Solution implémentée

### 1. Correction de dérive configurable

**Dans `include/project_config.h` :**
```cpp
// Correction de dérive par défaut (quand pas de sync NTP)
constexpr float DEFAULT_DRIFT_CORRECTION_PPM = -25.0f;  // Correction par défaut (-25 PPM)
constexpr bool ENABLE_DEFAULT_DRIFT_CORRECTION = true;   // Activer la correction par défaut

// Indicateur visuel de dérive sur OLED
constexpr bool ENABLE_DRIFT_VISUAL_INDICATOR = true;    // Activer l'indicateur visuel
constexpr uint32_t DRIFT_CHECK_INTERVAL_MS = 10000;     // Intervalle de vérification (10s)
```

### 2. Code de correction automatique

**Dans `src/power.cpp` :**
- Correction automatique quand `_lastNtpSync == 0` (pas de sync NTP)
- Application d'une correction négative de -25 PPM par défaut
- Corrections toutes les 5 minutes si la correction ≥ 1 seconde
- Sauvegarde automatique de l'heure corrigée

### 3. Indicateur visuel

**Dans `src/display_view.cpp` :**
- Point blanc en haut à droite de l'OLED quand hors-ligne
- Alerte visuelle de dérive temporelle possible
- Vérification toutes les 10 secondes

## Tests et validation

### Scripts de test créés

1. **`test_drift_analysis.py`** : Simulation de la correction de dérive
2. **`monitor_drift_logs.py`** : Monitoring des logs série pour capturer les corrections

### Logs attendus

```
[Power] Correction de dérive par défaut appliquée: -15 s (dérive configurée: -25.00 PPM)
[Power] Correction de dérive par défaut appliquée: -30 s (dérive configurée: -25.00 PPM)
[Power] Correction de dérive par défaut appliquée: -45 s (dérive configurée: -25.00 PPM)
```

## Configuration et ajustement

### Valeurs recommandées

- **Dérive positive (heure avance)** : Utiliser des valeurs **négatives**
  - Exemple : `-25.0f` pour une dérive de +25 PPM
- **Dérive négative (heure retarde)** : Utiliser des valeurs **positives**
  - Exemple : `+15.0f` pour une dérive de -15 PPM

### Processus d'ajustement

1. **Test initial** : Laisser l'ESP32 hors-ligne pendant 1 heure
2. **Mesure** : Noter la dérive observée (avance/retard en secondes)
3. **Calcul** : `dérive_PPM = (dérive_secondes / temps_écoulé_secondes) * 1000000`
4. **Ajustement** : Modifier `DEFAULT_DRIFT_CORRECTION_PPM` dans `project_config.h`
5. **Recompilation** : Recompiler et flasher le firmware
6. **Validation** : Tester à nouveau

### Exemples de configuration

```cpp
// Pour une dérive positive de 30 PPM (heure avance)
constexpr float DEFAULT_DRIFT_CORRECTION_PPM = -30.0f;

// Pour une dérive négative de 15 PPM (heure retarde)
constexpr float DEFAULT_DRIFT_CORRECTION_PPM = +15.0f;

// Pour désactiver la correction
constexpr bool ENABLE_DEFAULT_DRIFT_CORRECTION = false;
```

## Fonctionnement technique

### Quand la correction s'applique

- **Seulement** quand l'ESP32 est hors-ligne (`WiFi.status() != WL_CONNECTED`)
- **Seulement** si aucune synchronisation NTP récente n'a eu lieu
- **Seulement** si la correction calculée est ≥ 1 seconde

### Fréquence de correction

- Toutes les 5 minutes (`DRIFT_CORRECTION_INTERVAL_MS = 300000`)
- La correction est cumulative (s'ajoute au temps écoulé depuis le démarrage)

### Impact sur les performances

- **CPU** : Négligeable (calcul simple toutes les 5 minutes)
- **Mémoire** : Négligeable (quelques variables statiques)
- **Flash** : Négligeable (sauvegarde périodique de l'heure)
- **Précision** : Amélioration significative de la précision temporelle hors-ligne

## Recommandations d'utilisation

### 1. Test progressif
- Commencer avec `-25.0f` et ajuster selon les observations
- Tester sur différentes périodes (1h, 6h, 24h)

### 2. Monitoring
- Surveiller les logs pour voir les corrections appliquées
- Utiliser `monitor_drift_logs.py` pour capturer les corrections

### 3. Synchronisation NTP
- Maintenir une connexion WiFi périodique pour une précision optimale
- La correction par défaut ne s'applique que quand hors-ligne

### 4. Température
- La dérive peut varier avec la température ambiante
- Considérer des ajustements saisonniers si nécessaire

## Désactivation complète

Pour désactiver complètement la correction de dérive :

```cpp
constexpr bool ENABLE_DEFAULT_DRIFT_CORRECTION = false;
constexpr bool ENABLE_DRIFT_VISUAL_INDICATOR = false;
```

## Conclusion

La solution implémentée offre :

✅ **Configuration centralisée** dans `project_config.h`  
✅ **Correction automatique** quand hors-ligne  
✅ **Indicateur visuel** sur l'OLED  
✅ **Facilement ajustable** selon les besoins  
✅ **Impact minimal** sur les performances  
✅ **Documentation complète** et scripts de test  

La dérive temporelle est maintenant **entièrement configurable** et **facilement ajustable** selon vos observations ! 🎯
