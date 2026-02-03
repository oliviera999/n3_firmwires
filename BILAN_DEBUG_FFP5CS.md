# Bilan – Charge des outils de debug (FFP5CS)

Document de synthèse sur la charge (mémoire, CPU, flash, comportement) induite par les outils et le code de debug dans le firmware ESP32 et l’interface web locale.

---

## Résumé

- **Firmware** : Logging série très présent (~858 appels dans 25 fichiers). En production les appels sont neutralisés (NullSerial) ; en test ils s’exécutent et peuvent charger le CPU / saturer le buffer Serial. Routes HTTP `/debug-logs` et `/debug/static-assets` sont toujours compilées. Delay 1 s au boot et écritures NVS périodiques uniquement en profil test.
- **Interface web** : Plusieurs `fetch` vers un serveur d’ingest (127.0.0.1:7243) à chaque événement (WebSocket, dbvars, WiFi). Les appels échouent silencieusement si le serveur n’est pas là.
- **Infrastructure NDJSON** (`debug_log.h`) : présente mais non utilisée dans le code ; si activée, 256 B de buffer par log.

---

## 1. Firmware (ESP32)

### 1.1 Logging série (Serial / LOG_*)

- **Volume** : environ 858 appels `Serial.print` / `Serial.printf` répartis dans 25 fichiers sous `src/`.
- **Production (wroom-prod)** :  
  `Serial` est redirigé vers un stub (`NullSerial` dans `config.h`). Les appels restent dans le binaire mais ne font rien à l’exécution. Coût : un peu de flash (chaînes et code) ; le linker peut en supprimer une partie.
- **Test (wroom-test)** :  
  Serial actif, `LOG_LEVEL=5`, `ENABLE_SERIAL_MONITOR=1`. Tous les logs sont exécutés → charge CPU et temps d’écriture (115200 baud). Risque de blocage si le buffer Serial se remplit.

### 1.2 Fichier `debug_log.h` (NDJSON)

- **État** : le fichier existe dans `include/` mais n’est inclus nulle part dans `src/` ; aucune macro `DEBUG_LOG_*` ni appel `DebugLog::log` dans le firmware.
- **Si on l’activait** : chaque log utiliserait un buffer de 256 octets (`DEBUG_LOG_JSON_BUF_SIZE`) et enverrait une ligne NDJSON sur Serial ou dans un fichier LittleFS.

### 1.3 Lignes [DEBUG] en dur

- **Fichier** : `src/system_boot.cpp` (lignes 151–154).
- **Contenu** : trois appels `Serial.println` / `Serial.printf` autour de `oled.begin()` (avant, résultat, `isPresent()`). Aucun `#if` : le code est toujours compilé. En prod, ce sont des no-op ; en test, trois lignes au boot.

### 1.4 Delay au boot (debug)

- **Fichier** : `src/app.cpp` (lignes 114–117).
- **Condition** : `#if defined(DEBUG_MODE) || !defined(PROFILE_PROD)`.
- **Effet** : `delay(1000)` — une seconde de blocage au démarrage.
- **En pratique** : en wroom-test (sans `PROFILE_PROD`) ce delay est actif à chaque boot. En wroom-prod il est exclu.

### 1.5 Diagnostics (NVS et rapport détaillé)

- **Sauvegarde NVS périodique** (`src/diagnostics.cpp`, vers 250–258) :  
  Garde `#if defined(PROFILE_TEST) || defined(DEBUG_MODE)`. Toutes les 60 secondes, écriture en NVS de `diag_lastUptime` et `diag_lastHeap`. Actif uniquement en profil test (ou si `DEBUG_MODE` est défini). Usure flash limitée mais régulière.
- **Bloc détaillé watchdog / panic** (`src/diagnostics.cpp`, vers 444–518) :  
  Même garde. Ajoute au rapport de diagnostic des lignes détaillées (uptime/heap avant reboot, config watchdog, core dump). Plus de code et d’usage stack en build test ; pas d’impact en prod.

### 1.6 Routes HTTP « debug »

- **GET /debug-logs** (`src/web_routes_status.cpp`, `registerDebugLogs`) :  
  Construit un JSON avec état système (uptime, heap, WiFi, WebSocket, capteurs, actionneurs). Utilise un `StaticJsonDocument<JSON_DOCUMENT_SIZE>` (4096 B en prod, 1024 B en test pour WROOM) sur la stack du handler, plus `ensureHeapForRoute` et `beginResponseStream`. Pas de `#if PROFILE_TEST` : la route est enregistrée en prod et en test. Charge uniquement à l’appel.
- **GET /debug/static-assets** (`src/web_routes_ui.cpp`, vers 129–153) :  
  Répond avec un JSON (sessionId, runId, hypothesisId, usage LittleFS, présence de fichiers statiques). `StaticJsonDocument<512>` et une douzaine de `LittleFS.exists()`. Toujours compilée et enregistrée ; charge à l’appel.

### 1.7 Mailer

- **Fichier** : `src/mailer.cpp` (lignes 822–823).
- **Code** : `_smtp.debug(1);` sans condition — active les logs SMTP détaillés de la librairie. En prod, la sortie va vers `Serial` (NullSerial), donc aucun effet visible. En test, trafic Serial supplémentaire à chaque envoi SMTP.

### 1.8 Autres branches PROFILE_TEST / PROFILE_DEV

