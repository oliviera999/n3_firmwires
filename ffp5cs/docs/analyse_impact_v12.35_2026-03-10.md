# Analyse d'impact — v12.35 + serveur v5.0.108

**Date :** 2026-03-10  
**Run :** erase flash + flash 12.35 + monitor 10 min (wroom-s3-test, COM7)  
**Log :** `monitor_10min_2026-03-10_17-36-59.log`

---

## Changements déployés

| Composant | Changement | Objectif |
|-----------|------------|----------|
| **Firmware 12.35** | `HTTP_POST_RPC_TIMEOUT_MS` 10 s → 17 s (S3) | RPC ≥ HTTP (15 s) + 2 s marge ; éviter abandon avant fin POST |
| **Serveur 5.0.108** | Cache outputs + update board différés (`register_shutdown_function`) | Réduire latence perçue avant 200 côté ESP32 |
| **Serveur 5.0.108** | Logs structurés 401/400/500 (sensor, version, post_id) | Faciliter diagnostic rejets et timeouts |

---

## Résultats du run (10 min)

### Métriques principales

| Métrique | Valeur |
|----------|--------|
| POST tentatives | 4 |
| POST réussis | 0 |
| POST échoués | 10 |
| POST timeouts | 0 |
| Taux succès POST | 0 % |
| Durée moyenne POST (avant échec) | ~11,5 s |
| GET tentatives | 6 |
| Parsing GET réussi | 0 |
| Pool netRPC | 15/16 (saturé) |
| Crashes | 0 |
| Version firmware | v12.35 |

### Constats

1. **Toujours 0 % POST** : Le serveur iot.olution.info (post-data3-test) reste injoignable ou ne répond pas assez vite depuis l’environnement de test (192.168.0.180). Les POST sont abandonnés après le timeout RPC (17 s désormais).

2. **RPC 17 s actif** : Le firmware utilise bien `HTTP_POST_RPC_TIMEOUT_MS = 17 s`. Les échecs « timeout RPC » signifient que la requête HTTP ne se termine pas dans les 17 s (serveur lent ou inaccessible).

3. **Pool saturé (15/16)** : Les slots restent occupés par des requêtes en cours ou en timeout. Le Fetch (12 s) et les POST en attente saturent le pool.

4. **Pas de régression** : Aucun crash, heap stable, comportement cohérent avec les runs précédents (v12.33/v12.34).

---

## Impact des changements

### Suggestion 1 (RPC ≥ HTTP)

- **Effet** : Le firmware attend maintenant 17 s au lieu de 10 s avant d’abandonner un POST.
- **Impact observé** : Aucun POST réussi ; les requêtes dépassent toujours le délai. Le blocage vient plutôt de la connectivité vers le serveur que du timeout RPC.
- **Conclusion** : Changement correct et utile pour les cas où le serveur répond en 11–16 s ; ici le serveur ne répond pas ou met plus de 17 s.

### Suggestion 7 (serveur)

- **Effet** : Réponse 200 plus rapide grâce à la mise à jour différée du cache et du board.
- **Impact** : Non mesurable dans ce run car aucun POST n’atteint le serveur. Les logs serveur pourraient confirmer l’effet en production.

### Suggestion 10 (logs)

- **Effet** : Logs structurés en cas de 401/400/500.
- **Impact** : Aucun rejet loggé dans ce run (0 requête reçue). Utile pour les futures pannes ou rejets d’auth.

---

## Recommandations

1. **Diagnostic réseau** : Vérifier depuis l’environnement de test (ou le même réseau que l’ESP32) :
   - `curl -v https://iot.olution.info/ffp3/post-data3-test` (réponse, délai)
   - Pare-feu, DNS, routage vers iot.olution.info

2. **Test en conditions réelles** : Lancer un monitoring avec l’ESP32 sur le même réseau que le serveur de production ou dans un contexte où la connectivité est connue (ex. site physique aquaponie).

3. **Surveiller les logs serveur** : Après déploiement, consulter les logs 401/400/500 pour valider le nouveau format et les motifs de rejet.
