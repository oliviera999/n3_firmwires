# Analyse de cohérence NVS - FFP5CS v11.104

**Date**: 2025-01-XX  
**Version**: 11.104  
**Objectif**: Vérifier la cohérence de l'utilisation de NVS dans tout le projet

---

## 📋 Résumé exécutif

Le projet a migré vers un système NVS centralisé (`g_nvsManager`) depuis la v11.80, mais **plusieurs modules utilisent encore directement `Preferences`** au lieu du gestionnaire centralisé. Cette incohérence peut causer :

- ❌ Fragmentation NVS (multiples namespaces au lieu de 6 consolidés)
- ❌ Perte des bénéfices de compression JSON
- ❌ Absence de cache et flush différé
- ❌ Risques de corruption non gérés
- ❌ Migration incomplète depuis l'ancien système

---

## 🔍 État actuel de la migration

### ✅ Modules migrés (utilisent `g_nvsManager`)

| Module | Namespace | Clés | Statut |
|--------|-----------|------|--------|
| `config.cpp` | `CONFIG` | `bouffe_*`, `remote_json` | ✅ Migré |
| `config.cpp` | `SYSTEM` | `ota_update_flag`, `net_*` | ✅ Migré |
| `mailer.cpp` | `CONFIG` | `remote_json` | ✅ Migré (v11.104) |

### ❌ Modules NON migrés (utilisent `Preferences` directement)

| Module | Namespace actuel | Namespace cible | Priorité | Impact |
|--------|------------------|-----------------|----------|--------|
| `automatism_persistence.cpp` | `actSnap`, `actState`, `pendingSync` | `STATE` | 🔴 **CRITIQUE** | État actionneurs |
| `mailer.cpp` | `bouffe`, `ota`, `rtc`, `diagnostics` | `CONFIG`, `SYSTEM`, `TIME`, `LOGS` | 🟡 **MOYEN** | Affichage emails |
| `diagnostics.cpp` | `diagnostics` | `LOGS` | 🟡 **MOYEN** | Statistiques système |
| `power.cpp` | `rtc` | `TIME` | 🟡 **MOYEN** | Gestion temps |
| `gpio_parser.cpp` | `gpio` | `CONFIG` | 🟡 **MOYEN** | Configuration GPIO |
| `ota_manager.cpp` | `ota` | `SYSTEM` | 🟡 **MOYEN** | Mise à jour OTA |
| `sensors.cpp` | `waterTemp` | `SENSORS` | 🟢 **FAIBLE** | Température eau |
| `app.cpp` | `digest` | `SENSORS` | 🟢 **FAIBLE** | Digest événements |
| `time_drift_monitor.cpp` | `timeDrift` | `TIME` | 🟡 **MOYEN** | Dérive temporelle |
| `automatism_alert_controller.cpp` | `alerts` | `LOGS` | 🟢 **FAIBLE** | Alertes |
| `automatism_network.cpp` | `reset`, `cmdLog` | `SYSTEM`, `LOGS` | 🟢 **FAIBLE** | Reset/Logs réseau |

---

## 🔴 Problèmes identifiés

### 1. **automatism_persistence.cpp** - CRITIQUE

**Problème**: Utilise directement `Preferences` pour les états critiques des actionneurs.

**Impact**:
- État des actionneurs non migré vers le système centralisé
- Pas de cache ni flush différé
- Risque de perte d'état en cas de corruption

**Code actuel**:
```cpp
// ❌ Ancien système
Preferences prefs;
prefs.begin("actSnap", false);
prefs.putBool("aqua", pumpAquaWasOn);
```

**Solution recommandée**:
```cpp
// ✅ Nouveau système
g_nvsManager.saveBool(NVS_NAMESPACES::STATE, "snap_aqua", pumpAquaWasOn);
g_nvsManager.saveBool(NVS_NAMESPACES::STATE, "snap_heater", heaterWasOn);
g_nvsManager.saveBool(NVS_NAMESPACES::STATE, "snap_light", lightWasOn);
```

**Namespaces à migrer**:
- `actSnap` → `STATE` (préfixe `snap_`)
- `actState` → `STATE` (préfixe `state_`)
- `pendingSync` → `STATE` (préfixe `sync_`)

---

### 2. **mailer.cpp** - MOYEN

