# Audit de bugs — n3_firmwires (2026-07)

Audit de code transverse des bibliothèques `shared/` et des firmwares actifs
(robustesse réseau, OTA, contrat HMAC, logique pure). Chaque constat a été
**vérifié dans le code** (chemin d'appel remonté jusqu'à un déclencheur réel)
avant d'être qualifié de bug ; les constats non déclenchables aujourd'hui sont
explicitement marqués **latent**.

Pour chaque constat, ce document propose **une ou plusieurs options de correction**
avec leurs compromis, et indique lesquelles ont été **appliquées**.

**État au 2026-07-27** : **F1 est corrigé** (`n3_common` 1.8.2 ; n3pp 4.66, msp 2.69,
poissonglouton 0.5.20, uploadphotosserver 2.73). F2 à F6 restent ouverts — leurs
options sont documentées ci-dessous, le choix revient au mainteneur.

> Un audit jumeau couvre le serveur : `n3_serveur/docs/AUDIT_BUGS_2026-07.md`.
> Les constats **F2** (ici) et **S1** (là-bas) portent sur le même contrat HMAC.

## Synthèse

| # | Gravité | Sujet | Fichier principal | État |
|---|---------|-------|-------------------|------|
| F1 | 🔴 Élevé | Boucle de téléchargement OTA sans détection de stagnation → blocage indéfini | `shared/n3_common/src/n3_ota.cpp` | ✅ **corrigé** (`n3_common` 1.8.2) |
| F2 | 🟠 Moyen | Contrat « corps canonique HMAC » fragile + troncature silencieuse à 512 o | `ffp5cs/src/ffp3_post_body.cpp` | ouvert |
| F3 | 🟡 Faible | `compareVersions` ignore le retour de `sscanf` → OTA silencieusement inhibée | `shared/n3_common/src/n3_ota.cpp` | ouvert |
| F4 | 🟡 Faible | `integrityDetails[192]` non initialisé avant usage | `shared/n3_common/src/n3_ota.cpp` | ouvert |
| F5 | 🟡 Faible | `n3HmacSha256` déréférence `key` / `message` sans garde nulle | `shared/n3_hmac/src/n3_hmac.cpp` | ouvert |
| F6 | ⚪ Contrat | `N3SfBackend` : sémantique d'index ambiguë entre `peek()` et `commit()` | `shared/n3_store_forward/` | ouvert |

---

## F1 — 🔴 Boucle de téléchargement OTA sans détection de stagnation — ✅ CORRIGÉ

> **Correctif appliqué** — option A ci-dessous (`n3_common` 1.8.2 ; n3pp 4.66, msp 2.69,
> poissonglouton 0.5.20, uploadphotosserver 2.73). Constante `N3_OTA_STALL_TIMEOUT_MS`
> (15 s par défaut, surchargeable par `-D`) : au-delà de ce délai **sans aucun octet reçu**,
> l'OTA est abandonnée proprement (`Update.abort()` + `http.end()`) avec un message d'échec
> explicite. Aucune borne sur la durée TOTALE : un téléchargement lent qui **progresse**
> n'est pas affecté.

### Constat

`downloadAndFlashFirmware()` (`shared/n3_common/src/n3_ota.cpp:228-283`) :

```cpp
while (http.connected() && remaining > 0) {
    size_t availableBytes = stream->available();
    if (availableBytes == 0) {
        delay(1);
        continue;          // <-- aucune borne de temps
    }
    ...
}
```

Aucune condition de sortie ne couvre le cas « la connexion TCP reste ouverte mais
le serveur n'envoie plus rien » :

- `http.setTimeout(30000)` (ligne 165) s'applique aux opérations bloquantes de
  `HTTPClient` / `readBytes`, **pas** à cette boucle de scrutation manuelle sur
  `available()` ;
- `http.connected()` reste vrai tant que le socket n'est pas fermé — un serveur
  qui accepte, répond `200`, envoie ses en-têtes puis se tait (proxy saturé,
  captive portal, coupure côté serveur sans RST) maintient la condition ;
- `delay(1)` rend la main à l'ordonnanceur FreeRTOS, donc la tâche IDLE tourne et
  **le watchdog de tâche ne se déclenche pas** : l'appareil ne redémarre même pas,
  il reste figé dans l'OTA.

Le blocage se produit avec `Update.begin()` déjà appelé : la partition OTA est en
cours d'écriture et l'appareil ne repasse ni en mesure, ni en sommeil profond.
Sur les firmwares deep-sleep (n3pp, msp), cela consomme la batterie jusqu'à
épuisement.

### Options de correction

**Option A (recommandée) — chien de garde de stagnation.**

