# Documentation des Stacks FreeRTOS

**Date**: 26 janvier 2026  
**Version**: 11.156

## Vue d'Ensemble

Le système utilise **6 tâches FreeRTOS principales** pour gérer les différentes fonctionnalités. Chaque tâche possède son propre stack avec des caractéristiques spécifiques.

**Total stacks alloués** : ~30KB (allocation hybride : statique + dynamique)

---

## Caractéristiques des Tâches

### 1. sensorTask

**Fichier** : `src/app_tasks.cpp:135-226`

**Configuration** (`include/config.h:492-494`) :
- **Taille stack** : 3072 bytes (3 KB)
- **Priorité** : 2 (moyenne)
- **Core** : 1 (Core dédié aux capteurs)
- **Type allocation** : **Statique** (`xTaskCreateStaticPinnedToCore`)
- **Buffer statique** : `sensorTaskStack[3072]` (ligne 29)

**Fonction principale** :
- Lecture périodique des capteurs (température eau, température air, humidité, ultrasons)
- Envoi des données dans `g_sensorQueue`
- Intervalle : 500ms entre chaque lecture

**Utilisation réelle (HWM observé)** :
- **HWM au boot** : ~1864 bytes libres
- **Stack utilisée** : ~1208 bytes (39%)
- **Marge de sécurité** : 1208 bytes (excellente marge)

**Réduction appliquée** : Réduit de 4096 à 3072 bytes (v11.157) basé sur HWM

---

### 2. webTask

**Fichier** : `src/app_tasks.cpp:254-277`

**Configuration** (`include/config.h:496-499`) :
- **Taille stack** : 5120 bytes (5 KB)
- **Priorité** : 1 (basse - offline-first)
- **Core** : 0 (Core principal)
- **Type allocation** : **Dynamique** (`xTaskCreatePinnedToCore`)
- **Allocation** : Sur le heap (fragmente la mémoire)

**Fonction principale** :
- Gestion de l'interface web (serveur HTTP)
- Appels à `webServer.loop()` toutes les 100ms
- Gestion des requêtes HTTP entrantes

**Utilisation réelle (HWM observé)** :
- **HWM au boot** : ~5332 bytes libres
- **Stack utilisée** : ~-212 bytes (négatif = marge très faible)
- **Marge de sécurité** : 212 bytes (marge critique)

**Réduction appliquée** : Réduit de 6144 à 5120 bytes (v11.157) basé sur HWM

**Note** : Marge très faible - à surveiller pour stack overflow

---

### 3. automationTask

**Fichier** : `src/app_tasks.cpp:279-450`

**Configuration** (`include/config.h:501-504`) :
- **Taille stack** : 8192 bytes (8 KB)
- **Priorité** : 3 (haute - tâche critique)
- **Core** : 1 (Core dédié aux capteurs)
- **Type allocation** : **Dynamique** (`xTaskCreatePinnedToCore`)
- **Allocation** : Sur le heap (fragmente la mémoire)

**Fonction principale** :
- Logique d'automatisation principale
- Traitement des données capteurs depuis `g_sensorQueue`
- Gestion des actions (pompes, alimentation, etc.)
- Envoi heartbeat toutes les 30s
- Traitement des mails en attente
- Monitoring mémoire et fragmentation

**Utilisation réelle (HWM observé)** :
- **HWM au boot** : ~7356 bytes libres
- **HWM en fonctionnement** : ~416 bytes libres (94.9% utilisé)
- **Stack utilisée** : ~7776 bytes (95%)
- **Marge de sécurité** : 416 bytes (marge critique)

**Réduction appliquée** : **AUCUNE** - Ne pas réduire (atteint 94.9% utilisation)

**Note** : ⚠️ **CRITIQUE** - Stack très utilisée, ne pas réduire davantage

**Monitoring** : Vérification périodique toutes les 30s (PROFILE_TEST uniquement)

---

### 4. displayTask

**Fichier** : `src/app_tasks.cpp:228-252`

**Configuration** (`include/config.h:506-508`) :
- **Taille stack** : 3072 bytes (3 KB)
- **Priorité** : 1 (basse)
- **Core** : 1 (Core dédié aux capteurs)
- **Type allocation** : **Statique** (`xTaskCreateStaticPinnedToCore`)
- **Buffer statique** : `displayTaskStack[3072]` (ligne 32)