**Problème**: Lit encore depuis les anciens namespaces pour l'affichage dans les emails.

**Impact**:
- Les emails peuvent afficher des données obsolètes ou vides
- Incohérence avec le reste du système

**Namespaces à migrer**:
- `bouffe` → `CONFIG` (préfixe `bouffe_`)
- `ota` → `SYSTEM` (préfixe `ota_`)
- `rtc` → `TIME` (préfixe `rtc_`)
- `diagnostics` → `LOGS` (préfixe `diag_`)

**Note**: `remoteVars` a été corrigé en v11.104 ✅

---

### 3. **diagnostics.cpp** - MOYEN

**Problème**: Utilise `Preferences` directement au lieu de `g_nvsManager`.

**Impact**:
- Statistiques système non dans le namespace consolidé
- Pas de bénéfices du cache

**Code actuel**:
```cpp
// ❌ Ancien système
_prefs.begin("diagnostics", false);
_prefs.putUInt("rebootCnt", _stats.rebootCount);
```

**Solution recommandée**:
```cpp
// ✅ Nouveau système
g_nvsManager.saveInt(NVS_NAMESPACES::LOGS, "diag_rebootCnt", _stats.rebootCount);
g_nvsManager.saveInt(NVS_NAMESPACES::LOGS, "diag_minHeap", _stats.minFreeHeap);
```

---

### 4. **power.cpp** - MOYEN

**Problème**: Utilise `Preferences` pour le namespace `rtc`.

**Impact**:
- Temps RTC non dans le namespace consolidé `TIME`
- Migration déjà prévue dans les règles de migration

**Solution**: Utiliser `g_nvsManager` avec `NVS_NAMESPACES::TIME` et préfixe `rtc_`.

---

### 5. **gpio_parser.cpp** - MOYEN

**Problème**: Utilise `Preferences` pour le namespace `gpio`.

**Impact**:
- Configuration GPIO non dans le namespace consolidé `CONFIG`
- Migration déjà prévue dans les règles de migration

**Solution**: Utiliser `g_nvsManager` avec `NVS_NAMESPACES::CONFIG` et préfixe `gpio_`.

---

### 6. **time_drift_monitor.cpp** - MOYEN

**Problème**: Utilise `Preferences` directement (membre de classe).

**Impact**:
- Dérive temporelle non dans le namespace consolidé `TIME`
- Migration déjà prévue dans les règles de migration

**Note**: Cette classe encapsule `Preferences` comme membre, ce qui est acceptable, mais devrait utiliser `g_nvsManager` pour la cohérence.

---

## 📊 Mapping des migrations

### Règles de migration existantes (déjà définies dans `nvs_manager.cpp`)

```cpp
const MigrationRule rules[] = {
    {"bouffe",      NVS_NAMESPACES::CONFIG,  "bouffe_"},
    {"ota",         NVS_NAMESPACES::SYSTEM,  "ota_"},
    {"remoteVars",  NVS_NAMESPACES::CONFIG,  "remote_"},
    {"rtc",         NVS_NAMESPACES::TIME,    "rtc_"},
    {"diagnostics", NVS_NAMESPACES::LOGS,    "diag_"},
    {"alerts",      NVS_NAMESPACES::LOGS,    "alert_"},
    {"timeDrift",   NVS_NAMESPACES::TIME,    "drift_"},
    {"gpio",        NVS_NAMESPACES::CONFIG,  "gpio_"},
    {"actSnap",     NVS_NAMESPACES::STATE,   "snap_"},
    {"actState",    NVS_NAMESPACES::STATE,   "state_"},
    {"pendingSync", NVS_NAMESPACES::STATE,   "sync_"},
    {"waterTemp",   NVS_NAMESPACES::SENSORS, "temp_"},
    {"digest",      NVS_NAMESPACES::SENSORS, "digest_"},
    {"cmdLog",      NVS_NAMESPACES::LOGS,    "cmd_"},
    {"reset",       NVS_NAMESPACES::SYSTEM,  "reset_"},
};
```

**✅ Les règles de migration sont déjà définies**, mais **les modules n'utilisent pas encore `g_nvsManager`**.

---

## 🎯 Plan de migration recommandé

### Phase 1: Modules critiques (Priorité 🔴)