```cpp
unsigned long lastDataMs = millis();
const unsigned long kStallTimeoutMs = 15000;   // aucun octet reçu -> abandon

while (http.connected() && remaining > 0) {
    size_t availableBytes = stream->available();
    if (availableBytes == 0) {
        if (millis() - lastDataMs > kStallTimeoutMs) {
            if (details && detailsSize > 0) {
                snprintf(details, detailsSize,
                         "OTA firmware: flux bloque (%d/%d octets recus).",
                         writtenTotal, contentLen);
            }
            mbedtls_md_free(&mdCtx);
            Update.abort();
            http.end();
            return false;
        }
        delay(1);
        continue;
    }
    lastDataMs = millis();
    ...
}
```

Cible précisément le mode de panne, sans borner la durée légitime d'un
téléchargement lent (une image de 1,5 Mo sur un lien faible peut dépasser la minute).

**Option B — budget global de téléchargement** (ex. 180 s depuis `Update.begin()`),
en complément ou en remplacement. Plus simple à raisonner, mais risque de couper
un téléchargement légitime en RSSI dégradé — à dimensionner sur le pire cas
mesuré de la flotte.

**Option C — laisser `HTTPClient` gérer.** Supprimer la scrutation manuelle et
lire directement via `stream->readBytes()` (bloquant, borné par `setTimeout`).
Le plus propre, mais modifie la boucle de progression et mérite un test terrain
avant déploiement.

Les options A et B sont cumulables (stagnation courte + budget long).

---

## F2 — 🟠 Contrat « corps canonique HMAC » fragile + troncature silencieuse

