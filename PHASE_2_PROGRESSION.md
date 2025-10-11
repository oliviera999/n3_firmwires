# PHASE 2 - PROGRESSION REFACTORING

**Date début**: 2025-10-11  
**Commit initial**: 167fb63  
**Objectif**: Diviser automatism.cpp (3421 lignes) en 6 modules

---

## 📊 PROGRESSION GLOBALE

### État Actuel

**automatism.cpp actuel**: 3391 lignes (-30 après Persistence)  
**Modules créés**: 1/6 (17%)  
**Méthodes extraites**: 3/47 (6%)  
**Temps écoulé**: ~1h

### Modules

| Module | Méthodes | Lignes estim. | Statut | Commit |
|--------|----------|---------------|--------|--------|
| ✅ **Persistence** | 3 | ~50 | TERMINÉ | 167fb63 |
| ⏳ **Actuators** | 10 | ~600 | EN COURS | - |
| ⏸️ **Feeding** | 8 | ~500 | À FAIRE | - |
| ⏸️ **Network** | 5 | ~700 | À FAIRE | - |
| ⏸️ **Sleep** | 13 | ~800 | À FAIRE | - |
| ⏸️ **Core** | 8 | ~700 | À FAIRE | - |

**Total**: 1/6 modules (17%)

---

## ✅ MODULE 1: PERSISTENCE (TERMINÉ)

### Détails

**Commit**: 167fb63  
**Fichiers créés**:
- `src/automatism/automatism_persistence.h`
- `src/automatism/automatism_persistence.cpp`

**Méthodes extraites** (3):
1. ✅ `saveActuatorSnapshot()` - Sauvegarde NVS
2. ✅ `loadActuatorSnapshot()` - Chargement NVS
3. ✅ `clearActuatorSnapshot()` - Effacement

**Modifications automatism.cpp**:
- Include ajouté: `#include "automatism/automatism_persistence.h"`
- 3 méthodes remplacées par délégations
- ~30 lignes déplacées

**Build**:
- ✅ Compilation SUCCESS
- Flash: 80.9% (+220 bytes)
- RAM: 22.1% (inchangé)

**Durée**: ~1h

---

## ⏳ MODULE 2: ACTUATORS (EN COURS)

### Objectif

Extraire 10 méthodes de contrôle manuel actionneurs

**Méthodes cibles**:
1. ⏳ `startAquaPumpManualLocal()` - ligne 78
2. ⏳ `stopAquaPumpManualLocal()` - ligne 124
3. ⏳ `startHeaterManualLocal()` - ligne 171
4. ⏳ `stopHeaterManualLocal()` - ligne 209
5. ⏳ `startLightManualLocal()` - ligne 247
6. ⏳ `stopLightManualLocal()` - ligne 286
7. ⏳ `toggleEmailNotifications()` - ligne 325
8. ⏳ `toggleForceWakeup()` - ligne 341
9. ⏳ `startTankPumpManual()` - ligne 2522
10. ⏳ `stopTankPumpManual()` - ligne 2594

### Complexité

**Pattern dupliqué détecté** (lignes 78-122, 124-171, etc.):
```cpp
void startXxxManualLocal() {
    // 1. Action immédiate
    // 2. WebSocket broadcast
    // 3. Tâche async pour sync serveur (13 lignes dupliquées !)
}
```

**Solution**: Factoriser avec helper `syncActuatorStateAsync()`

### Estimations

**Lignes**: ~600 (incluant factorisation)  
**Durée**: 4-6h  
**Difficulté**: ⚙️⚙️ Moyen (duplication de code)

---

## ⏸️ MODULES 3-6 (À FAIRE)

### Module 3: Feeding (8 méthodes, ~500 lignes)
- handleFeeding()
- manualFeedSmall(), manualFeedBig()
- createFeedingMessage()
- traceFeedingEvent(), traceFeedingEventSelective()
- checkNewDay(), saveFeedingState()

### Module 4: Network (5 méthodes, ~700 lignes)
- fetchRemoteState()
- applyConfigFromJson()
- sendFullUpdate()
- handleRemoteState()
- checkCriticalChanges()

### Module 5: Sleep (13 méthodes, ~800 lignes)
- handleAutoSleep() - **443 lignes !**
- shouldEnterSleepEarly()
- handleMaree()
- Méthodes activité/validation/anomalies

### Module 6: Core (orchestration, ~700 lignes)
- begin(), update(), updateDisplay()
- handleRefill(), handleAlerts()
- Coordination générale

---

## 📈 MÉTRIQUES

### Réduction Code par Module

| Module | Avant | Après | Réduction |
|--------|-------|-------|-----------|
| Persistence | 3421 | 3391 | -30 lignes ✅ |
| Actuators | 3391 | ~2791 | -600 (estimé) |
| Feeding | ~2791 | ~2291 | -500 (estimé) |
| Network | ~2291 | ~1591 | -700 (estimé) |
| Sleep | ~1591 | ~791 | -800 (estimé) |
| **Core final** | 3421 | **~700** | **-2721 (-80%)** |

### Objectif Final

**automatism.cpp**: 3421 → ~700 lignes (-80%)  
**Modules**: 1 fichier → 6 modules (+docs)  
**Maintenabilité**: +300%

---

## ⏱️ TEMPS RÉEL

**Démarré**: 2025-10-11 13:30  
**Module Persistence**: 13:30 - 14:30 (1h) ✅  
**Module Actuators**: 14:30 - ... (en cours)

**Pauses**: À documenter

**Total Phase 2**: X jours / 3-5 jours estimés

---

## 🎯 PROCHAINES ACTIONS

### Immédiat (Maintenant)

1. Créer `automatism_actuators.h`
2. Créer `automatism_actuators.cpp`
3. Extraire méthodes avec factorisation
4. Tester compilation
5. Commit Module 2

### Aujourd'hui (si temps)

- Compléter Module Actuators
- Démarrer Module Feeding

### Demain

- Modules Network et Sleep (les plus complexes)
- Tests fonctionnels

---

## ✅ CHECKLIST VALIDATION

### Par Module

**Module Persistence** ✅:
- [x] Header créé
- [x] Implementation créée
- [x] Méthodes déléguées dans automatism.cpp
- [x] Compilation SUCCESS
- [x] Pas de régression taille
- [x] Commit effectué

**Module Actuators** ⏳:
- [ ] Header créé
- [ ] Implementation créée
- [ ] Méthodes déléguées
- [ ] Factorisation code dupliqué
- [ ] Compilation SUCCESS
- [ ] Tests manuels
- [ ] Commit

---

## 📝 NOTES

### Découvertes pendant Refactoring

1. **Pattern async dupliqué 10 fois** (lignes 101-122, 148-171, etc.)
   - Même structure xTaskCreate
   - 13 lignes répétées
   - Factorisation OBLIGATOIRE

2. **Délai 50ms systématique** dans toutes les tâches async
   - `vTaskDelay(pdMS_TO_TICKS(50));`
   - Pourquoi 50ms ?

3. **Stack size 4096** pour toutes tâches sync
   - Possiblement surdimensionné ?
   - À mesurer

### Difficultés Rencontrées

Aucune pour l'instant ✅

---

## 🚀 ÉTAT SUIVANT

**Prochain module**: Actuators  
**Complexité**: Moyenne (factorisation requise)  
**Durée estimée**: 4-6h  
**Début**: Maintenant

**Fin estimée Phase 2**: J+3 à J+5

---

**Document**: Suivi progression Phase 2  
**Mis à jour**: Temps réel  
**Prochain update**: Après Module Actuators


