# Backlog bench-gated : concurrence `s_lastFetchedJson` & unification OTA

> Items issus de l'audit qui **ne doivent PAS être appliqués en validation compile-seule** :
> ils touchent des chemins critiques (reboots / flash OTA) dont la justesse runtime exige
> un **banc réel**. Patchs/designs vérifiés ci-dessous, prêts pour une session avec matériel.
> Le serveur (`n3_serveur`) supporte désormais HTTP Range (206) — pré-requis de l'unification OTA.

---

## 1. `s_lastFetchedJson` — race + référence pendante (cause probable de reboots #2)

### Constat (web_client.cpp)
- `s_lastFetchedJson` (`char[]`, ligne 65) écrit par **netTask** (`fetchRemoteState`, lignes 742-744)
  et lu par **autoTask** via `copyLastFetchedTo` (ligne 758, `deserializeJson`), **sans verrou**.
- **Piège supplémentaire (sous-estimé)** : `deserializeJson(doc, s_lastFetchedJson)` reçoit un
  `char*` **mutable** → ArduinoJson opère en **zero-copy** : `doc` conserve des **pointeurs vers
  le buffer** après le retour de `copyLastFetchedTo`. Donc `doc`, utilisé ensuite par autoTask,
  référence toujours `s_lastFetchedJson` que netTask peut réécrire → `LoadProhibited` (le code le
  note lignes 64/721/801).
- ⚠️ Un simple mutex autour du `deserializeJson` **ne suffit pas**, et copier vers un buffer
  **local** (proposé par une analyse) est **faux** (le `doc` pointerait vers un buffer détruit au
  retour) — et de toute façon impossible sur la pile d'autoTask (HWM ~95 %, 976 o libres).

### Correctif retenu (mutex dédié + désérialisation en mode COPIE)
1. Ajouter `static SemaphoreHandle_t s_lastFetchedJsonMutex = nullptr;` près de `s_httpMutex`
   (ligne ~23), créé paresseusement (même motif que `s_httpMutex`). `<freertos/semphr.h>` déjà inclus.
2. **Écriture** (742-744) : entourer le `memcpy` + `s_lastFetchedSize = jsonLen` de
   `xSemaphoreTake/Give(s_lastFetchedJsonMutex, …)`.
3. **Lecture** (`copyLastFetchedTo`) : sous le mutex, désérialiser en **forçant la copie** :
   `deserializeJson(doc, (const char*) s_lastFetchedJson);` — le cast en `const char*` fait
   dupliquer les chaînes dans le pool de `doc` (élastique en ArduinoJson 7) → `doc` devient
   **indépendant** du buffer. Relâcher le mutex juste après ; l'unwrap outputs/switches opère
   ensuite sur un `doc` autonome.
   - Pas de gros buffer sur la pile (on tient le mutex pendant la désérialisation ~ms ; seul le
     `memcpy` d'écriture de netTask est brièvement bloqué — acceptable, écriture peu fréquente).
   - Ordre de verrous sûr : `copyLastFetchedTo` ne prend QUE `s_lastFetchedJsonMutex` (jamais
     `s_httpMutex`) → pas d'inversion avec le wrapper `fetchRemoteState` (qui prend `s_httpMutex`).

### À valider sur banc
- Confirmer disparition des `LoadProhibited` sous toggle WiFi + polling auto concurrent (2-4 h).
- Vérifier l'impact heap du mode copie (transitoire ~1,5 Ko) — négligeable attendu.
- Confirmer qu'aucun appelant ne dépendait du zero-copy (revue : seuls autoTask/netTask-boot).

---

## 2. Unification des 3 modes de download OTA (audit S3)

### Constat
Trois implémentations en cascade (`ota_manager_download.cpp` / `_alt.cpp`) :
`downloadFirmwareModern` (esp_http_client) → `downloadFirmware` (HTTPClient) →
`downloadFirmwareUltraRevolutionary` (micro-chunks 2 Ko). Le mode micro-chunks **n'aide pas**
contre la latence o2switch (front-loadée à la connexion, pas au streaming).

### Design retenu (téléchargement adaptatif **résumable** unique)
- **Un seul chemin** (recommandé : `esp_http_client`), avec **reprise via HTTP Range** :
  sur coupure, rouvrir avec `Range: bytes=<octets_écrits>-`, attendre **206** et poursuivre
  l'écriture flash ; gérer **200** (serveur ignore Range → redémarrage propre depuis 0) et **416**
  (erreur fatale). Le serveur supporte maintenant 206 (cf. `OtaFileController` côté n3_serveur).
- Conserver : validation **taille + MD5** de bout en bout, **watchdog** (`esp_task_wdt_reset`) dans
  la boucle de lecture, partition cible sauvegardée avant `Update.begin()`, `esp_ota_set_boot_partition`.
- Retries bornés avec backoff exponentiel ; timeout global 5 min conservé.
- `updateTask()` : remplacer la cascade Modern→Classic→Ultra par un appel unique ;
  **supprimer** `downloadFirmwareUltraRevolutionary` (et l'un des deux chemins HTTP).

### Point dur — MD5 sur reprise
`Update.setMD5()` calcule le MD5 du **flux écrit**. Si l'on reprend à l'octet N en ne streamant
que N..EOF, le contexte MD5 d'`Update` ne correspondra pas au MD5 du fichier complet. **À vérifier
sur banc** : comportement exact d'`Update` sur reprise (réinitialise-t-il le contexte ?). Sinon,
n'autoriser la reprise que si la cohérence MD5 complète est garantie, ou valider via un hash applicatif.

### À valider sur banc (obligatoire avant merge)
Reprise à 10/50/90 %, coupures multiples, serveur renvoyant 200 au lieu de 206, 416, watchdog sous
réseau lent, MD5 corrompu → échec propre sans flash partiel. Rollout progressif (beta → canary → prod)
avec plan de rollback (revert = ré-activer la cascade). **Ne pas merger sans banc.**

---

## 3. Autres items bench-gated (rappel)
- Réduction de profondeur de pile `autoTask`/`netTask` (déporter gros `JsonDocument` locaux en statique/BSS).
- God-class `automatism` : extraire état + interface `IActuators`.
- Bornage du `portMAX_DELAY` du mutex HTTP (`web_client.cpp:116`) — comportement timeout à valider.