Contexte : `Ffp3PostBody::buildFullUpdateBody()` (`ffp5cs/src/ffp3_post_body.cpp:129`)
construit le corps `x-www-form-urlencoded` **exactement** dans l'ordre que le
serveur reconstitue dans `App\Security\Ffp3HmacPostBody` — parce que sous mod_php
`php://input` est vide et que le serveur doit re-fabriquer le corps signé
(cf. `n3_serveur/CHANGELOG.md` 5.1.12, et constat **S1** de l'audit serveur).

Deux fragilités concrètes.

### F2a — troncature silencieuse des paires supplémentaires

`applyExtraPairs()` (`ffp5cs/src/ffp3_post_body.cpp:101-127`) :

```cpp
char buf[512];
size_t len = strlen(extraPairs);
if (len >= sizeof(buf)) len = sizeof(buf) - 1;   // <-- silencieux
memcpy(buf, extraPairs, len);
```

Au-delà de 511 octets, la chaîne est coupée **au milieu d'une paire** : la
dernière clé/valeur est corrompue ou perdue, sans aucun log. Le corps signé
diverge alors de ce que le serveur reconstitue → **401 « Signature incorrecte »**
sur toutes les mesures concernées, avec une cause invisible dans les traces
firmware. Même remarque, plus bas, sur le débordement de `store.pairs`
(ligne 71 : `if (store.count >= …) return;` — extra ignoré en silence).

*Fix* : faire remonter la condition — retour booléen ou
`Serial.printf("[POST][WARN] extraPairs tronque (%u > %u)\n", …)` — pour que le
diagnostic pointe la cause réelle plutôt que « HMAC invalide ».

### F2b — le formatage des valeurs n'est pas partagé

Le firmware émet la chaîne telle qu'elle a été construite ; le serveur, lui,
**reformate** dans `Ffp3HmacPostBody::formatPair()` :
`sprintf('%.1f')` pour `TempAir`, `Humidite`, `Pression`, `TempEau`,
`chauffageThreshold` ; cast `(int)` pour `EauAquarium`, `Luminosite`, `limFlood`,
`tideWindowMs`, etc.

Tant que les deux côtés produisent la même représentation, tout va bien. Mais
toute évolution unilatérale — un champ passé de `%.0f` à `%.1f`, un nouveau champ
numérique ajouté d'un seul côté — casse l'authentification **en production, en
silence**, sans qu'aucun test ne le voie : `ffp5cs/test/test_post_body` valide le
firmware seul, `Ffp3HmacPostBodyTest` valide le serveur seul, et rien ne compare
les deux.

### Options de correction

**Option A (recommandée) — vecteurs d'or partagés.**
Versionner un jeu de cas `(entrées → corps canonique attendu)` dans un JSON commun
aux deux dépôts, consommé par `ffp5cs/test/test_post_body` **et**
`Ffp3HmacPostBodyTest`. Toute divergence de format devient un échec de CI, des
deux côtés, avant déploiement.

**Option B — supprimer la reconstitution.** Faire signer au firmware un condensé
stable (`sha256` des paires triées) plutôt que la sérialisation exacte, ou poster
en `application/json` — cas où `php://input` reste lisible sous mod_php. Supprime
définitivement la classe de bug, au prix d'une évolution de contrat coordonnée
firmware + serveur (voir option C du constat S1 côté serveur).

**Option C — journaliser le corps signé.** Ajouter `sha256(body)[0:16]` aux traces
firmware, en miroir du `body_hash` déjà journalisé côté serveur
(`n3_serveur/docs/ENDPOINTS_ESP32_SERVEUR.md:223`). Ne corrige rien mais rend le
diagnostic immédiat : deux hachages côte à côte au lieu d'un 401 opaque.

### Note connexe — N3PP / MSP1

`n3DataPost()` (`shared/n3_data/src/n3_data.cpp:96-149`) émet les mêmes en-têtes
`X-Sig-*` dès que `API_SIG_SECRET` est renseigné (`n3pp/src/n3pp_network.cpp:59`,
`msp/src/msp_network.cpp:61`), **mais le serveur ne dispose d'aucune reconstitution
canonique pour ces familles** : le body-signing y échoue systématiquement sous
mod_php, sans repli `api_key`. Détail et correctifs dans le constat **S1** de
l'audit serveur — à traiter côté serveur, ou à neutraliser côté firmware en
laissant `API_SIG_SECRET` vide sur n3pp/msp jusqu'à correction.

Particularité à ne pas manquer si l'on écrit cette reconstitution : contrairement
à ffp5cs, `n3DataPost` ajoute `timestamp=…&signature=…` **dans le corps signé**
(lignes 101-105, avant le calcul ligne 143), alors que `Ffp3HmacPostBody` les
exclut explicitement via `AUTH_KEYS`.

---

## F3 — 🟡 `compareVersions` ignore le retour de `sscanf`

`shared/n3_common/src/n3_ota.cpp:390-398` :

```cpp
static int compareVersions(const char* v1, const char* v2) {
    int maj1 = 0, min1 = 0, pat1 = 0;
    ...
    sscanf(v1, "%d.%d.%d", &maj1, &min1, &pat1);   // retour ignoré
```

Si `version` de la metadata n'est pas parsable en tête (préfixe `v`, espace,
chaîne vide, champ JSON d'un autre type), les variables restent à 0 → la version
distante est vue comme `0.0.0` → `compareVersions(...) <= 0` → **« Déjà à jour »**
et la mise à jour est ignorée en silence (`n3_ota.cpp:477-481`). Une metadata
malformée se traduit donc par une flotte qui ne se met plus à jour, sans erreur
visible.

Effet de bord bénin mais à connaître : le format à deux composantes de ce dépôt
(`15.09`, `15.10`) fonctionne parce que `%d` lit `09` comme `9` et que `pat`
reste à 0 — un passage à un format à zéros significatifs (`15.9` vs `15.09`)
casserait l'ordre.

### Options de correction

**Option A (recommandée)** — vérifier le nombre de champs lus et refuser une
metadata illisible :

```cpp
if (sscanf(v1, "%d.%d.%d", &maj1, &min1, &pat1) < 2) return 0;  // illisible
```

avec, côté `n3OtaCheck()`, un log + `onUpdateEnd(false, "OTA ignoree: version distante illisible.")`
pour que la condition remonte au serveur au lieu de se confondre avec « déjà à jour ».

**Option B** — normaliser à la source : imposer `MAJOR.MINOR.PATCH` dans la
metadata OTA générée par le serveur, et le vérifier dans le script de publication.
Corrige la cause plutôt que le symptôme, mais ne protège pas d'une metadata
corrompue en transit.

---

## F4 — 🟡 `integrityDetails[192]` non initialisé

`n3OtaCheck()` (`shared/n3_common/src/n3_ota.cpp:489`) déclare
`char integrityDetails[192];` puis, en cas d'échec, le passe à `Serial.printf`
et à `config.onUpdateEnd()` (lignes 494-499).

Vérification faite : **tous** les chemins de retour `false` de
`downloadAndFlashFirmware()` écrivent bien dans `details` — le constat n'est donc
pas déclenchable aujourd'hui. Mais la garantie tient uniquement à la discipline
de l'appelé, et `verifyFirmwareSignature()` a déjà une sortie `false` sans
écriture (`n3_ota.cpp:64`, rendue inatteignable par le garde de l'appelant
ligne 324). Un futur chemin d'échec qui oublie `snprintf` afficherait de la
mémoire de pile non initialisée dans un log et dans un e-mail d'alerte.

*Fix (1 ligne)* : `char integrityDetails[192] = {0};` — ou
`integrityDetails[0] = '\0';` juste après la déclaration.

---

## F5 — 🟡 `n3HmacSha256` déréférence `key` / `message` sans garde nulle

`shared/n3_hmac/src/n3_hmac.cpp:88-91` appelle `strlen(key)` et `strlen(message)`
sans vérifier la nullité, alors que la fonction est exportée dans l'en-tête public
`n3_hmac.h` et documentée sans précondition. Les appelants actuels
(`n3DataPost` ligne 100, `n3HmacSignRequest` ligne 108) passent des pointeurs
garantis non nuls — constat **latent**.

Le module frère `n3_hmac_canonical.cpp` fait, lui, les bonnes vérifications
(`computeHmacHex`, lignes 145-148) : l'incohérence entre les deux est le vrai
défaut.

*Fix* : aligner sur `computeHmacHex` —
`if (!key || !message || !hexOutput || hexOutputSize < 65) return false;`

---

## F6 — ⚪ `N3SfBackend` : sémantique d'index ambiguë entre `peek()` et `commit()`

`shared/n3_store_forward/src/n3_store_forward.h` documente `commit()` comme
« Consomme l'élément après acquittement (**avance le curseur / retire la clé**) »,
tandis que `n3SfDrain()` (`n3_store_forward.cpp:41-51`) itère par index croissant
`peek(0)`, `peek(1)`, `peek(2)`… **sur la même instance de backend**.

Ces deux propriétés ne sont compatibles que si les index sont **stables pendant
tout le drain**. C'est le cas de l'unique implémentation réelle,
`SdCursorBackend` (`uploadphotosserver/src/camera_sync.cpp:171-186`) : `peek()`
lit un `std::vector<SyncEntry>` figé et `commit()` n'écrit que le curseur NVS,
sans toucher au vecteur. **Aucun bug aujourd'hui.**

En revanche, un backend « file NVS indexée » qui retire réellement la clé au
commit — usage explicitement envisagé par l'en-tête (« file NVS indexée côté
ffp5cs », ligne 31) — décalerait les index à chaque commit et **sauterait un
élément sur deux**. Le risque n'est couvert par aucun test : le `MockBackend` de
`shared/tests_native/test/test_store_forward/test_store_forward.cpp:17-34`
enregistre les commits sans jamais retirer d'élément.

