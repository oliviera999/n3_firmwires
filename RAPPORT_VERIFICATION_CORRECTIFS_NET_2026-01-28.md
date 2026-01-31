# Rapport – Vérification des correctifs réseau (monitoring 5 min, 28/01/2026)

**Log analysé :** `monitor_5min_2026-01-28_21-36-09.log`  
**Correctifs appliqués :** stabilisation TCP avant 1ère requête (`waitForNetworkReady` dans netTask) + `maxAttempts = 2` dans `web_client.cpp`.

---

## 1. Impact des correctifs

### 1.1 Stabilisation TCP au boot — **efficace**

- **Preuve :** `[netTask] Boot tryFetchConfigFromServer: OK` à 21:36:25 (ligne 602).
- WiFi connecté à 21:36:13 (STA_GOT_IP), première requête config réussie ~12 s plus tard après `waitForNetworkReady()`.
- Aucune erreur "connection refused" ou SSL au démarrage ; la 1ère requête HTTPS (config) a réussi.

### 1.2 Retries (maxAttempts = 2)

- Les messages "Tentative 1/2" et "Tentative 2/2" ne sont émis qu’en `LOG_DEBUG` ; en build wroom-test le niveau est probablement inférieur, donc absents du log.
- Les 2 erreurs "connection refused" visibles viennent de la bibliothèque (`HTTPClient.cpp:1421`), pas du format "[HTTP] Erreur … (tentative X/Y)". On ne peut pas déduire du log si une 2ᵉ tentative a eu lieu pour ces requêtes, mais le code avec `maxAttempts = 2` est bien en place.

---

## 2. Erreurs restantes (2 "connection refused")

### Erreur 1 — 21:38:19 (port 80, HTTP fallback)

- **Contexte :** `failed connect to iot.olution.info:80` puis `returnError(): error(-1): connection refused`.
- **Interprétation :** La phase HTTPS avait déjà échoué (voir ci‑dessous), le firmware est passé en fallback HTTP (GET). La connexion TCP sur le port 80 a été refusée (serveur, pare-feu ou pas d’écoute HTTP).
- **Juste avant :** à 21:38:14 on voit `[HTTP] HTTPS failed, trying HTTP fallback (GET)` — donc échec HTTPS puis tentative HTTP.

### Erreur 2 — 21:40:32 (port 443, HTTPS)

- **Contexte :** `select returned due to timeout 5000 ms`, `TLS start postponed`, `start_ssl_client: connect failed: -1`, `failed connect to iot.olution.info:443`, puis `returnError(): error(-1): connection refused`.
- **Interprétation :** Timeout de connexion TCP (5 s) côté TLS, puis échec rapporté comme "connection refused". Cause probable : réseau ou serveur temporairement indisponible / lent.

---

## 3. Problème dominant : fragmentation mémoire (TLS reportée)

Pendant une grande partie du monitoring, les requêtes HTTPS sont **reportées** avant même toute tentative de connexion :

- Messages répétés : `[HTTP] ⚠️ Plus grand bloc insuffisant (34804 bytes < 45KB), fragmentation=58%` et `[HTTP] ⚠️ TLS nécessite ~42-46KB contiguë, report de la requête`.
- **Plus grand bloc libre** souvent vers **34 KB** (parfois pire, ex. **19 KB**, fragmentation 76 %).
- mbedTLS a besoin d’environ **45 KB contigu** ; tant que cette condition n’est pas remplie, la requête est repoussée (pas de tentative TCP).
- Quand une requête part malgré tout (ex. après libération temporaire ou fallback HTTP), elle peut alors rencontrer "connection refused" comme ci‑dessus.

Conclusion : les 2 "connection refused" ne sont pas uniquement dues au manque de retry ou au timing au boot ; elles surviennent dans un contexte où **beaucoup de requêtes sont déjà bloquées par la fragmentation**, et les rares qui partent peuvent encore échouer pour des raisons réseau/serveur.

---

## 4. Synthèse et recommandations

| Élément | Statut |
|--------|--------|
| Boot : 1ère requête config | OK (tryFetchConfigFromServer: OK) |
| Stabilisation TCP avant 1ère requête | Efficace |
| maxAttempts = 2 | En place (impact non visible en log, niveau DEBUG désactivé) |
| Erreurs "connection refused" | 2 sur 5 min (1× HTTP:80, 1× HTTPS:443 timeout → -1) |
| Cause principale des échecs / reports | Fragmentation mémoire (bloc max &lt; 45 KB) → TLS reportée en boucle |

### Recommandations

1. **Conserver** les correctifs actuels (waitForNetworkReady + maxAttempts = 2) ; ils améliorent le boot et la résilience.
2. **Priorité suivante :** réduire l’usage mémoire TLS et/ou la fragmentation (ex. : libérer plus tôt les buffers TLS, réduire allocations concurrentes, ou revoir le seuil / la stratégie de "report" quand le plus grand bloc &lt; 45 KB).
3. **Optionnel :** activer temporairement `LOG_DEBUG` (ou au moins des logs WARN explicites avec "tentative X/Y" pour les erreurs HTTP) en build wroom-test pour vérifier en log que la 2ᵉ tentative est bien faite lors d’un "connection refused".

---

*Rapport généré après analyse du log du 28/01/2026 (monitoring 5 min post‑correctifs).*
