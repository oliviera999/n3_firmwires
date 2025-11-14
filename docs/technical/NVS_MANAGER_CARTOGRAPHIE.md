# Cartographie `nvs_manager.cpp` - etat au 2025-10-29

## Vue d'ensemble

- Gestionnaire NVS centralise qui encapsule mutex recursif, cache cle/valeur,
  validations, statistiques et operations de maintenance (cleanup, migration,
  rotation logs).
- Fournit une API typed pour `String`, `bool`, `int`, `float`, `unsigned long`,
  JSON compresse, ainsi que des utilitaires (`keyExists`, `removeKey`,
  `clearNamespace`, `printNamespaceContents`).
- Maintient une carte de clefs `dirty` pour la fonction de flush differe, avec
  intervalle configurables et nettoyage periodique automatique.
- Stabilise les acces via `NVSLockGuard` et `xSemaphoreCreateRecursiveMutex` afin
  d'eviter les corruptions dues aux acces concurrents.

## Architecture interne

- Namespaces centralises (`sys`, `cfg`, `time`, `state`, `logs`, `sens`) crees au
  boot par `begin()`. `openNamespace/closeNamespace` gerent le handle courant.
- `validateKey`, `validateValue`, `calculateChecksum` servent a controler la
  validite des donnees avant enregistrement et a generer des checksums stockes
  dans un cache interne.
- Les methodes `save*` et `load*` utilisent le cache pour eviter les lectures
  repetitives; elles journalisent via `Serial` et retournent `NVSError` explicite.
- `saveJsonCompressed` et `loadJsonDecompressed` utilisent `compressJson` /
  `decompressJson` avec base64 et deflate simplifie.
- `schedulePeriodicCleanup()` et `shouldPerformCleanup()` declenchent des taches
  d'entretien (suppression cle obsolete, rotation logs) sur base de timestamps.

## Dependances cles

- APIs ESP-IDF: `nvs.h`, `nvs_flash.h`, `esp_system.h`.
- FreeRTOS: `xSemaphoreCreateRecursiveMutex`, `xSemaphoreTakeRecursive`,
  `TickType_t` pour le verrouillage.
- Projet: `project_config.h` (seuils/constantes), `EventLog` (via logs), modules
  consommant ce service (`ConfigManager`, `AutomatismPersistence`, `Automatism`).
- Utilitaires: `String`, container cache interne (std::vector + struct).

## Points de fragilite

- Taille et densite (>1 300 lignes) avec nombreuses branches erreur; difficile a
  tester unitairement car appels directs aux APIs ESP et `Serial` dans toute
  l'implementation.
- Couplage implicite: plusieurs modules accedent directement a `g_nvsManager`, ce
  qui rend l'injection de dependance et le test difficile.
- Usage intensif de `String` pour les valeurs et la compression JSON qui fragmente
  la heap, surtout lors du doublement compress/decompress.
- Cache maison (vecteur) sans eviction LRU et sans verification de coherence quand
  le namespace est modifie ailleurs; risque de stale data.
- Nettoyage/maintenance (`cleanupCorruptedData`, `migrateFromOldSystem`) melange
  logique applicative et diagnostique console dans les memes methodes.
- `flushCache` depend de timers internes et d'une tache externe appelant
  `checkDeferredFlush`; un oubli d'appel peut laisser des donnees non synchronisees.

## Pistes de refactorisation

- Extraire des classes `NvsNamespace`, `NvsCache`, `NvsMaintenance` pour separer
  les responsabilites et faciliter les tests (mock nvs_handle_t).
- Introduire des wrappers RAII pour `nvs_handle` et centraliser la journalisation
  dans un helper (formatage, severity) afin d'eviter la duplication `Serial.printf`.
- Remplacer le cache vector par une structure hash (ou abriter dans `std::unordered_map`)
  avec politique de coherence (versioning/checksum) et eviction configurable.
- Fournir une interface pure pour injection (ex: `INVSDriver`) que `ConfigManager`
  peut consommer, isolant `String` dans la couche frontale.
- Isoler la logique de compression JSON dans un module dedie (utilisable aussi par
  `AutomatismNetwork`) pour reduire la duplication et permettre des tests PC.
- Formaliser un scheduler (timer ou tache) pour la maintenance et le flush differe,
  au lieu de compter sur des appels opportunistes a `checkDeferredFlush()`.