### Options de correction

**Option A (recommandée)** — verrouiller le contrat dans l'en-tête : « les index
de `peek()` DOIVENT rester stables pendant tout le drain ; un backend qui retire
l'élément au commit doit différer la suppression à la fin du drain », et ajouter
un test natif avec un backend retirant à la volée pour figer le comportement attendu.

**Option B** — rendre l'orchestrateur robuste aux deux sémantiques : itérer
toujours sur `peek(0)` et ne progresser l'index que lorsqu'un élément n'a pas été
committé (`HardFail` mis à part). Plus sûr, mais change la boucle actuelle — donc
à couvrir par les tests existants avant de l'adopter.

---

## Points vérifiés et écartés (non-bugs)

Consignés pour éviter de les ré-auditer.

- **Dépassements de tampon C** — aucun `strcpy` / `strcat` / `sprintf` non borné
  dans `shared/`, `ffp5cs/`, `n3pp/`, `msp/`, `poissonglouton/`,
  `uploadphotosserver/` (seule occurrence : un commentaire dans
  `ffp5cs/include/tls_mutex.h`). Toutes les écritures passent par `snprintf` avec
  une taille correcte.
- **`n3HmacSha256`, boucle hexadécimale** (`n3_hmac.cpp:98-102`) : pour `i = 31`,
  `snprintf(hexOutput + 62, 65 - 62 = 3, "%02x", …)` écrit exactement 2 caractères
  + NUL dans les limites du tampon.
- **`verifyFirmwareSignature`** (`n3_ota.cpp:76-89`) : `malloc(strlen(signatureB64))`
  est toujours suffisant, un décodage base64 produisant ~3/4 de la taille d'entrée ;
  `free()` est appelé sur tous les chemins de retour.
- **`FloodAlert::evaluate`** (`ffp5cs/include/automatism/flood_alert.h`) : les
  soustractions d'epoch sont protégées contre le repli `uint32_t`
  (`(nowEpoch >= st.xxx) ? … : 0`). Le renvoi répété de `ExitFlood` est sans
  conséquence ici (l'appelant se contente de remettre un drapeau à `false`, de
  façon idempotente) — contrairement au portage serveur, cf. constat **S6** de
  l'audit serveur.
- **`Ffp3PostBody::sortExtras` / `appendPair`** : tri correct (échanges
  successifs, équivalent à un tri par sélection) ; `appendPair` borne bien
  `snprintf` et détecte le débordement via `pos >= cap`.
