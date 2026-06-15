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

---

## 4. Mise à jour 2026-06-14 (analyse multi-agents)

### Appliqué (concurrence, atomic — strictement plus sûr, compile-validé CI)
- **`net_request_pool.cpp`** : `s_netRequestPoolUsed[]` (bool) → `std::atomic<bool>` + `compare_exchange_strong`
  dans `netRequestAllocTrySlot`. Corrige une **vraie course alloc inter-tâches** (check-then-set ; l'accès
  parallèle netTask/auto/web est déjà documenté dans le code, cf. `app_tasks.cpp:370`) → évitait deux tâches
  prenant le même slot.
- **`app_tasks.cpp`** : `s_heartbeatDroppedCount` (uint32_t) → `std::atomic<uint32_t>` (compteur diag multi-tâches).

### Investigué mais NON appliqué — pièges identifiés
- **Bornage mutex HTTP** : déjà borné (tous les `xSemaphoreTake` de `web_client.cpp` utilisent
  `pdMS_TO_TICKS(...)`, aucun `portMAX_DELAY`). **Rien à faire.**
- **Compteur HTTP `diagnostics.cpp`** : passer les membres `httpSuccessCount/FailCount` en `atomic` casserait
  la copiabilité de `DiagnosticStats` (struct copiée en de nombreux endroits) → **trop risqué pour un compteur
  diag, écarté**.
- **Profondeur de pile (static-conversion)** : ⚠️ les plus gros candidats sont **multi-tâches** — les rendre
  `static` introduirait une **NOUVELLE course** (pire que la pile économisée) :
  - `AutomatismSync::processFetchedRemoteConfig` (3× `JsonDocument` ≈ 6 Ko) — appelé par netTask
    (`app_tasks_net.cpp:86`) **ET** autoTask (`automatism.cpp:269`, `automatism_sleep.cpp:477`).
  - `WebClient::copyLastFetchedTo` (`tmp` ≈ 1 Ko) — appelé par netTask-boot (`app_tasks_net.cpp:85`) **ET**
    autoTask (`automatism_sync.cpp:527`).
  → La vraie solution reste de **réduire la profondeur d'appel** (refactor), validée sur banc. Ne PAS
    static-iser ces buffers sans verrou.

### OTA résumable — impl complète rédigée, BENCH-REQUISE
Une implémentation complète `downloadFirmwareAdaptiveResumable` (esp_http_client + Range 206 + backoff) a été
produite. **Point dur non résolu sans banc** : le contexte MD5 d'`Update` sur reprise — si le serveur renvoie
206 partiel, le hash du flux N..EOF doit correspondre au MD5 du fichier complet, ce qui **n'est pas garanti** et
DOIT être validé sur matériel (sinon flash corrompu). Stratégie hybride retenue (206→continuer, 200→redémarrer
propre, 416→fatal) mais **à prouver sur banc avant tout merge**.

---

## 5. Mise à jour 2026-06-15 (chantiers C2/C3/C4 — PR draft bench-gated)

### C3 — Réduction profondeur de pile (PR draft, CI-validable, **HWM banc-requise**)
Appliqué : déplacement des gros buffers de travail **de la pile vers le heap local** (alloués/libérés à
chaque appel via `std::unique_ptr` + `new(std::nothrow)`), **sans** static-isation (la course était le piège
identifié au §4). Fonctions multi-tâches concernées :
- `AutomatismSync::processFetchedRemoteConfig` (`automatism_sync.cpp`) : `docJson[2048]` + `inputCopy`
  (`StaticJsonDocument<2048>`) + `normalizedDoc` (`StaticJsonDocument<2048>`) + `jsonStr[REMOTE_JSON_CACHE_SIZE]`
  ≈ 6-8 Ko regroupés dans une struct `ProcessBuffers` allouée en heap. Dominant de la pile autoTask (HWM ~95%).
- `WebClient::copyLastFetchedTo` (`web_client.cpp`) : `tmp` (`StaticJsonDocument<JSON_DOCUMENT_SIZE>`, jusqu'à
  4 Ko sur S3) déplacé en heap dans la branche unwrap.