**Fonction principale** :
- Mise à jour de l'affichage OLED
- Appels à `automatism.updateDisplay()`
- Intervalle dynamique (basé sur `getRecommendedDisplayIntervalMs()`)
- Minimum : `MIN_DISPLAY_INTERVAL_MS`

**Utilisation réelle (HWM observé)** :
- **HWM au boot** : ~2328 bytes libres
- **Stack utilisée** : ~744 bytes (24%)
- **Marge de sécurité** : 744 bytes (bonne marge)

**Réduction appliquée** : Réduit de 4096 à 3072 bytes (v11.157) basé sur HWM

---

### 5. netTask

**Fichier** : `src/app_tasks.cpp:71-133`

**Configuration** (`include/config.h:513-516`) :
- **Taille stack** : 10240 bytes (10 KB)
- **Priorité** : 2 (moyenne)
- **Core** : 0 (Core principal)
- **Type allocation** : **Dynamique** (`xTaskCreatePinnedToCore`)
- **Allocation** : Sur le heap (fragmente la mémoire)

**Fonction principale** :
- **Propriétaire unique de WebClient/TLS** (point critique)
- Traitement des requêtes réseau depuis `g_netQueue`
- Types de requêtes :
  - `FetchRemoteState` : Récupération état serveur distant
  - `PostRaw` : Envoi données brutes
  - `Heartbeat` : Envoi heartbeat
  - `BootFetchRemoteState` : Fetch au boot
- Opérations TLS/HTTPS (mbedTLS)

**Utilisation réelle (HWM observé)** :
- **HWM au boot** : ~5584 bytes libres
- **Stack utilisée** : ~4656 bytes (45%)
- **Marge de sécurité** : 4656 bytes (bonne marge)

**Réduction appliquée** : Réduit de 12288 à 10240 bytes (v11.157) basé sur HWM

**Note** : Tâche critique pour TLS - nécessite stack importante pour mbedTLS

---

### 6. loopTask (Tâche principale Arduino)

**Fichier** : `src/app.cpp` - Fonction `loop()`

**Configuration** :
- **Taille stack** : Stack par défaut ESP-IDF (typiquement 8 KB)
- **Priorité** : 1 (par défaut)
- **Core** : 1 (par défaut)
- **Type allocation** : Géré par ESP-IDF

**Fonction principale** :
- Boucle principale Arduino
- Appels périodiques aux différentes fonctions
- Gestion OTA si nécessaire

**Utilisation réelle (HWM observé)** :
- **HWM au boot** : Variable (non mesuré précisément)
- **Note** : Stack par défaut généralement suffisante

---

## Tableau Récapitulatif

| Tâche | Stack (bytes) | Priorité | Core | Allocation | HWM Libre | Utilisation | Marge | Statut |
|-------|---------------|----------|------|------------|-----------|-------------|-------|--------|
| **sensorTask** | 3072 (3 KB) | 2 | 1 | Statique | 1864 | 39% | 1208 | ✅ OK |
| **webTask** | 5120 (5 KB) | 1 | 0 | Dynamique | 5332 | -4%* | 212 | ⚠️ Critique |
| **automationTask** | 8192 (8 KB) | 3 | 1 | Dynamique | 416 | 95% | 416 | 🔴 Critique |
| **displayTask** | 3072 (3 KB) | 1 | 1 | Statique | 2328 | 24% | 744 | ✅ OK |
| **netTask** | 10240 (10 KB) | 2 | 0 | Dynamique | 5584 | 45% | 4656 | ✅ OK |
| **loopTask** | ~8192 (8 KB) | 1 | 1 | ESP-IDF | ? | ? | ? | ✅ OK |

*Note : HWM négatif pour webTask indique une marge très faible (mesure au boot peut être imprécise)

---

## Analyse par Type d'Allocation

### Allocation Statique (2 tâches)

**Tâches** : `sensorTask`, `displayTask`

