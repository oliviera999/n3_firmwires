# 📊 Rapport Diagnostic Fragmentation Mémoire

**Date:** 2026-01-26 10:30-10:33  
**Fichier log:** `monitor_wroom_test_2026-01-26_10-30-11.log`  
**Durée:** ~3 minutes

---

## 🔍 Snapshots Capturés

### Séquence d'Initialisation

1. **`boot`** (non visible dans le log, probablement trop tôt)
   - Snapshot initial après Serial.begin

2. **`before_queues`** (17921 ms)
   - Heap libre: 116 316 bytes
   - Plus grand bloc: 90 100 bytes
   - Fragmentation: **22%**
   - Allocated: 148 056 bytes

3. **`after_sensor_queue`** (17942 ms)
   - Heap libre: 115 480 bytes (-836 bytes)
   - Plus grand bloc: 86 004 bytes (-4096 bytes)
   - Fragmentation: **25%** (+3%)
   - Allocated: 148 892 bytes (+836 bytes)
   - **Impact:** Queue capteurs (35 slots × 24 bytes = ~840 bytes)

4. **`after_net_queue`** (17984 ms)
   - Heap libre: 115 348 bytes (-132 bytes)
   - Plus grand bloc: 86 004 bytes (identique)
   - Fragmentation: **25%** (stable)
   - Allocated: 149 024 bytes (+132 bytes)
   - **Impact:** Queue réseau (6 slots × 4 bytes = ~24 bytes, négligeable)

5. **`after_tasks`** (18133 ms)
   - Heap libre: 76 276 bytes (-39 072 bytes)
   - Plus grand bloc: 49 140 bytes (-36 864 bytes)
   - Fragmentation: **35%** (+10%)
   - Allocated: 188 096 bytes (+39 072 bytes)
   - **Impact:** Création des tâches FreeRTOS (stacks)

6. **`after_setup`** (18280 ms)
   - Heap libre: 76 012 bytes (-264 bytes)
   - Plus grand bloc: 49 140 bytes (identique)
   - Fragmentation: **35%** (stable)
   - Allocated: 188 360 bytes (+264 bytes)
   - **Impact:** Finalisation setup (négligeable)

### Opérations TLS

7. **`before_tls_get`** (18229 ms)
   - Heap libre: 76 012 bytes
   - Plus grand bloc: 49 140 bytes
   - Fragmentation: **35%**

8. **`after_tls_get`** (20728 ms)
   - Heap libre: 74 704 bytes (-1308 bytes)
   - Plus grand bloc: 34 804 bytes (-14 336 bytes)
   - Fragmentation: **53%** (+18%)
   - Allocated: 189 668 bytes (+1308 bytes)
   - **Impact:** Opération TLS GET (~2500 ms)

9. **`after_tls_reset`** (20716 ms)
   - Heap libre: 74 704 bytes
   - Plus grand bloc: 34 804 bytes
   - Fragmentation: **53%**
   - **Note:** La libération TLS ne réduit pas la fragmentation

---

## 🎯 Analyse des Points Critiques

### Point Critique 1: Création des Tâches (after_tasks)

**Avant:** `after_net_queue` (17984 ms)
- Fragmentation: 25%
- Plus grand bloc: 86 004 bytes

**Après:** `after_tasks` (18133 ms)
- Fragmentation: **35%** (+10%)
- Plus grand bloc: **49 140 bytes** (-36 864 bytes)
- Heap libre: -39 072 bytes

**Cause:** Allocation des stacks des tâches FreeRTOS
- `sensorTask`: ~8192 bytes stack
- `netTask`: ~12288 bytes stack
- `automationTask`: ~8192 bytes stack
- `displayTask`: ~4096 bytes stack
- Total: ~32-40 KB alloués

**Impact:** Réduction du plus grand bloc de 86 KB à 49 KB

### Point Critique 2: Opération TLS GET

**Avant:** `before_tls_get` (18229 ms)
- Fragmentation: 35%
- Plus grand bloc: 49 140 bytes

**Après:** `after_tls_get` (20728 ms)
- Fragmentation: **53%** (+18%)
- Plus grand bloc: **34 804 bytes** (-14 336 bytes)

**Cause:** Allocation TLS (~42-46 KB) qui fragmente le heap
- TLS alloue un gros bloc contigu
- Après libération, le bloc n'est pas complètement défragmenté
- Le plus grand bloc disponible diminue

