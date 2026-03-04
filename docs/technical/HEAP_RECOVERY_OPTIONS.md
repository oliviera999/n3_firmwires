# Options de récupération heap (ESP32-WROOM)

Ce document décrit le constat observé sur un run 2 h (log du 2026-02-23) et les pistes pour récupérer davantage de heap sur WROOM. Voir aussi [ANALYSE_ORIGINE_PROBLEMES_CRITIQUES.md](../reports/monitoring/reports/ANALYSE_ORIGINE_PROBLEMES_CRITIQUES.md) (origine problèmes DHT22, watchdog, mémoire).

## Constat

- **Minimum heap observé** : 33 168 octets (~32 KB), atteint pendant le premier TLS (GET config / POST) au boot.
- **Heap libre ensuite** : stable ~93 KB sur 2 h ; plus grand bloc contigu ~30 708 octets ; pas de fuite.
- **Seuil TLS** : `TLS_MIN_HEAP_BYTES` = 35 KB (`include/config.h`, `HeapConfig::MIN_HEAP_FOR_TLS`). En dessous, les connexions TLS sont refusées (`tls_mutex.h`, `TLSMutex::acquire()`).
- Les allocations mbedTLS/HTTP au premier handshake sont en grande partie internes (stack netTask + heap temporaire). La netTask utilise déjà une **stack statique** (`netTaskStack[]` dans `app_tasks.cpp`), ce qui évite un gros bloc heap pour la stack.
- Sur WROOM, les buffers sont déjà réduits dans `include/config.h` (`BufferConfig` : HTTP 1024, JSON 1024/2048, etc.) ; réduire encore risque troncature ou `IncompleteInput` (voir commentaires dans config.h).

## Pistes pour récupérer du heap

| Piste | Description | Risque / contrainte |
|-------|-------------|----------------------|
| **A. Délai du premier TLS** | Attendre 2–3 s après « réseau prêt » avant d’appeler `tryFetchConfigFromServer()` dans netTask (boot). Permet à des allocations de boot (logs, init) de se libérer avant le pic TLS. **Implémenté** : `FIRST_TLS_DELAY_MS = 2000` dans `src/app_tasks.cpp` (BOARD_WROOM uniquement). | Délai de config distante au boot ; valider sur wroom-test (monitor 5–10 min, config distante appliquée après 2–3 s). |
| **B. Ne pas réduire netTask stack** | Garder `NET_TASK_STACK_SIZE = 12288` ; HWM log 2 h ~6936, marge déjà serrée pour OTA/TLS. | Réduire libérerait 2 KB mais risque stack overflow (doc OTA/WDT). |
| **C. Réduire tailles JSON / buffers** | Réduire encore `JSON_DOCUMENT_SIZE` ou `OUTPUTS_STATE_READ_BUFFER_SIZE` sur WROOM. | Déjà à 1024/2048 ; risque troncature/IncompleteInput (voir config.h). À n’envisager qu’avec tests de non-régression (GET outputs/state, /dbvars). |
| **D. Réserve mail (s_mailReserve)** | Déjà utilisée pour éviter fragmentation lors de l’envoi mail (bloc contigu). Pas de réduction sans risque pour SMTP. | Laisser tel quel. |
| **E. Vérifier buffers HTTP internes** | Les tailles `HTTP_BUFFER_SIZE` / `HTTP_TX_BUFFER_SIZE` (1024 sur WROOM) sont déjà basses. Vérifier dans `web_client` / stack WiFi qu’aucun buffer plus gros n’est alloué en dur. | Audit rapide ; gain incertain. |

## Recommandation

1. **Diagnostic (Partie 1)** : CRIT uniquement quand le **heap libre actuel** est sous 35 KB ; pour le min historique sous 35 KB, log **une seule fois** par boot (WARN). C’est implémenté dans `src/app_tasks.cpp` (bloc `FEATURE_DIAG_STATS`).
2. **Récupération heap** : la **piste A** (délai 2 s avant premier TLS sur WROOM) est implémentée dans `src/app_tasks.cpp` (constante `FIRST_TLS_DELAY_MS`). Valider sur wroom-test (monitor 5–10 min, vérifier que la config distante est bien appliquée après 2–3 s). Les pistes B–E sont documentées pour référence ; ne pas réduire netTask stack (B) ni les buffers JSON (C) sans tests de non-régression.

Aucun changement aux seuils 35 KB ni au comportement de `tls_mutex.h` ou des routes web.
