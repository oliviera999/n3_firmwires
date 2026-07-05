# Prompt de correction — audit `uploadphotosserver` (2026-07-05)

Prompt autonome pour une session dédiée à l'implémentation des correctifs de
[`AUDIT-2026-07-05.md`](./AUDIT-2026-07-05.md). Couvre **tous** les findings (A1–A9, M1–M4,
B1), impose une **phase de vérification du code avant tout codage**, et fige les arbitrages
déjà tranchés sur A3 (fuseau) et A2 (contrat serveur).

> À copier-coller tel quel comme message d'ouverture de la nouvelle session.

---

```
Contexte : dépôts n3_firmwires (firmware ESP32-CAM `uploadphotosserver` v2.55, cibles
msp1/n3pp/ffp3) et n3_serveur (contrat serveur). Un audit complet existe dans
`uploadphotosserver/AUDIT-2026-07-05.md` (findings A1–A9, M1–M4, B1, avec emplacements
fichier:ligne et pistes). Objectif : implémenter les correctifs de TOUS les points de l'audit.

============================================================
PHASE 0 — VÉRIFICATION DU CODE (OBLIGATOIRE, AVANT TOUT CODAGE)
============================================================
1. Lis intégralement `uploadphotosserver/AUDIT-2026-07-05.md`.
2. Pour CHAQUE finding (A1..A9, M1..M4, B1) : ouvre le(s) fichier(s) cité(s), relis le code
   réel et CONFIRME que le finding tient toujours (le code a pu évoluer). Note tout écart
   entre l'audit et le code actuel. Si un finding n'est plus valide ou est déjà corrigé,
   signale-le et ne le traite pas.
3. Établis une baseline verte AVANT de modifier : compile le firmware
   (skill build-firmware : `pio run -e msp1`, `-e n3pp`, `-e ffp3`) et, côté n3_serveur,
   lance le qa-gate (composer cs:check, analyse, test). Tout doit passer avant de commencer.
4. Ne commence à coder qu'après cette vérification. Regroupe les correctifs par lot cohérent
   et vérifie (build + tests) après chaque lot, pas seulement à la fin.

Développe sur une NOUVELLE branche partant de master dans CHAQUE dépôt concerné
(ex. claude/uploadphotoserver-fixes-<slug>). Ne pousse rien hors de ces branches.

============================================================
DÉCISIONS DÉJÀ TRANCHÉES (ne pas redemander)
============================================================
- A3 (fuseau) : comportement Casablanca +1 h. Mettre GMT_OFFSET_SEC=3600 dans config.h et
  remplacer NTP_TZ_STRING "Africa/Casablanca" (IANA non résolu par newlib) par le TZ POSIX
  "<+01>-1", ou supprimer setenv(TZ) et se fier à l'offset. Vérifier que l'heure affichée par
  la galerie correspond bien à l'heure murale marocaine après correction.
- A2 (contrat serveur) : le firmware annonce le VRAI backlog (cameraSyncPendingCount au
  démarrage du drain) comme `total`. Le serveur n'envoie le mail récap qu'une fois le backlog
  réellement vidé : ajouter un drapeau `final=1` sur l'appel finish quand pending==0 après le
  drain, et côté n3_serveur ne déclencher sendTransferReport que si final (ou received>=total).
  Supprime le spam de récap par réveil. Touche les 2 dépôts + tests serveur.

============================================================
CORRECTIFS À IMPLÉMENTER — TOUS LES POINTS
============================================================
Bugs / robustesse firmware :
- M1 — Fuite framebuffer dans adjustExposure() (camera_setup.cpp:388-404) : séparer les
  gardes, `if (fb) esp_camera_fb_return(fb);` inconditionnel.
- A1 — cameraSyncDrain : plafonner `planned` au réel drainable
  = min(pending, SYNC_DRAIN_MAX_DURATION_MS / SYNC_UPLOAD_MIN_INTERVAL_MS) ; revoir la
  sémantique `complete`/`aborted` pour ne PAS remonter "aborted" sur un simple report au
  réveil suivant (camera_sync.cpp:243-244,318-319). Aligner les constantes config.h:103,106,108.
- A6 — Réserver le numéro de séquence UNE seule fois dans capturePhoto (main.cpp:462 vs 487),
  après persistance/upload confirmé.
- A7 — Faire avancer le curseur NVS aussi sur l'upload direct réussi (sans SD), ou documenter
  clairement pourquoi non ; éviter que pending gonfle indéfiniment.

Mémoire :
- M2 — entries.reserve(SYNC_MAX_BACKLOG_SCAN) dans cameraSyncDrain (camera_sync.cpp:204) et/ou
  abaisser le cap pour limiter DRAM (~19 Ko) et la fragmentation.
- M3 — Réduire la pression DRAM framebuffer+TLS en repli CIF/DRAM sans PSRAM : vérifier/loguer
  la heap avant handshake TLS, et si possible réutiliser un client TLS ou réduire les buffers.
- M4 — Éviter getString() sur corps volumineux et limiter les temporaires String dans les
  chemins chauds (snprintf en buffers pile là où pertinent : main.cpp:490, camera_sync.cpp:539).

Correction / cohérence :
- A3 — cf. décision ci-dessus (config.h:22-24, camera_time.cpp:23-27).
- A9 — Retirer la config morte (SERVER_PORT, WIFI_RECONNECT_INTERVAL_MS) ; ajouter un
  static_assert(sizeof/offsetof) sécurisant le reinterpret_cast WIFI_LIST->N3WifiNetwork
  (main.cpp:127) ; faire que la cadence OTA 2h tienne compte du temps éveillé si trivial.

Contrat serveur :
- A2 — cf. décision ci-dessus (camera_sync.cpp:257 + GallerySyncController + tests n3_serveur).

Sécurité (INCLUS — à traiter, en mode additif/rétro-compatible) :
- A4 — Signer les POST upload/sync/control avec HMAC-SHA256 en plus de la clé API, sur le
  modèle déjà présent dans shared/n3_data (headers X-Sig-Timestamp/X-Sig-Nonce/X-Sig-Hmac,
  ou timestamp+signature). Le firmware ajoute la signature quand API_SIG_SECRET est défini ;
  côté n3_serveur, valider la signature quand présente (SignatureValidator) et retomber sur
  la clé API si absente (rétro-compatibilité totale). camera_uploader.cpp envoie du multipart
  brut : signer un condensé stable (ex. timestamp+nonce+api_key) transporté en headers.
- A5 — Activer le pinning TLS : fournir n3_data_ca_cert.h (CA du serveur, Let's Encrypt/o2switch)
  dans l'include path du firmware pour passer de setInsecure() à setCACert() (mécanisme opt-in
  déjà prévu dans n3_data.cpp:32-45) ; appliquer aussi au client TLS de camera_uploader.cpp:39.
  Passer OTA_METADATA_URL en https:// (config.h:68) pour fermer le downgrade (binaire déjà
  signé sha256+ECDSA). Garder un flag de rollback HTTP documenté.

Qualité image (INCLUS) :
- B1 — adjustExposure() moyenne des octets JPEG (inopérant, camera_setup.cpp:390-392) :
  soit capturer une trame de mesure GRAYSCALE/RGB565 dédiée pour une vraie luminosité, soit
  supprimer ce réglage manuel et s'en remettre à l'AEC matériel OV2640 (déjà réactivé dans
  initializeCamera). Choisir l'option la plus simple et fiable, la documenter.
- A8 — Warm-up/AEC raccourcis (camera_setup.cpp:407-433) : valider ou rétablir des marges
  sûres ; exposer les durées via config.h avec commentaire, pour revenir facilement à
  3×1000 ms si dégradation en transition de luminosité (aube/crépuscule).

============================================================
CONTRAINTES PROJET (OBLIGATOIRES)
============================================================
- Bumper FIRMWARE_VERSION dans uploadphotosserver/include/config.h + historique dans
  uploadphotosserver/VERSION.md (skill bump-firmware-version). Idem versionnage n3_serveur si
  modifié (skill bump-version : VERSION + CHANGELOG.md).
- Compiler les 3 envs firmware (msp1/n3pp/ffp3) et lancer les tests natifs Unity si du code
  shared/ (n3_data, n3_time…) est touché (skill firmware-native-tests).
- Côté n3_serveur : qa-gate complet (cs:check, analyse, test) vert avant PR.
- Vérification comportementale (skill /verify quand pertinent) et une passe /code-review sur le
  diff final.
- Commits clairs et atomiques par lot ; PR draft dans chaque dépôt, avec description reliant
  chaque commit aux IDs de findings (A#/M#/B1). Ne jamais pousser hors des branches de correctifs.

Livrable final attendu : chaque finding de l'audit soit corrigé, soit explicitement marqué
"non reproductible / déjà corrigé" avec justification, dans la description de PR.
```