Comportement inchangé ; dégradation gracieuse sous OOM (`new(std::nothrow)`→nullptr→`return false`, retry au
cycle suivant) au lieu d'un débordement de pile. **À valider sur banc** : `uxTaskGetStackHighWaterMark`
avant/après (DoD : marge autoTask/netTask > 20 %), 0 stack-overflow, et impact heap des allocations transitoires
(~8 Ko) sous polling soutenu + faible heap. CI : builds wroom + S3 valident la compilation.

---

## 6. Mise à jour 2026-06-15 — C2 unification OTA (PR draft, **BENCH-REQUISE**)

### Implémenté (CI-validable, NON mergeable sans banc)
- Cascade `downloadFirmwareModern → downloadFirmware → downloadFirmwareUltraRevolutionary` **supprimée**.
  Remplacée par **un seul chemin** `OTAManager::downloadFirmwareAdaptiveResumable` (esp_http_client),
  + helper `openFirmwareConnection(url, rangeStart, &status)` qui pose l'en-tête `Range` si reprise.
- `updateTask` n'appelle plus que ce chemin unique (rollback = revert de la PR → ré-active la cascade).
- `downloadFirmwareUltraRevolutionary` (micro-chunks) **retiré** de `ota_manager_download_alt.cpp`
  (seul `downloadFilesystem` y reste).
- Stratégie reprise : **206**→poursuivre la MÊME session `Update` (offset = octets écrits) ;
  **200**→`Update.abort()`+`begin()` puis ré-écriture depuis 0 ; **416**→fatal. Backoff exponentiel
  plafonné (`OTA_RESUME_*` dans `config_network.h`), timeout global 5 min, feed TWDT dans la boucle.
- **Garde-fou MD5** : la partition boot n'est marquée (`esp_ota_set_boot_partition`) **que** si
  `Update.end()` (qui vérifie taille + MD5 du flux complet) réussit. Donc même si la logique de reprise
  était subtilement fausse, un flash corrompu ne sera **jamais** booté.
- **Bug latent corrigé** : l'ancien `downloadFirmwareModern` comparait le retour de
  `esp_http_client_fetch_headers()` (= content-length) au code 200 ; le nouveau code lit le code via
  `esp_http_client_get_status_code()`.

### À VALIDER SUR BANC (bloquant avant merge) — WROOM **et** S3
- Continuité réelle du contexte MD5 d'`Update` entre fragments 206 contigus (point dur §2/§4).
- Reprise à 10/50/90 %, coupures multiples, serveur renvoyant 200 au lieu de 206, 416, réseau lent
  (watchdog), MD5 corrompu → **échec propre sans flash partiel booté**.
- Rollout progressif beta→canary→prod, plan de rollback = revert de la PR (ré-active la cascade).
- NB : on pourrait renforcer en parsant `Content-Range` (offset renvoyé == offset demandé) via un
  handler d'en-tête — non fait ici (le garde-fou MD5 final couvre déjà la corruption). À évaluer au banc.
---

## 7. Mise à jour 2026-06-15 — C4 découpe god-class (PR draft, CI-verte)

### C4 — Casser la god-class `automatism` (1re étape sous filet de tests, CI-verte)
Première extraction (petite étape, comportement strictement identique, validée nativement) :
- Nouveau module **pur** `include/automatism/flood_alert.h` (`FloodAlert::evaluate` + `markEmailSent`) :
  machine d'état anti-spam « aquarium trop plein » (debounce + cooldown + hystérésis de sortie). Aucune
  dépendance Arduino/config → testable en `g++`.
- `Automatism::handleAlerts` (`automatism_display.cpp`) délègue la **décision** à `FloodAlert::evaluate` ;
  les **effets de bord** (email, NVS, blink OLED, flag `_highAquaSent`) restent dans l'appelant.
- Suite de tests native `test/test_flood_alert/` (10 cas : debounce, cooldown, 1er email, mail désactivé,
  sortie d'hystérésis, zone morte, reset compteurs) ajoutée à `firmware-ci.yml` **et** `platformio-native.ini`.

DoD partielle atteinte : logique extraite + testée, **aucune régression de comportement** (parité ligne à ligne
avec l'ancien bloc inline ; vérifiée en local `-Wall -Wextra`). Reste bench-gated pour les chemins matériels
non touchés ici (nourrissage/pompe/chauffage). Étapes suivantes proposées : extraire l'interface `IActuators`
et les contrôleurs refill/display restants — **par petites étapes**, chacune CI-verte.
