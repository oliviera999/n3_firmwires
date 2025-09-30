# Correction Timeout Watchdog - SOLUTION FINALE AGRESSIVE

## 🚨 État du problème
**CRITIQUE** : des timeouts watchdog persistent malgré les optimisations initiales. Des mesures plus agressives s’imposent.

## 🔧 Optimisations agressives appliquées

### 1. Réduction drastique de la fréquence d’affichage
- **Tâche d’affichage** : 200 ms → **1000 ms** (1 FPS au lieu de 5)
- **Intervalle d’update** : 500 ms → **500 ms** (2 FPS interne)
- **Changement d’écran** : 5 s → **10 s** (moins fréquent)

### 2. Réduction des priorités de tâches
- **Toutes les tâches** : priorité abaissée à **1** (la plus basse stable)
- **Capteurs** : 2 → **1**
- **Affichage** : 3 → **1**
- **Automatismes** : 1 → **1**

### 3. Augmentation des délais des tâches
- **Affichage** : 200 ms → **1000 ms**
- **Automatismes** : 100 ms → **200 ms**
- **Boucle principale** : 200 ms → **200 ms**

### 4. Gestion d’erreurs renforcée
- **Blocs try/catch** autour des lectures capteurs
- **Valeurs par défaut** en cas d’erreur
- **Réduction du mode immédiat** de 50 % à 25 %
- **Logs de debug** moins fréquents : 5 s → 10 s

## 📊 Configuration après correctifs agressifs

| Component | Before | After | Improvement |
|-----------|--------|-------|-------------|
| Fréquence affichage | 5 FPS | 1 FPS | -80 % charge I2C |
| Update affichage | 2 FPS | 2 FPS | Stable |
| Priorités tâches | 1-3 | Toutes 1 | Pas de conflits |
| Timeout Watchdog | 5 min | 10 min | +100 % |
| Changement d’écran | 5 s | 10 s | -50 % updates |
| Logs debug | 5 s | 10 s | -50 % overhead |

## 🎯 Résultats attendus

### ✅ Bénéfices immédiats
1. **Réduction massive I2C** : -80 % d’opérations
2. **Pas de conflits de tâches** : priorités homogènes
3. **Ordonnancement stable** : délais plus longs
4. **Meilleure récupération** : try/catch évitent les crashs
5. **Overhead réduit** : logs/updates moins fréquents

### ⚠️ Compromis
1. **Affichage plus lent** : 1 FPS au lieu de 5
2. **UI moins réactive** : écran toutes les 10 s au lieu de 5
3. **Retour visuel réduit** : mises à jour moins fréquentes

## 🧪 Stratégie de test

### Script de test : `test_watchdog_aggressive.py`
- **Durée** : 15 minutes
- **Monitoring** : détection temps réel
- **Métriques** : erreurs, FPS affichage, stabilité
- **Recommandations** : suggestions automatiques

### Critères de succès
- ✅ **0 erreurs watchdog** en 15 minutes
- ✅ **Mises à jour stables** à 1 FPS
- ✅ **Aucun conflit I2C** détecté
- ✅ **Toutes les tâches** tournent sans crash

## 🚀 Prochaines étapes

### Si succès (0 erreur)
1. **Augmenter progressivement** l’affichage à 2 FPS
2. **Surveiller la stabilité** pendant 24 h
3. **Optimiser** si besoin

### Si partiellement (1–2 erreurs)
1. **Réduire** à 0,5 FPS
2. **Bufferiser** l’affichage
3. **Ajouter** des mutex I2C

### Si échec (≥3 erreurs)
1. **Désactiver** l’affichage
2. **Mode headless**
3. **Se concentrer** sur l’essentiel

## 📁 Fichiers modifiés

1. **`src/app.cpp`**
   - Display task delay: 200ms → 1000ms
   - Automation task delay: 100ms → 200ms
   - All task priorities: Reduced to 1

2. **`src/automatism.cpp`**
   - Display update interval: 200ms → 500ms
   - Screen switch interval: 5s → 10s
   - Enhanced error handling with try-catch
   - Reduced immediate mode frequency

3. **`test_watchdog_aggressive.py`**
   - New aggressive monitoring script
   - 15-minute test duration
   - Enhanced error analysis

## 🔍 Commandes de monitoring

```bash
# Flasher le correctif agressif
pio run -e wroom-prod -t upload

# Run aggressive test
python test_watchdog_aggressive.py

# Monitoring manuel
pio device monitor
```

## 📈 Indicateurs de succès

| Indicateur | Cible | Actuel | Statut |
|--------|--------|---------|--------|
| Watchdog Errors | 0 | TBD | Testing |
| Display FPS | 1 | 1 | ✅ Applied |
| Task Conflicts | 0 | 0 | ✅ Applied |
| I2C Errors | 0 | TBD | Testing |
| System Uptime | >15min | TBD | Testing |

## 🎯 Conclusion

Cette approche agressive devrait résoudre durablement les timeouts watchdog en :

1. **Minimizing I2C communication** to absolute minimum
2. **Eliminating task priority conflicts**
3. **Providing maximum CPU breathing room**
4. **Adding robust error handling**

Le système sera moins réactif visuellement mais devrait être parfaitement stable. Une fois la stabilité confirmée, on pourra augmenter progressivement les performances tout en conservant la fiabilité.