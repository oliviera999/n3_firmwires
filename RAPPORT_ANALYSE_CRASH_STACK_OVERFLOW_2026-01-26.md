# Rapport d'Analyse - Crash Stack Overflow DataQueue

**Date**: 26 janvier 2026  
**Version**: 11.156  
**Problème**: Stack canary watchpoint triggered (loopTask) lors de la rotation de la DataQueue

## 🔍 Analyse du Crash

### Symptômes observés

```
13:55:33.665 > [DataQueue] ⚠️ Queue pleine, rotation...
13:55:33.673 > Guru Meditation Error: Core  1 panic'ed (Unhandled debug exception).
13:55:33.678 > Debug exception reason: Stack canary watchpoint triggered (loopTask)
```

Le crash se produit **immédiatement après** le message "Queue pleine, rotation...", ce qui indique que la fonction `rotateIfNeeded()` provoque un débordement de pile.

### Contexte d'exécution

1. **Tâche concernée**: `loopTask` (la fonction `loop()` d'Arduino)
2. **Stack configurée**: 16384 bytes (16 KB) via `CONFIG_ARDUINO_LOOP_STACK_SIZE=16384`
3. **Point de crash**: Dans `DataQueue::rotateIfNeeded()` appelé depuis `DataQueue::push()`

### Chaîne d'appels problématique

```
loop() 
  → (via automatism_sync.cpp:329)
  → DataQueue::push()
    → isFull() 
      → countEntries() [Buffer 512 bytes]
    → rotateIfNeeded()
      → countEntries() [Buffer 512 bytes]
      → File operations (LittleFS)
        → Multiple buffers (lineBuf 512 bytes)
    → countEntries() [Buffer 512 bytes] (ligne 73)
```

### Problèmes identifiés

#### 1. **Appels multiples à `countEntries()`**
- `push()` appelle `isFull()` qui appelle `countEntries()` (buffer 512 bytes)
- `rotateIfNeeded()` appelle aussi `countEntries()` (buffer 512 bytes)
- `push()` appelle encore `countEntries()` après la rotation (buffer 512 bytes)
- **Total**: 3 buffers de 512 bytes = 1536 bytes minimum sur la stack

#### 2. **Opérations de fichier coûteuses en stack**
- `rotateIfNeeded()` ouvre deux fichiers simultanément (`src` et `tmp`)
- Lit et écrit ligne par ligne avec buffer de 512 bytes
- Les opérations LittleFS peuvent utiliser de la stack supplémentaire

#### 3. **Pas de réutilisation du count**
- Le count calculé dans `isFull()` n'est pas réutilisé
- Le count calculé dans `rotateIfNeeded()` n'est pas réutilisé
- Chaque appel à `countEntries()` parcourt tout le fichier

## 🔧 Solutions proposées

### Solution 1: Optimisation immédiate (Recommandée)

**Objectif**: Réduire l'utilisation de stack en évitant les appels redondants à `countEntries()`

**Modifications**:
1. Passer le count en paramètre à `rotateIfNeeded()` pour éviter de le recalculer
2. Réutiliser le count calculé dans `push()` au lieu de le recalculer
3. Réduire la taille du buffer si possible (512 → 256 bytes)

**Impact**: Réduction de ~50% de l'utilisation de stack dans `push()`

### Solution 2: Déplacer la rotation dans une tâche dédiée

**Objectif**: Éviter complètement l'utilisation de stack dans `loop()`

**Modifications**:
- Créer une tâche FreeRTOS dédiée pour la rotation
- Utiliser une queue pour signaler la nécessité de rotation
- `push()` retourne immédiatement après avoir ajouté l'entrée

**Impact**: Élimination complète du problème de stack dans `loop()`, mais plus complexe

### Solution 3: Rotation asynchrone différée

**Objectif**: Ne pas bloquer `push()` avec la rotation

**Modifications**:
- Marquer la queue comme "nécessite rotation" au lieu de la faire immédiatement
- Effectuer la rotation lors du prochain `pop()` ou dans une tâche périodique

**Impact**: `push()` reste rapide, mais la queue peut temporairement dépasser la limite

## ✅ Correction appliquée

**Solution choisie**: Solution 1 (Optimisation immédiate)

### Modifications dans `data_queue.cpp`:

1. **Modifier `rotateIfNeeded()` pour accepter un paramètre `currentSize`**:
   ```cpp
   void rotateIfNeeded(uint16_t currentSize);
   ```

2. **Modifier `push()` pour réutiliser le count**:
   ```cpp
   uint16_t entryCount = countEntries();
   if (entryCount >= _maxEntries) {
       rotateIfNeeded(entryCount);
   }
   // Réutiliser entryCount au lieu de recalculer
   ```

3. **Réduire la taille du buffer de 512 à 256 bytes** (suffisant pour JSON lines)

### Avantages:
- ✅ Réduction immédiate de l'utilisation de stack
- ✅ Pas de changement d'architecture
- ✅ Performance améliorée (moins de lectures de fichier)
- ✅ Facile à tester et valider

## 📊 Estimation de l'impact

**Avant**:
- 3 buffers de 512 bytes = 1536 bytes
- Stack utilisée estimée: ~2000-3000 bytes dans `push()`

**Après**:
- 1 buffer de 256 bytes = 256 bytes
- Stack utilisée estimée: ~1000-1500 bytes dans `push()`
- **Réduction**: ~50% d'utilisation de stack

## 🧪 Tests recommandés

1. **Test de stress**: Remplir la queue jusqu'à 50 entrées puis ajouter une nouvelle
2. **Test de stack**: Vérifier le HWM (High Water Mark) de la stack de `loop()` après rotation
3. **Test de régression**: Vérifier que la rotation fonctionne correctement

## 📝 Notes supplémentaires

- La stack de `loop()` est déjà augmentée à 16 KB, mais le problème persiste
- Cela suggère que d'autres opérations dans `loop()` consomment aussi de la stack
- Une analyse plus approfondie de l'utilisation de stack dans `loop()` pourrait être nécessaire