1. **automatism_persistence.cpp**
   - Migrer `actSnap`, `actState`, `pendingSync` vers `NVS_NAMESPACES::STATE`
   - Utiliser les préfixes `snap_`, `state_`, `sync_`
   - **Impact**: État des actionneurs - CRITIQUE pour le fonctionnement

### Phase 2: Modules moyens (Priorité 🟡)

2. **diagnostics.cpp**
   - Migrer vers `NVS_NAMESPACES::LOGS` avec préfixe `diag_`
   - **Impact**: Statistiques système

3. **power.cpp**
   - Migrer vers `NVS_NAMESPACES::TIME` avec préfixe `rtc_`
   - **Impact**: Gestion du temps RTC

4. **gpio_parser.cpp**
   - Migrer vers `NVS_NAMESPACES::CONFIG` avec préfixe `gpio_`
   - **Impact**: Configuration GPIO

5. **time_drift_monitor.cpp**
   - Migrer vers `NVS_NAMESPACES::TIME` avec préfixe `drift_`
   - **Impact**: Dérive temporelle

6. **ota_manager.cpp**
   - Migrer vers `NVS_NAMESPACES::SYSTEM` avec préfixe `ota_`
   - **Impact**: Mise à jour OTA

7. **mailer.cpp** (compléter la migration)
   - Migrer `bouffe`, `ota`, `rtc`, `diagnostics` vers les namespaces consolidés
   - **Impact**: Affichage dans les emails

### Phase 3: Modules faibles (Priorité 🟢)

8. **sensors.cpp**
   - Migrer `waterTemp` vers `NVS_NAMESPACES::SENSORS` avec préfixe `temp_`
   - **Impact**: Température eau (fallback)

9. **app.cpp**
   - Migrer `digest` vers `NVS_NAMESPACES::SENSORS` avec préfixe `digest_`
   - **Impact**: Digest événements

10. **automatism_alert_controller.cpp**
    - Migrer `alerts` vers `NVS_NAMESPACES::LOGS` avec préfixe `alert_`
    - **Impact**: Alertes système

11. **automatism_network.cpp**
    - Migrer `reset`, `cmdLog` vers `NVS_NAMESPACES::SYSTEM` et `LOGS`
    - **Impact**: Reset/Logs réseau

---

## ⚠️ Points d'attention

### 1. **Compatibilité ascendante**

La fonction `migrateFromOldSystem()` dans `nvs_manager.cpp` devrait automatiquement migrer les données, mais il faut s'assurer que :
- ✅ Les modules migrés utilisent les nouveaux namespaces
- ✅ Les modules non migrés peuvent encore lire depuis les anciens namespaces
- ✅ La migration se fait une seule fois au boot

### 2. **Classes encapsulant Preferences**

Certaines classes (`PowerManager`, `Diagnostics`, `TimeDriftMonitor`) utilisent `Preferences` comme membre privé. Options :

**Option A**: Remplacer par `g_nvsManager` (recommandé)
- ✅ Cohérence avec le reste du système
- ✅ Bénéfices du cache et flush différé
- ❌ Nécessite refactoring

**Option B**: Garder `Preferences` mais utiliser les namespaces consolidés
- ✅ Moins de refactoring
- ❌ Pas de bénéfices du cache
- ❌ Incohérence avec le reste du système

**Recommandation**: Option A pour la cohérence à long terme.

### 3. **Tests après migration**

Pour chaque module migré :
1. ✅ Vérifier que les données sont lues correctement
2. ✅ Vérifier que la migration automatique fonctionne
3. ✅ Vérifier que les anciennes données sont nettoyées
4. ✅ Tester la persistance après reboot
5. ✅ Vérifier l'utilisation mémoire NVS

---

## 📝 Recommandations spécifiques

### 1. **automatism_persistence.cpp** - URGENT

```cpp
// ❌ AVANT
void AutomatismPersistence::saveActuatorSnapshot(...) {
  Preferences prefs;
  prefs.begin("actSnap", false);
  prefs.putBool("aqua", pumpAquaWasOn);
  prefs.end();
}

// ✅ APRÈS
void AutomatismPersistence::saveActuatorSnapshot(...) {
  g_nvsManager.saveBool(NVS_NAMESPACES::STATE, "snap_aqua", pumpAquaWasOn);
  g_nvsManager.saveBool(NVS_NAMESPACES::STATE, "snap_heater", heaterWasOn);
  g_nvsManager.saveBool(NVS_NAMESPACES::STATE, "snap_light", lightWasOn);
  g_nvsManager.saveBool(NVS_NAMESPACES::STATE, "snap_pending", true);
}
```

