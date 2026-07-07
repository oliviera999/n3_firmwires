# Nomenclature « ffp3 » / « ffp5cs » / « ffp5 »

> Dépôt **n3_firmwires** (firmwares). Document jumeau côté serveur :
> `n3_serveur/docs/NOMENCLATURE_FFP3.md`. Les deux doivent rester cohérents.

Le radical **`ffp3`** est fortement surchargé dans l'écosystème. Ce document fixe le vocabulaire
canonique, la correspondance firmware ↔ serveur, et liste les chantiers volontairement différés.

## 1. Vocabulaire canonique

| Terme | Ce que ça désigne | Niveau |
|-------|-------------------|--------|
| **`ffp5cs`** | Le **firmware** de l'aquaponie (dossier `ffp5cs/`, versionné `ProjectConfig::VERSION`, ESP32 WROOM/S3). | firmware / matériel |
| **`ffp3`** | Le **système supervisé côté serveur** : tables `ffp3Data*`/`ffp3Outputs*`/`ffp3Heartbeat*`, routes `/post-data*`, dossier `Ffp3/`, cible OTA. | serveur / données |
| **`ffp5`** | Nom de **canal/schéma OTA** du firmware ffp5cs (`ffp5-wroom`) dans le pipeline de déploiement. Le chemin serveur reste pourtant `/ffp3/ota/`. | déploiement OTA |

**Règle d'or : on clarifie, on ne renomme pas le socle.** `ffp3` (serveur) porte des données de
production historiques ; `ffp5cs` (firmware) a tout son historique de version. On documente
l'équivalence plutôt que d'aligner les noms par un renommage risqué.

## 2. Les (au moins) 4 sens de « ffp3 »

1. **Système / données serveur** de l'aquaponie — cible du firmware `ffp5cs`
   (`firmwares.manifest.json` : `ffp5cs` a `serveurFolder: "ffp3"`, `otaTarget: "ffp3"`).
2. **Galerie caméra** — le firmware caméra `uploadphotosserver -e ffp3` poste vers
   `/ffp3gallery/` (photos, `board=5`), un sous-système serveur **distinct** des tables
   `ffp3Data*`. Même installation physique, firmware et flux différents.
3. **Dépôt serveur PHP** — le sous-module `ffp5cs/ffp3` pointe vers
   `github.com/oliviera999/ffp3.git` (le code serveur « FFP3 Datas », = dépôt n3_serveur).
4. **Contrat d'auth HMAC générique** — `shared/n3_data` nomme « FFP3 » sa version moderne du
   protocole HMAC ; `msp` (météo) et `n3pp` (serre) l'utilisent aussi, **sans lien** avec le
   système aquaponie. Ici « ffp3 » = version de protocole, pas le système.

## 3. Correspondance firmware ffp5cs → serveur ffp3

La famille serveur n'est **jamais** choisie par un identifiant du firmware : elle est portée par
la **route/URL** appelée (qui fixe l'environnement, donc la table).

| Profil firmware (`platformio.ini`) | Carte | Route POST | Env serveur | Table Data |
|---|---|---|---|---|
| `wroom-prod` | ESP32-WROOM | `/post-data` | `prod` | `ffp3Data` |
| `wroom-test` | ESP32-WROOM | `/post-data-test` | `test` | `ffp3Data2` |
| `wroom-s3-prod` | ESP32-S3 | `/post-data3` | `s3` | `ffp3DataS3` |
| `wroom-s3-test` | ESP32-S3 | `/post-data-s3-test` (ou `/post-data3-test`) | `s3test` / `test3` | `ffp3DataS3Test` / `ffp3Data3` |

- ⚠️ L'env serveur **`s3` est de la PRODUCTION** (ESP32-S3), pas un environnement de test.
- Depuis **v13.87**, le firmware appelle les routes **sans le préfixe `/ffp3/`** (les GET
  `/ffp3/*` renvoyaient un 301 Apache non suivi par le HTTPClient ESP32). Le serveur accepte les
  deux formes. Réf. `ffp5cs/include/server_url_config.h`.

## 4. Le champ POST `sensor` (convention, depuis firmware v15.09)

- `sensor` porte l'**identité système** : `ProjectConfig::SYSTEM_ID = "ffp3"`
  (`include/config_system.h`) — **pas** le type de carte.
- Ne pas confondre avec `ProjectConfig::BOARD_TYPE` (`esp32-wroom` / `esp32-s3`), qui reste utilisé
  pour la **clé de canal OTA** (`metadata.json → channels.<env>.<BOARD_TYPE>`) et le **préfixe de
  `post_id`** (déduplication anti-replay).
- Côté serveur, `sensor` est **journalisé et stocké** (tronqué à 30 caractères) mais **jamais
  validé ni utilisé pour router** : l'environnement/table vient de la route. Le corps signé HMAC
  reste auto-cohérent (les deux côtés signent la valeur transmise, quelle qu'elle soit).

## 5. Chantiers différés (non traités volontairement)

Options identifiées mais **écartées** pour l'instant, avec la raison :

| Chantier | Idée | Statut / raison |
|---|---|---|
| **Validation `sensor ↔ env` côté serveur** | Logger un **warning** (jamais un rejet) si `sensor` reçu ne correspond pas à l'env de la route, pour détecter un firmware mal configuré qui écrirait dans la mauvaise table `ffp3*`. | **Différé.** À faire *log-only* après stabilisation de la convention `sensor="ffp3"`. Un rejet casserait la prod. |
| **Aligner le canal OTA `ffp5` sur `ffp3`** | Renommer `ffp5-wroom` → `ffp3-*` pour un radical unique. | **Documenté seulement.** Touche le pipeline OTA (`firmware-ota-deploy.yml`, `deploy_ota.py`, chemins serveur) → risque « appareils qui ne reçoivent plus l'OTA ». Non prioritaire. |
| **Renommer le dossier firmware `ffp5cs/` → `ffp3/`** | Aligner firmware ↔ serveur. | **Rejeté.** Casse le sous-module `ffp5cs/ffp3`, le manifest, la CI matricielle, les scripts flash/OTA et l'historique git. Coût/bénéfice défavorable. |
| **Renommer les tables `ffp3Data*` → `ffp5*`** | Aligner serveur ↔ firmware. | **Rejeté.** Migration de données de production (100k+ lignes), réécriture `TableConfig`/repos/dashboards, risque de perte de données. |
| **Renommer le « contrat HMAC FFP3 »** (`n3_data`) | Il n'a rien de spécifique à ffp3. | **Rejeté.** Partagé par `msp`/`n3pp` + serveur `SignatureValidator` ; surface trop large pour un gain purement cosmétique. |