- **web_server.cpp** : routes ou branches (ex. `/wifi/status`, `/server-status`) réservées au profil test/dev.
- **app_tasks.cpp** : logs HWM de stack (si `FEATURE_DIAG_STACK_LOGS`).
- **automatism_sync.cpp**, **automatism_sleep.cpp** : branches conditionnelles pour le debug.
- **tls_mutex.h** : implémentation « debug » des mutex selon le profil.

Ces parties n’impactent le runtime en production que si du code commun les appelle ; en général elles ne s’exécutent pas en prod.

---

## 2. Interface web locale (`data/`)

### 2.1 Instrumentation « agent » (fetch ingest)

- **Fichiers** : `data/shared/websocket.js`, `data/shared/common.js`.
- **Comportement** : des appels `fetch` vers `http://127.0.0.1:7243/ingest/...` sont placés dans des régions `// #region agent log`. Ils sont déclenchés à chaque événement concerné : réception d’un message WebSocket, affichage/chargement/soumission des dbvars, mise à jour WiFi (connecté / déconnecté / autre).
- **Charge** : si le serveur d’ingest n’est pas lancé (cas normal hors session Cursor), les requêtes échouent et sont ignorées (`.catch(()=>{})`). Pas de crash, mais requêtes inutiles et bruit dans l’onglet réseau du navigateur.

---

## 3. Tableau récapitulatif (Production vs Test)

| Élément | Production (wroom-prod) | Test (wroom-test) |
|--------|--------------------------|--------------------|
| Serial | No-op (NullSerial) | Tous les logs exécutés ; risque saturation buffer |
| Flash (code) | Routes `/debug-logs` et `/debug/static-assets` + code LOG présents | Idem + branches PROFILE_TEST (diagnostics, NVS diag, etc.) |
| RAM (runtime) | Pas de buffer NDJSON (non utilisé). Handlers : 4 KB + 512 B JSON à chaque appel des routes debug | Idem |
| Delay 1 s au boot | Désactivé (PROFILE_PROD) | Actif |
| NVS (diag) | Pas d’écriture périodique diag | Écriture toutes les 60 s (diag_lastUptime, diag_lastHeap) |
| SMTP debug | No-op (Serial = NullSerial) | Logs SMTP détaillés sur Serial |
| Web (navigateur) | Fetch ingest à chaque événement (échouent silencieusement) | Idem |

---

## 4. Synthèse des coûts principaux

1. **Profil test** : delay 1 s au boot, écritures NVS toutes les 60 s, trafic Serial important (LOG + SMTP debug).
2. **Toutes cibles** : routes `/debug-logs` et `/debug/static-assets` toujours compilées et enregistrées ; à chaque appel, heap temporaire (ordre de 4 KB + 512 B) dans le handler.
3. **Front** : requêtes fetch ingest à chaque événement (WS, dbvars, WiFi), même sans serveur d’ingest.

Ce document peut être mis à jour à chaque changement significatif des outils ou du code de debug.

---

## 5. Résumé risques – Ce qui peut poser problème

| Risque | Sévérité | Contexte | Action possible |
|--------|----------|----------|------------------|
| **Saturation buffer Serial** | Moyen | wroom-test uniquement : si les logs s’enchaînent trop vite (ex. boucle, capteurs + SMTP + WebSocket), le buffer Serial (typ. 256 B) peut se remplir et **bloquer** le thread qui appelle `Serial.print`. Règle projet : ne pas bloquer > 3 s. | Réduire `LOG_LEVEL` en test, ou limiter les logs dans les chemins chauds. |
| **Delay 1 s au boot** | Faible | wroom-test : blocage **volontaire** 1 s au démarrage. Peut masquer un problème de timing (ex. watchdog au boot) ou gêner les tests rapides. | Accepter en test ; en prod déjà désactivé. |
| **Écritures NVS toutes les 60 s** | Faible | wroom-test : 2 clés NVS (`diag_lastUptime`, `diag_lastHeap`) écrites chaque minute → **usure flash** limitée mais continue. NVS supporte ~100k cycles par secteur. | OK pour du test court ; en run 24/7 test, envisager un intervalle plus long (ex. 5–10 min) ou désactiver. |
| **Routes /debug-logs et /debug/static-assets en prod** | Faible | Exposées sans auth. Donnent état système, capteurs, WiFi, présence de fichiers. **Surface d’attaque** et **fuite d’infos** si l’ESP est accessible sur le LAN. | Protéger par `#if PROFILE_TEST` ou supprimer en prod si pas nécessaires. |
| **Stack handler /debug-logs** | Faible | 4 KB (prod) ou 1 KB (test) de `StaticJsonDocument` sur la stack du handler. webTask a 8 KB ; si d’autres allocations stack sont importantes dans le même chemin, **stack overflow** possible. | Déjà un risque identifié dans le projet (HWM webTask) ; surveiller si on ajoute du code dans ce handler. |
| **Fetch ingest (web)** | Très faible | Requêtes vers 127.0.0.1 qui échouent → pas de crash, pas d’impact fonctionnel. Léger **bruit réseau** et **clutter** dans l’onglet Network. | Retirer les régions « agent log » une fois le debug terminé pour garder le front propre. |

**À retenir**

- **Production** : pas de blocage ni d’écriture NVS diag ; les seuls points à surveiller sont l’exposition des routes debug (sécurité) et la taille flash (déjà tendue sur wroom-prod).
- **Test** : le seul risque **opérationnel** notable est le blocage possible par saturation du buffer Serial en cas de logs très fréquents ; le reste est coût ou usure limitée.
