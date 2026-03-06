# 🔍 Vérification PISTE 2 - Logs de Diagnostic Ajoutés

**Date:** 2026-01-25  
**Objectif:** Vérifier si `AutomatismSync::update()` est bien appelé dans la chaîne d'appels

---

## 📝 Modifications Apportées

### 1. Log de démarrage dans `automationTask()` (`src/app_tasks.cpp`)

**Ajout:**
- Log au démarrage de la tâche pour confirmer son exécution

**Message attendu au boot:**
```
[autoTask] ⚠️ DEBUG PISTE 2: automationTask démarrée
```

**Interprétation:**
- Si ce message **n'apparaît pas** → La tâche `automationTask` ne démarre pas
- Si ce message **apparaît** → La tâche démarre correctement

### 2. Log périodique dans `automationTask()` (`src/app_tasks.cpp`)

**Ajout:**
- Log toutes les 60 secondes pour confirmer l'appel à `Automatism::update()`
- Compteur d'appels pour suivre la fréquence

**Message attendu toutes les 60 secondes:**
```
[autoTask] DEBUG PISTE 2: Appel Automatism::update() #XXX à XXXXX ms
```

**Interprétation:**
- Si ce message **n'apparaît pas** → `Automatism::update()` n'est jamais appelé
- Si ce message **apparaît** → `Automatism::update()` est appelé régulièrement

### 3. Log périodique dans `Automatism::update()` (`src/automatism.cpp`)

**Ajout:**
- Log toutes les 60 secondes pour confirmer l'exécution de la méthode
- Compteur d'appels pour suivre la fréquence

**Message attendu toutes les 60 secondes:**
```
[Auto] DEBUG PISTE 2: Automatism::update() appelé #XXX à XXXXX ms
```

**Interprétation:**
- Si ce message **n'apparaît pas** → La méthode n'est jamais exécutée
- Si ce message **apparaît** → La méthode s'exécute régulièrement

### 4. Log périodique avant `_network.update()` (`src/automatism.cpp`)

**Ajout:**
- Log toutes les 60 secondes pour confirmer l'appel à `_network.update()`

**Message attendu toutes les 60 secondes:**
```
[Auto] DEBUG PISTE 2: Appel _network.update() à XXXXX ms
```

**Interprétation:**
- Si ce message **n'apparaît pas** → `_network.update()` n'est jamais appelé
- Si ce message **apparaît** → `_network.update()` est appelé régulièrement

### 5. Log au premier appel dans `AutomatismSync::update()` (`src/automatism/automatism_sync.cpp`)

**Déjà ajouté pour PISTE 1:**
- Log au premier appel pour confirmer l'exécution

**Message attendu:**
```
[Sync] ⚠️ DEBUG PISTE 1: update() appelé pour la première fois
[Sync] DEBUG PISTE 1: WiFi=X, SendEnabled=X, RecvEnabled=X
```

**Interprétation:**
- Si ce message **n'apparaît pas** → `AutomatismSync::update()` n'est jamais appelé (**PISTE 2 CONFIRMÉE**)
- Si ce message **apparaît** → `AutomatismSync::update()` est appelé (PISTE 2 non confirmée)

---

## 🔬 Chaîne d'Appels Attendue

```
automationTask() [app_tasks.cpp:264]
  └─> g_ctx->automatism.update(readings) [app_tasks.cpp:351]
      └─> Automatism::update() [automatism.cpp:59]
          └─> _network.update(r, _acts, *this) [automatism.cpp:99]
              └─> AutomatismSync::update() [automatism_sync.cpp:55]
```

### Logs de Diagnostic par Étape

1. **Étape 1 - Tâche démarrée:**
   ```
   [autoTask] ⚠️ DEBUG PISTE 2: automationTask démarrée
   ```

2. **Étape 2 - Appel depuis automationTask:**
   ```
   [autoTask] DEBUG PISTE 2: Appel Automatism::update() #XXX à XXXXX ms
   ```

3. **Étape 3 - Exécution Automatism::update():**
   ```
   [Auto] DEBUG PISTE 2: Automatism::update() appelé #XXX à XXXXX ms
   ```

4. **Étape 4 - Appel _network.update():**
   ```
   [Auto] DEBUG PISTE 2: Appel _network.update() à XXXXX ms
   ```

5. **Étape 5 - Exécution AutomatismSync::update():**
   ```
   [Sync] ⚠️ DEBUG PISTE 1: update() appelé pour la première fois
   ```

---

## 🔬 Comment Vérifier la PISTE 2

### Étape 1: Capturer un nouveau log depuis le boot

1. **Redémarrer l'ESP32**
2. **Capturer tous les messages depuis le démarrage**
3. **Chercher les messages dans l'ordre:**

#### Messages de démarrage (au boot):
```
[autoTask] ⚠️ DEBUG PISTE 2: automationTask démarrée
```

**Interprétation:**
- Si **présent** → La tâche démarre ✅
- Si **absent** → La tâche ne démarre pas ❌

#### Messages périodiques (toutes les 60 secondes):

**Étape 2:**
```
[autoTask] DEBUG PISTE 2: Appel Automatism::update() #XXX à XXXXX ms
```

**Étape 3:**
```
[Auto] DEBUG PISTE 2: Automatism::update() appelé #XXX à XXXXX ms
```