### 2. **mailer.cpp** - Compléter la migration

```cpp
// ❌ AVANT
prefs.begin("bouffe", true);
footer += "- bouffeMatinOk: "; 
footer += prefs.getBool("bouffeMatinOk", false) ? "true" : "false";

// ✅ APRÈS
bool bouffeMatinOk;
g_nvsManager.loadBool(NVS_NAMESPACES::CONFIG, "bouffe_matin", bouffeMatinOk, false);
footer += "- bouffeMatinOk: "; 
footer += bouffeMatinOk ? "true" : "false";
```

### 3. **diagnostics.cpp** - Refactoring recommandé

```cpp
// ❌ AVANT (membre Preferences)
_prefs.begin("diagnostics", false);
_prefs.putUInt("rebootCnt", _stats.rebootCount);

// ✅ APRÈS (g_nvsManager)
g_nvsManager.saveInt(NVS_NAMESPACES::LOGS, "diag_rebootCnt", _stats.rebootCount);
```

**Note**: Nécessite de retirer le membre `Preferences _prefs` de la classe.

---

## 🔧 Actions immédiates recommandées

### Priorité 1 (Cette semaine)

1. ✅ **Corriger mailer.cpp** - `remoteVars` (FAIT en v11.104)
2. 🔴 **Migrer automatism_persistence.cpp** - CRITIQUE pour l'état des actionneurs
3. 🟡 **Migrer diagnostics.cpp** - Statistiques système

### Priorité 2 (Semaine prochaine)

4. 🟡 **Migrer power.cpp** - Gestion temps RTC
5. 🟡 **Migrer gpio_parser.cpp** - Configuration GPIO
6. 🟡 **Compléter mailer.cpp** - Tous les namespaces restants

### Priorité 3 (Prochain sprint)

7. 🟡 **Migrer time_drift_monitor.cpp** - Dérive temporelle
8. 🟡 **Migrer ota_manager.cpp** - Mise à jour OTA
9. 🟢 **Migrer sensors.cpp** - Température eau
10. 🟢 **Migrer app.cpp** - Digest événements
11. 🟢 **Migrer automatism_alert_controller.cpp** - Alertes
12. 🟢 **Migrer automatism_network.cpp** - Reset/Logs

---

## 📈 Métriques de succès

Après migration complète :
- ✅ **0** usage direct de `Preferences` (sauf dans `nvs_manager.cpp`)
- ✅ **6** namespaces consolidés au lieu de 14+
- ✅ **100%** des modules utilisent `g_nvsManager`
- ✅ **Réduction fragmentation NVS** (mesurable via `getUsageStats()`)
- ✅ **Bénéfices cache** (flush différé activé)

---

## 🔍 Vérification post-migration

### Checklist pour chaque module migré

- [ ] Code utilise `g_nvsManager` au lieu de `Preferences`
- [ ] Namespace consolidé correct (`CONFIG`, `SYSTEM`, `TIME`, `STATE`, `LOGS`, `SENSORS`)
- [ ] Préfixe de clé correct (selon règles de migration)
- [ ] Migration automatique testée (anciennes données migrées)
- [ ] Tests de persistance après reboot
- [ ] Vérification mémoire NVS (pas de fragmentation)
- [ ] Documentation mise à jour

---

## 📚 Références

- **Migration automatique**: `src/nvs_manager.cpp` ligne 834-849
- **Namespaces consolidés**: `include/nvs_manager.h` ligne 21-28
- **Règles de migration**: `src/nvs_manager.cpp` ligne 834-849
- **Documentation**: `docs/guides/README.md` (ligne 158)

---

**Conclusion**: La migration vers le système NVS centralisé est **incomplète**. Il est recommandé de migrer tous les modules restants, en commençant par les modules critiques (`automatism_persistence.cpp`).

**Version**: 11.104  
**Auteur**: Analyse automatique  
**Date**: 2025-01-XX