**Impact:** Fragmentation passe de 35% à 53%, plus grand bloc passe de 49 KB à 35 KB

---

## 📊 Évolution de la Fragmentation

| Snapshot | Temps (ms) | Fragmentation | Plus grand bloc | Heap libre | Allocated |
|----------|------------|---------------|-----------------|------------|-----------|
| before_queues | 17921 | 22% | 90 100 | 116 316 | 148 056 |
| after_sensor_queue | 17942 | 25% | 86 004 | 115 480 | 148 892 |
| after_net_queue | 17984 | 25% | 86 004 | 115 348 | 149 024 |
| after_tasks | 18133 | **35%** | **49 140** | 76 276 | 188 096 |
| after_setup | 18280 | 35% | 49 140 | 76 012 | 188 360 |
| before_tls_get | 18229 | 35% | 49 140 | 76 012 | 188 360 |
| after_tls_get | 20728 | **53%** | **34 804** | 74 704 | 189 668 |
| after_tls_reset | 20716 | 53% | 34 804 | 74 704 | 189 668 |

---

## 🔬 Conclusions

### Cause Principale de Fragmentation

**1. Allocation des Stacks des Tâches (Point Critique 1)**

Lors de la création des tâches FreeRTOS (`after_tasks`), la fragmentation passe de 25% à 35% et le plus grand bloc passe de 86 KB à 49 KB.

**Cause:** Les stacks des tâches sont alloués sur le heap et fragmentent la mémoire.

**Solution possible:**
- Utiliser des allocations statiques pour les stacks si possible
- Réduire la taille des stacks si possible
- Allouer les stacks en premier (avant autres allocations)

**2. Opération TLS (Point Critique 2)**

Lors de la première opération TLS GET, la fragmentation passe de 35% à 53% et le plus grand bloc passe de 49 KB à 35 KB.

**Cause:** L'allocation TLS (~42-46 KB) puis sa libération laisse des "trous" dans le heap qui ne sont pas défragmentés automatiquement.

**Solution possible:**
- Réorganiser les allocations pour allouer TLS en premier
- Implémenter une défragmentation manuelle après TLS
- Réduire la taille des allocations TLS si possible

### Fragmentation Stabilisée

Après la première opération TLS, la fragmentation se stabilise à **53%** et le plus grand bloc à **34 804 bytes**. Cette fragmentation persiste même après libération TLS.

**Conclusion:** La fragmentation est causée par :
1. L'allocation des stacks des tâches (réduit le plus grand bloc de 86 KB à 49 KB)
2. L'opération TLS (réduit le plus grand bloc de 49 KB à 35 KB)
3. L'absence de défragmentation automatique (fragmentation reste à 53%)

---

## 📋 Actions Recommandées

### Action 1: Réduire la Taille des Stacks

Vérifier et réduire si possible :
- `TaskConfig::SENSOR_TASK_STACK_SIZE`
- `TaskConfig::NET_TASK_STACK_SIZE`
- `TaskConfig::AUTOMATION_TASK_STACK_SIZE`
- `TaskConfig::DISPLAY_TASK_STACK_SIZE`

### Action 2: Réorganiser l'Ordre des Allocations

Allouer les gros blocs en premier :
1. Stacks des tâches (déjà fait au démarrage)
2. Queues FreeRTOS
3. Buffers WebSocket
4. Autres allocations

### Action 3: Optimiser la Libération TLS

Améliorer `resetTLSClient()` pour :
- Forcer une défragmentation si possible
- Libérer complètement la mémoire TLS
- Réduire le délai entre allocations TLS

### Action 4: Considérer une Défragmentation Manuelle

Si possible, implémenter une défragmentation manuelle après opérations TLS importantes.

---

## 📊 Statistiques

- **Snapshots capturés:** 9
- **Comparaisons effectuées:** 6
- **Fragmentation initiale:** 22%
- **Fragmentation finale:** 53%
- **Plus grand bloc initial:** 90 100 bytes
- **Plus grand bloc final:** 34 804 bytes
- **Réduction:** 55 296 bytes (61%)

---

**Rapport généré le:** 2026-01-26  
**Système de diagnostic:** Opérationnel  
**Fichier log analysé:** `monitor_wroom_test_2026-01-26_10-30-11.log`
