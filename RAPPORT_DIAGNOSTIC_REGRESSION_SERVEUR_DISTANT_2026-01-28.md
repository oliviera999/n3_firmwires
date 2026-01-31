# Diagnostic régression – Échanges serveur distant / ESP32

**Date** : 2026-01-28  
**Contexte** : Prod (v11.101) envoie toujours régulièrement ; v11.155 et antérieures fonctionnaient « presque correctement » ; version actuelle (wroom-test 11.156+) affiche "connection refused" et "SSL fatal alert" en boucle.

---

## 1. Conclusion du diagnostic

**La régression ne vient pas du serveur ffp3** (voir plan d’analyse serveur). Elle vient du **firmware** : dans le commit **38a934b** (28 Jan 2026), le nombre de tentatives HTTPS par requête est passé de **3 à 1** dans `src/web_client.cpp`. Toute erreur transitoire (connection refused, SSL fatal alert) fait échouer immédiatement sans retry.

**Correction recommandée** : rétablir **2 ou 3 tentatives HTTPS** dans `WebClient::httpRequest()` (remettre `maxAttempts = 2` ou `3` avec backoff).

---

## 2. Cause identifiée : suppression des retries HTTPS (commit 38a934b)

### Fichier concerné

- **Fichier** : [src/web_client.cpp](src/web_client.cpp)
- **Commit** : 38a934b – « Firmware: config, web_client, web_server, NVS, OTA, mailer, bootstrap; script 10min; nettoyage logs/analyses »
- **Modification** : vers les lignes 212–214, `const int maxAttempts = 3;` a été remplacé par `const int maxAttempts = 1;` avec le commentaire « Plan simplification: une seule tentative par appel, pas de retry interne (retry au prochain cycle tâche) ».

### Comportement avant / après

| Élément | Avant (v11.155 / prod v11.101) | Après (38a934b, actuel) |
|---------|--------------------------------|--------------------------|
| Tentatives HTTPS par requête | 3 (avec backoff exponentiel 1s, 4s) | 1 |
| Comportement sur "connection refused" / "SSL fatal alert" | Retry 2 à 3 fois | Échec immédiat |
| Fallback HTTP (port 80) | N/A ou 1 fois après 3 échecs HTTPS | 1 fois après 1 échec HTTPS |
| Prod v11.101 | Fonctionne (ancien code) | Inchangé (firmware figé) |

### Pourquoi la prod v11.101 continue de fonctionner

- Elle n’a **pas** le commit 38a934b : elle conserve **3 tentatives** par requête.
- Les refus ou erreurs TLS transitoires sont surmontés par les retries.
- Elle utilise les endpoints **PROD** (`/ffp3/post-data`, `/ffp3/api/outputs/state`, `/ffp3/heartbeat`) ; le code serveur est le même pour PROD et TEST.

### Pourquoi le fallback HTTP ne compense pas

- En cas d’échec HTTPS, le code tente `http://iot.olution.info/...` (port 80).
- Si le serveur n’écoute que sur 443 ou si le reverse proxy n’accepte que HTTPS, le fallback échoue.
- Et il n’y a qu’**une** tentative HTTP ; pas de retry non plus sur le fallback.

---

## 3. Historique Git pertinent

- **bdb06af** (v11.152) : Fix crash TLS concurrent – mutex STRICT pour GET HTTPS. Pas de suppression de retry.
- **a4b1d6e** (v11.155) : Simplification séquentielle communication réseau (mail dans automationTask, priorité heartbeat → mails → données). Pas de changement du nombre de tentatives HTTP.
- **c1caf36** (v11.158) : Réduction queues/buffers/cache (g_netQueue 5→3 slots). Pas de modification de `maxAttempts` dans web_client.
- **38a934b** (28 Jan 2026) : **Introduction de `maxAttempts = 1`** dans web_client.cpp + fallback HTTP. C’est la régression.

---

## 4. Recommandations correctives (côté firmware)

1. **Restaurer 2 ou 3 tentatives HTTPS** dans `WebClient::httpRequest()` :
   - Remettre `const int maxAttempts = 2;` ou `3;` (au lieu de 1).
   - Conserver le backoff existant (ou un backoff court, ex. 1s puis 2s).
2. **Optionnel** : ne déclencher le fallback HTTP qu’après échec de **toutes** les tentatives HTTPS (comportement déjà cohérent si on restaure les retries).
3. **Vérification** : après correction, flash + monitoring 5 min et vérifier dans les logs la présence de retries (ex. `[HTTP] Tentative 2/3`) et la réussite de requêtes vers `https://iot.olution.info/ffp3/...`.

---

## 5. Synthèse

- **Serveur ffp3** : aucune modification du code serveur nécessaire pour corriger cette régression ; le code ne peut pas provoquer "connection refused" ni "SSL fatal alert" (niveau TCP/TLS).
- **Firmware** : la régression est due à la **réduction à une seule tentative HTTPS** dans `src/web_client.cpp` (commit 38a934b). Restaurer 2 ou 3 tentatives HTTPS par requête est la correction recommandée.