**Étape 4:**
```
[Auto] DEBUG PISTE 2: Appel _network.update() à XXXXX ms
```

**Étape 5:**
```
[Sync] ⚠️ DEBUG PISTE 1: update() appelé pour la première fois
```

### Étape 2: Analyser la chaîne d'appels

**Vérifier que tous les messages apparaissent dans l'ordre:**

1. ✅ `[autoTask] DEBUG PISTE 2: automationTask démarrée`
2. ✅ `[autoTask] DEBUG PISTE 2: Appel Automatism::update()`
3. ✅ `[Auto] DEBUG PISTE 2: Automatism::update() appelé`
4. ✅ `[Auto] DEBUG PISTE 2: Appel _network.update()`
5. ❓ `[Sync] DEBUG PISTE 1: update() appelé`

**Si l'étape 5 est absente:**
- **PISTE 2 CONFIRMÉE** → `AutomatismSync::update()` n'est jamais appelé malgré l'appel à `_network.update()`

---

## ✅ Résultats Attendus

### Scénario 1: PISTE 2 CONFIRMÉE (méthode non appelée)

**Logs observés:**
```
[autoTask] ⚠️ DEBUG PISTE 2: automationTask démarrée
[autoTask] DEBUG PISTE 2: Appel Automatism::update() #1 à 1234 ms
[Auto] DEBUG PISTE 2: Automatism::update() appelé #1 à 1234 ms
[Auto] DEBUG PISTE 2: Appel _network.update() à 1234 ms
# MAIS AUCUN message [Sync] DEBUG PISTE 1
```

**Action:** Vérifier pourquoi `_network.update()` n'appelle pas `AutomatismSync::update()`:
- Vérifier que `_network` est bien de type `AutomatismSync`
- Vérifier que la méthode `update()` existe et est publique
- Vérifier qu'il n'y a pas d'erreur de compilation/linking

### Scénario 2: PISTE 2 NON CONFIRMÉE (méthode appelée)

**Logs observés:**
```
[autoTask] ⚠️ DEBUG PISTE 2: automationTask démarrée
[autoTask] DEBUG PISTE 2: Appel Automatism::update() #1 à 1234 ms
[Auto] DEBUG PISTE 2: Automatism::update() appelé #1 à 1234 ms
[Auto] DEBUG PISTE 2: Appel _network.update() à 1234 ms
[Sync] ⚠️ DEBUG PISTE 1: update() appelé pour la première fois
[Sync] DEBUG PISTE 1: WiFi=3, SendEnabled=1, RecvEnabled=1
```

**Action:** La chaîne d'appels est complète. Vérifier PISTE 1 ou autres conditions.

### Scénario 3: Tâche ne démarre pas

**Logs observés:**
```
# AUCUN message [autoTask] DEBUG PISTE 2: automationTask démarrée
```

**Action:** Vérifier:
- Que la tâche est bien créée dans `AppTasks::begin()`
- Que la priorité de la tâche est correcte
- Qu'il n'y a pas d'erreur de stack overflow au démarrage

---

## 📊 Checklist de Vérification

- [ ] Nouveau log capturé depuis le boot complet
- [ ] Message `[autoTask] DEBUG PISTE 2: automationTask démarrée` présent
- [ ] Messages `[autoTask] DEBUG PISTE 2: Appel Automatism::update()` présents toutes les 60s
- [ ] Messages `[Auto] DEBUG PISTE 2: Automatism::update() appelé` présents toutes les 60s
- [ ] Messages `[Auto] DEBUG PISTE 2: Appel _network.update()` présents toutes les 60s
- [ ] Message `[Sync] DEBUG PISTE 1: update() appelé` présent (au moins une fois)

---

## 🎯 Prochaines Étapes

1. **Compiler et flasher** avec les nouveaux logs
2. **Capturer un nouveau log** depuis le boot complet
3. **Analyser les messages** selon ce document
4. **Confirmer ou infirmer** la PISTE 2
5. **Agir en conséquence** :
   - Si PISTE 2 confirmée → Vérifier pourquoi `_network.update()` n'appelle pas `AutomatismSync::update()`
   - Si PISTE 2 non confirmée → Vérifier PISTE 1 ou autres conditions

---

## 🔍 Vérifications Supplémentaires

### Si PISTE 2 confirmée, vérifier:

1. **Type de `_network` dans `automatism.h`:**
   ```cpp
   AutomatismSync _network;  // Doit être de type AutomatismSync
   ```

2. **Méthode `update()` dans `AutomatismSync`:**
   ```cpp
   void update(const SensorReadings& readings, SystemActuators& acts, Automatism& core);
   ```

3. **Initialisation de `_network` dans le constructeur:**
   ```cpp
   _network(web, config)  // Doit être initialisé correctement
   ```

4. **Erreurs de compilation/linking:**
   - Vérifier qu'il n'y a pas d'erreurs de linking
   - Vérifier que toutes les dépendances sont résolues

---

**Modifications effectuées le:** 2026-01-25  
**Fichiers modifiés:**
- `src/app_tasks.cpp`
- `src/automatism.cpp`
- `src/automatism/automatism_sync.cpp` (déjà modifié pour PISTE 1)