**Avantages** :
- ✅ Pas de fragmentation mémoire (alloué à la compilation)
- ✅ Performance légèrement meilleure (pas d'allocation dynamique)
- ✅ Garantie de disponibilité

**Inconvénients** :
- ❌ Consomme de la mémoire statique (data segment)
- ❌ Limité par la taille du data segment DRAM

**Total alloué statiquement** : 6144 bytes (6 KB)

---

### Allocation Dynamique (3 tâches)

**Tâches** : `webTask`, `automationTask`, `netTask`

**Avantages** :
- ✅ Pas de limite data segment
- ✅ Flexibilité (peut être alloué/désalloué)

**Inconvénients** :
- ❌ **Fragmente le heap** (problème principal)
- ❌ Allocation au runtime (légèrement plus lent)
- ❌ Risque d'échec d'allocation si heap insuffisant

**Total alloué dynamiquement** : 23552 bytes (~23 KB)

**Impact fragmentation** :
- Ces 23KB fragmentent le heap en créant des "îlots" persistants
- Empêchent la défragmentation après libération TLS

---

## Problèmes Identifiés

### 1. Fragmentation par Allocation Dynamique

**Problème** : Les 3 grandes tâches (webTask, automationTask, netTask) sont allouées dynamiquement sur le heap, créant des îlots persistants qui fragmentent la mémoire.

**Impact** :
- Fragmentation initiale : 25% → 35% après création des tâches
- Réduction du plus grand bloc : 86KB → 49KB

**Solution proposée** : Convertir toutes les tâches en allocation statique (Solution 2 du plan)

---

### 2. Stack Overflow Risque

**Tâches à risque** :
- **automationTask** : 95% utilisée (416 bytes libres) - ⚠️ CRITIQUE
- **webTask** : Marge très faible (212 bytes) - ⚠️ À surveiller

**Solution proposée** : Monitoring continu et alertes si utilisation > 85%

---

### 3. Marge de Sécurité Insuffisante

**Tâches concernées** :
- `automationTask` : 416 bytes (5% de marge)
- `webTask` : 212 bytes (4% de marge)

**Recommandation** : Ne pas réduire davantage ces stacks

---

## Recommandations

### Court Terme

1. **Convertir netTask en allocation statique**
   - Impact : Libère 10KB du heap
   - Réduit fragmentation de ~5%

2. **Convertir webTask en allocation statique**
   - Impact : Libère 5KB du heap
   - Réduit fragmentation de ~2%

3. **Surveiller automationTask**
   - Ne pas réduire la stack (déjà à 95%)
   - Ajouter alertes si utilisation > 90%

### Moyen Terme

1. **Convertir automationTask en allocation statique**
   - Impact : Libère 8KB du heap
   - Réduit fragmentation de ~3%
   - **Risque** : Peut dépasser limite data segment

2. **Optimiser le code d'automationTask**
   - Réduire les allocations locales
   - Utiliser des buffers statiques au lieu de variables locales

---

## Métriques de Monitoring

### Stack High Water Mark (HWM)

**Définition** : Nombre minimum de bytes libres jamais utilisés dans le stack.

**Mesure** : `uxTaskGetStackHighWaterMark(taskHandle)`

**Interprétation** :
- **HWM > 1000 bytes** : ✅ Marge confortable
- **HWM 500-1000 bytes** : ⚠️ Marge acceptable
- **HWM < 500 bytes** : 🔴 Marge critique (risque stack overflow)

### Utilisation Stack

**Calcul** : `Utilisation = (Taille - HWM) / Taille * 100%`

**Seuils** :
- **< 70%** : ✅ Utilisation normale
- **70-85%** : ⚠️ Utilisation élevée
- **> 85%** : 🔴 Utilisation critique

---

## Conclusion

**État actuel** :
- 2 tâches en allocation statique (6 KB) - ✅ Optimal
- 3 tâches en allocation dynamique (23 KB) - ❌ Fragmente le heap
- 1 tâche critique (automationTask) à 95% utilisation - ⚠️ À surveiller

**Impact fragmentation** :
- Les 23KB alloués dynamiquement contribuent significativement à la fragmentation (35% après création des tâches)

**Solution radicale proposée** :
- Convertir TOUTES les tâches en allocation statique
- Impact attendu : Fragmentation 35% → 25%
- Risque : Peut dépasser limite data segment (nécessite test)
