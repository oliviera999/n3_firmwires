# Pool netRPC et envoi régulier POST — référence technique

Ce document décrit le pool NetRequest, les correctifs appliqués (v12.40, Phase 2 v12.42) et les évolutions futures possibles pour garantir un envoi régulier des données capteurs vers le serveur.

## Contexte

Le firmware FFP5CS utilise un **pool statique** de slots pour les requêtes réseau (POST, GET, Heartbeat, OTA). En conditions de saturation (serveur lent, charge élevée), les POST peuvent échouer avec « Pool plein ».

## Architecture du pool (v12.42)

| Plateforme | Taille pool | Slots réservés | Slots partagés |
|------------|-------------|----------------|----------------|
| WROOM      | 8           | OTA: 7, Fetch: 6, POST cat3: 5, cat2: 4, cat1: 3 | 0,1,2 (Heartbeat, fallback) |
| S3         | 16          | OTA: 15, Fetch: 14, POST cat3: 13, cat2: 12, cat1: 11 | 0..10 |

- **netRequestAllocForPostCategory(cat)** : tente le slot de la catégorie (cat3→5, cat2→4, cat1→3 WROOM) puis partagés
- **netRequestAllocForFetch()** : slot 6 (WROOM) en priorité, puis partagés 0..2
- **netRequestAlloc()** (Heartbeat) : slots partagés 0..2 uniquement
- **PostCategory** : `Periodic` (données 30s), `EventAck` (ack/événements), `Replay` (rattrapage) — priorité 3 > 2 > 1

## Correctifs v12.40 (2026-03-12)

1. **Slot réservé POST** : un slot dédié garantit qu'un POST peut toujours être envoyé même en saturation.
2. **Throttle strict** : seuil basé sur `netRequestPoolPostSlotsFullThreshold()` au lieu de `poolSize - 1`, évite les tentatives inutiles quand aucun slot POST n'est disponible.

## Phase 2 implémentée (v12.42)

3 slots réservés par catégorie POST, ordre de priorité 3 > 2 > 1 :
- **Cat3 (Replay)** : rattrapage SD, replay — slot 5 (WROOM)
- **Cat2 (EventAck)** : ack commandes, événements — slot 4 (WROOM)
- **Cat1 (Periodic)** : données périodiques 30s — slot 3 (WROOM)

L'isolation évite qu'une catégorie bloque les autres en saturation. API : `PostCategory` enum dans `post_category.h`, `netPostRaw(..., PostCategory, ...)`.

## Évolutions futures possibles

### Phase 3 — si saturation persiste encore

| Solution | Description | Complexité | Fichiers |
|----------|-------------|------------|----------|
| **7. Dégradation Fetch** | Augmenter `REMOTE_FETCH_INTERVAL_MS` temporairement quand `poolUsed >= seuil` (ex. 6s → 15s) | Moyenne | `automatism_sync.cpp` |
| **3. Augmenter le pool** | WROOM 8 → 10 slots (surveiller DRAM ~3,7 KB) | Faible | `app_tasks.cpp` |
| **5. Canal POST dédié** | File FIFO `g_netPostQueue` lue avant `g_netQueue` par netTask | Élevée | `app_tasks.cpp` |

### Phase 3 — alternatives

| Solution | Description | Contrainte |
|----------|-------------|------------|
| **8. Absorption POST dans heartbeat** | Inclure données minimales dans le heartbeat, réduire fréquence POST complets | **Changement contrat** firmware-serveur |

### Solutions non retenues (redondantes)

- **4. Espacer Fetch** : remplacé par 7 (dégradation adaptative)
- **6. Joker POST** : redondant avec le slot réservé (1)

## Validation

Après toute modification :

1. Monitor 15 min sur COM : `.\monitor_5min.ps1 -Port COM8 -DurationSeconds 900 -Environment wroom-test`
2. Vérifier l'absence de `[Sync] POST échoué (pool netRPC plein)` et `[netRPC] Pool plein`
3. Analyser le log avec `analyze_log.ps1` ou `monitor_summary.py`

**Note (2026-03-12)** : l'env `wroom-test` peut provoquer un overflow DRAM au link. Si le build échoue, utiliser `wroom-beta` pour le flash (construit correctement) ; le moniteur avec `wroom-beta` n'affiche pas de logs (Serial désactivé en prod). Pour valider les correctifs pool, privilégier un build réussi de `wroom-test` si possible.

## Références

- [app_tasks.cpp](../../src/app_tasks.cpp) : pool, allocateurs, netTask
- [automatism_sync.cpp](../../src/automatism/automatism_sync.cpp) : throttle, sendFullUpdate
- [VERSION.md](../../VERSION.md) : historique des versions
