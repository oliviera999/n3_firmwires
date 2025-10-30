# Cartographie `ota_manager.cpp` - etat au 2025-10-29

## Vue d'ensemble

- Gestion centralisee des mises a jour OTA: telechargement metadonnees JSON,
  selection artefacts firmware/filesystem, validation capacite, ecriture partitions
  et mise a jour LittleFS.
- Combine plusieurs APIs HTTP (`esp_http_client`, `HTTPClient`, `HTTPUpdate`) avec
  fallbacks multi-niveaux, verification MD5 optionnelle, checkpoints watchdog et
  journaux avances (`EventLog`).
- Expose callbacks (`setStatusCallback`, `setProgressCallback`, `setErrorCallback`)
  pour synchroniser l'interface locale (OLED, Web) et notifier l'automatisme.
- Maintient un etat long terme: version courante, version distante, URLs retenues,
  tailles attendues, flags de progression, timestamps de dernier check.

## Pipeline OTA

- `checkForUpdate()` declenche le telechargement des metadonnees (JSON) depuis
  l'URL dynamique (`OTAConfig::getMetadataUrl`), valides le schema (legacy vs
  `channels`) et selectionne firmware/filesystem selon profil (prod/test) et modele
  (`esp32-wroom`, `esp32-s3`).
- `performUpdate()` enchaine telechargement firmware puis filesystem (si definis),
  applique `validateSpace`, `validate*Size`, gere les callbacks et, en cas de succe,
  programme `ESP.restart()`.
- `updateTask(void*)` sert de tache FreeRTOS pour lancer l'update sans bloquer la
  boucle principale; elle surveille le watchdog (`esp_task_wdt_reset`) et manipule
  `m_isUpdating` pour bloquer les checks concurrents.

## Telechargement et ecriture

- Trois strategies firmware: `downloadFirmwareModern` (esp_http_client, buffers
  larges, keep-alive), `downloadFirmware` (HTTPClient classique),
  `downloadFirmwareUltraRevolutionary` (HTTPUpdate avec fallback legacy). Les flux
  ecrivent par chunks vers `Update.write()` et journalisent progression (10 %).
- `downloadFilesystem()` recupere le binaire SPIFFS/LittleFS, valide la taille et
  l'applique via `Update.begin(OTA_PARTITION_TYPE_DATA)` ou via `LittleFS.begin()`.
- Reessaies limites: pas de strategie centralisee, chaque methode gere ses propres
  retours d'erreur avec fallback immediate et logs.

## Etats et callbacks

- `m_statusCallback` recupere les messages utilisateur (`[OTA] ...`), `m_progressCallback`
  revoie un pourcentage entier, `m_errorCallback` signale les echecs (utilise par
  WebSocket/OLED).
- Flags internes: `m_isUpdating` verrouille, `m_updateTaskHandle` suit la tache
  en cours, `m_httpClient` conserve la derniere session `esp_http_client` pour
  reinit rapide.
- `setCheckInterval`, `setCurrentVersion` configurent la frequence de check et
  synchronisent l'interface (Serveur HTTP, WebSocket).

## Dependances cles

- Pile reseau: `WiFi`, `HTTPClient`, `esp_http_client`, `HTTPUpdate`, `NetworkConfig`.
- OTA core: `Update`, `esp_ota_ops`, `esp_partition`, `esp_task_wdt`.
- Integration projet: `project_config.h` (`OTAConfig`, `TimingConfig`), `Mailer`
  (notifications), `Automatism` (arm Mail blink), `EventLog`, `TaskMonitor`.
- Gestion JSON: `ArduinoJson` pour Parsing metadonnees.

## Points de fragilite

- Taille (>1 300 lignes) et duplication: trois implems telechargement, validations
  reparties, nombreuses branches conditionnelles pour `OTA_UNSAFE_FORCE`.
- Couplage fort au reseau: chaque methode verifie WiFi, logs verbeux, manipule
  directement `Serial`, `EventLog`, `Mailer` et `Automatism`.
- Usage massif de `String` (payloads, logs) et buffers statiques multiples (1 KB
  pour lecture, 4-8 KB pour MD5) pouvant fragmenter la heap pendant l'update.
- Aucune supervision unifiee des fallbacks: reessais limites, absence de backoff ou
  de compteur global d'echec; risque de boucle lors de pannes reseau.
- `updateTask` mixe logique metier (checks, reset, log) et pilotage FreeRTOS; la
  liberation des ressources (`esp_http_client_cleanup`, `vTaskDelete`) se repete.

## Pistes de refactorisation

- Introduire un `OTAService` decouplé qui expose un plan d'execution (etapes,
  diagnostics) et encapsule l'etat (artefacts, versions) dans une structure
  testable hors ESP (mock HTTP).
- Mutualiser les telechargements: creer un adaptateur `HttpDownloader` configurant
  les timeouts/buffers une seule fois et fournissant flux `Stream` pour `Update`.
- Extraire un `MetadataResolver` pour parse/validation JSON (`channels`, fallback,
  compat) et renvoyer un objet `OtaPlan`.
- Centraliser callbacks + journalisation dans une petite classe RAII afin de
  retirer le code repetitif (`log`, `logError`, `logProgress`).
- Ajouter un orchestrateur de reessai/backoff avec compteurs max et
  notifications, ce qui evite les boucles `while` direction reseau.
- Remplacer `String` par buffers `char[]` partages pour les logs critiques, ou
  utiliser `Print`/`StaticJsonDocument` pour construire les messages sans heap.




