# Tests de latence – serveur distant iot.olution.info

**Date des tests :** 2026-02-15  
**Objectif :** Comprendre pourquoi les POST depuis l’ESP32 durent ~18–21 s alors que le même serveur répond en < 0,2 s depuis un PC (curl). Documentation pour usage ultérieur (diagnostic, comparaison, évolution).

---

## 1. Contexte

- **ESP32** (wroom-test) : POST vers `http://iot.olution.info/ffp3/post-data-test` avec payload ~480–504 octets (données capteurs + config). Log firmware : `[HTTP] Requête: 18865 ms, code=200` à 21 s typiquement.
- **Question :** La latence est-elle côté réseau (chemin ESP32 → serveur) ou côté serveur (traitement PHP/BDD pour un POST valide) ?

---

## 2. Tests effectués (depuis PC, même réseau que possible)

### 2.1 Ping (ICMP)

```text
ping -n 5 iot.olution.info
```

**Résultat :** 5 envoyés, 0 reçus, **100 % perte** (timeout).

**Interprétation :** L’hôte ou un équipement sur le chemin bloque ou ne répond pas à l’ICMP. **Cela n’implique pas que le serveur soit inaccessible** : le trafic HTTP (TCP) peut être autorisé. Ne pas utiliser le ping pour conclure à un problème de disponibilité.

---

### 2.2 GET outputs/state (curl)

**Commande :**
```bash
curl -s -w "time_total: %{time_total}s\ncode: %{http_code}\n" -o NUL "http://iot.olution.info/ffp3/api/outputs-test/state"
```

**Résultat typique :** `time_total: 0.19s`, `code: 200`.

**Détail des temps (une mesure) :**
| Étape | Valeur |
|-------|--------|
| namelookup (DNS) | 8 ms |
| connect (TCP) | 73 ms |
| starttransfer (TTFB) | 170 ms |
| total | 170 ms |

---

### 2.3 POST post-data-test (clé invalide → 401)

**Commande :**
```bash
curl -s -w "time_total: %{time_total}s\ncode: %{http_code}\n" -o NUL -X POST "http://iot.olution.info/ffp3/post-data-test" --data "api_key=test&sensor=test&version=test"
```

**Résultat typique :** `time_total: 0.16s`, `code: 401`.

**Détail des temps :**
| Étape | Valeur |
|-------|--------|
| namelookup | 7 ms |
| connect | 81 ms |
| starttransfer | 163 ms |
| total | 166 ms |

Le serveur rejette la requête (clé invalide) **avant** l’insertion BDD et la sync outputs. Le temps mesuré reflète donc : DNS + TCP + traitement PHP jusqu’à la vérification de la clé API.

---

### 2.4 POST avec payload type ESP32 (~471 octets, clé invalide → 401)

**Objectif :** Vérifier si la taille du body (proche du POST réel) change la latence.

**Résultat :** total ~164 ms, code 401. Pas de dégradation par rapport au POST court.

**Conclusion :** La taille du payload (≈500 octets) n’explique pas les 18–21 s observés côté ESP32.

---

### 2.5 Variance (répétitions)

- **3 × GET** : 152 ms, 155 ms, 158 ms.
- **3 × POST 401** : 158 ms, 159 ms, 167 ms.
- **5 × GET** (espacés de 2 s) : 156 ms, 158 ms, 235 ms, 163 ms, 157 ms.

Les temps restent dans une fourchette 0,15–0,24 s depuis le PC. Aucun pic de type 18–20 s.

---

## 3. Synthèse des mesures (depuis PC)

| Test | URL / méthode | Payload | Code | Total typique |
|------|----------------|---------|------|----------------|
| GET | /ffp3/api/outputs-test/state | - | 200 | 0,15–0,22 s |
| POST | /ffp3/post-data-test | court (invalid) | 401 | 0,15–0,17 s |
| POST | /ffp3/post-data-test | ~471 B (invalid) | 401 | ~0,16 s |

**Constats :**

- Depuis le PC : DNS + connexion TCP + réponse serveur restent faibles (< 0,2 s).
- POST avec body type ESP32 mais clé invalide : réponse tout aussi rapide (401).
- Donc **depuis cette machine**, le serveur et le chemin réseau sont réactifs pour GET et pour POST rejeté (401).

---

## 4. Endpoint ping, optimisations réponse POST et test n3.olution.info

### 4.1 Endpoint /ffp3/ping (diagnostic latence)

Un endpoint **GET et POST** `/ffp3/ping` répond par le corps **OK** (2 octets), avec **Content-Length** et **Connection: close**, sans BDD ni log. Il sert à mesurer le RTT minimal vers le même hôte.

**Depuis le PC (même WiFi que l’ESP32 si possible) :**
```bash
curl -s -w "time_namelookup: %{time_namelookup}s\ntime_connect: %{time_connect}s\ntime_starttransfer: %{time_starttransfer}s\ntime_total: %{time_total}s\ncode: %{http_code}\n" -o NUL "http://iot.olution.info/ffp3/ping"
curl -s -w "time_total: %{time_total}s\ncode: %{http_code}\n" -o NUL -X POST "http://iot.olution.info/ffp3/ping" -d ""
```

**Interprétation :** Si le **ping** depuis l’ESP32 (ou depuis le même WiFi) est aussi ~18 s alors que **post-data** est ~18 s, le délai est en amont du PHP (réseau/proxy). Si le **ping** est rapide et **post-data** lent, le délai serait lié au POST ou au traitement (peu probable vu les logs serveur).

### 4.2 Optimisations réponse POST (o2switch / proxy)

La réponse 200 du **PostDataController** utilise désormais **ResponseHelper::textClose()** : en-têtes **Content-Length** et **Connection: close**. Objectif : limiter tout buffer proxy et signaler la fin de la réponse. Fichier : `ffp3/src/Controller/PostDataController.php` ; helper : `ffp3/src/Util/ResponseHelper.php` (méthode `textClose`).

### 4.3 Comparaison avec n3.olution.info (sous-domaine)

Pour tester si la latence est spécifique au domaine **iot.olution.info** ou générale au chemin réseau :

1. **Déployer l’endpoint minimal sur n3**  
   Copier le fichier **`ffp3/tools/ping_standalone.php`** à la racine web de **n3.olution.info** (ex. `public/ping.php`), puis appeler :
   - `http://n3.olution.info/ping.php` (GET ou POST).

2. **Comparer depuis le même réseau (curl)**  
   - `curl -s -w "%{time_total}s\n" -o NUL "http://iot.olution.info/ffp3/ping"`  
   - `curl -s -w "%{time_total}s\n" -o NUL "http://n3.olution.info/ping.php"`  
   Si **n3** est rapide et **iot** lent → problème spécifique à iot (vhost, proxy, DNS). Si les deux sont lents → chemin réseau (box, opérateur).

3. **Comparer depuis l’ESP32**  
   Modifier temporairement l’URL de test (ou ajouter un appel GET vers n3 dans le firmware) et comparer les durées loggées. Si n3 est rapide depuis l’ESP32, confirmer la piste « spécifique à iot.olution.info » côté hébergeur (o2switch).

---

## 5. Hypothèses pour la latence ESP32 (18–21 s)

Deux causes possibles (non exclusives) :

1. **Chemin réseau de l’ESP32**  
   WiFi → box → opérateur → hébergeur. Latence ou pertes plus élevées que depuis le PC (autre interface, autre route, ou contraintes WiFi).

2. **Traitement serveur pour POST valide**  
   Quand `api_key` est valide, le contrôleur fait : construction de `SensorData`, `sensorRepo->insert($data)`, `outputRepo->syncStatesFromSensorData($data)`, invalidation du cache. Si cette chaîne (PHP + BDD + sync) est lente, le temps de réponse peut atteindre 18–20 s alors qu’un 401 est renvoyé en < 0,2 s.

**Pour trancher :** mesurer côté serveur le temps passé dans `PostDataController::handle()` pour un POST **valide** (par ex. microtime avant/après insert et avant/après sync), ou exécuter un curl POST **avec clé valide** depuis le serveur (localhost) et mesurer le total.

---

## 6. Commandes utiles (dont ping) pour rejouer les tests

Tout depuis un shell (PowerShell ou bash). Remplacer `NUL` par `/dev/null` sous Linux/macOS si besoin.

**Détail des temps (GET) :**
```bash
curl -s -w "namelookup: %{time_namelookup}s  connect: %{time_connect}s  starttransfer: %{time_starttransfer}s  total: %{time_total}s  code: %{http_code}\n" -o NUL "http://iot.olution.info/ffp3/api/outputs-test/state"
```

**Détail des temps (POST 401) :**
```bash
curl -s -w "namelookup: %{time_namelookup}s  connect: %{time_connect}s  starttransfer: %{time_starttransfer}s  total: %{time_total}s  code: %{http_code}\n" -o NUL -X POST "http://iot.olution.info/ffp3/post-data-test" --data "api_key=x&sensor=x&version=x"
```

**Endpoint ping (diagnostic latence, section 4) :**
```bash
curl -s -w "total: %{time_total}s  code: %{http_code}\n" -o NUL "http://iot.olution.info/ffp3/ping"
curl -s -w "total: %{time_total}s  code: %{http_code}\n" -o NUL -X POST "http://iot.olution.info/ffp3/ping" -d ""
```

**Variance (3 GET) :**
```bash
for i in 1 2 3; do curl -s -w "  run $i : %{time_total}s | %{http_code}\n" -o NUL "http://iot.olution.info/ffp3/api/outputs-test/state"; done
```

**Ping (souvent sans réponse) :**
```bash
ping -n 5 iot.olution.info
```

---

## 7. Instrumentation serveur (PostDataController) – en place

Dans `ffp3/src/Controller/PostDataController.php`, après validation de la clé API et construction de `SensorData` :

- **Mesures** : `microtime(true)` avant et après chaque étape (insert, sync outputs, invalidate cache, update lastRequest). Durées en ms loggées dans le message.
- **Log** : à chaque POST valide réussi, une ligne du type :
  - `PostData OK sensor=... version=... timing_ms: insert=X sync=Y cache=Z board=W total=T`
- **Fichier de log** : celui configuré pour l’app (par défaut `cronlog.txt`, ou `LOG_FILE_PATH` dans `.env`).

**Interprétation :**

- Si `total` est faible (< 1 s) alors que l’ESP32 mesure 18–21 s → la latence est **réseau** (chemin ESP32 → serveur).
- Si `total` est élevé (plusieurs secondes) → regarder `insert` vs `sync` : l’un des deux (ou la BDD) est le goulot.
- `cache` et `board` sont en général < 1 ms ; un pic sur l’un d’eux indiquerait un problème ciblé.

**Autre modification :** `set_time_limit(30)` (au lieu de 10 s) pour éviter qu’PHP coupe le script avant que le client ESP32 (timeout 18–26 s) reçoive la réponse.

---

## 8. Ce que mesure le firmware (durée POST)

La durée affichée dans le log série (`[HTTP] Requête: XXXXX ms, code=...`) est mesurée dans **`src/web_client.cpp`** :

- **Début** : entrée dans `httpRequest()` (avant délai inter-requêtes éventuel, `begin()`, envoi du body).
- **Fin** : sortie de `httpRequest()` après réception et lecture de la réponse (ou échec/timeout).

Elle inclut donc : délai inter-requêtes (si `MIN_DELAY_BETWEEN_REQUESTS_MS`), résolution DNS (si l’URL est en nom d’hôte), connexion TCP, envoi du body, attente de la réponse serveur, lecture du body. **Ce n’est pas** uniquement le temps de traitement serveur (celui-ci est mesuré côté serveur, voir section 6).

Depuis la v11.xxx le log inclut **`connect_ms`**, **`request_ms`** et **`rssi`** :
- **connect_ms** : temps jusqu’à `begin()` réussi (délai inter-requêtes + DNS + TCP). Si ~18 s → goulot DNS ou connexion.
- **request_ms** : temps restant (envoi body + serveur + réception). Si ~18 s → goulot upload ou download.
- **rssi** : dBm (voir section 9).

**Environnement test** : par défaut l’URL utilise le hostname (`BASE_URL`). Sur **hébergement mutualisé**, l’accès par IP renvoie en général **403** ou **404** (config vhost non modifiable) ; on ne peut donc pas utiliser l’IP pour contourner le DNS. Sur un VPS, la section 10 décrit comment accepter l’IP. La constante `BASE_URL_TEST_IP` (109.234.162.74) reste en config pour référence / test manuel.

**Serveur** : au début de chaque requête POST, le cronlog enregistre `PostData request received at=... ts=...` (heure de réception par PHP). Comparer avec l’heure d’envoi côté ESP32 pour estimer le délai upload.

---

## 9. Test depuis le même WiFi que l’ESP32

Pour comparer la latence perçue par l’ESP32 avec celle d’un client sur le **même réseau WiFi** :

**POST avec clé valide (réponse 200, même chemin que l’ESP32) :**
```bash
curl -s -w "namelookup: %{time_namelookup}s  connect: %{time_connect}s  starttransfer: %{time_starttransfer}s  total: %{time_total}s  code: %{http_code}\n" -o NUL -X POST "http://iot.olution.info/ffp3/post-data-test" --data "api_key=VOTRE_CLE&sensor=test-pc&version=1&TempAir=20&Humidite=50&TempEau=22"
```
Remplacer `VOTRE_CLE` par la clé API configurée sur le serveur (celle de l’ESP32). Si le **total** reste faible (< 1 s) depuis ce PC alors que l’ESP32 voit 18–21 s, la latence est spécifique au chemin ou au client ESP32 (WiFi, stack TCP, etc.).

**Interprétation :**
- Même WiFi, PC rapide et ESP32 lent → problème côté ESP32 (WiFi/CPU/DNS) ou chemin sortant de la box différent pour l’ESP32.
- Même WiFi, les deux lents → problème réseau local ou opérateur.

---

## 10. RSSI et qualité WiFi (log firmware)

Le log `[HTTP] Requête: ... connect_ms=... request_ms=... rssi=X` affiche le RSSI en dBm au moment du log. Ordres de grandeur utiles :

| RSSI (dBm) | Qualité typique |
|------------|------------------|
| ≥ -50      | Excellente       |
| -50 à -70  | Bonne            |
| -70 à -85  | Acceptable       |
| -85 à -100 | Faible           |
| < -100     | Très faible      |

Un RSSI faible (< -85) peut expliquer des temps de réponse élevés ou des timeouts. Les seuils utilisés par le firmware (reconnexion, modem sleep) sont dans `include/config.h` / sleep (ex. `WIFI_RSSI_CRITICAL`, `WIFI_RSSI_POOR`).

---

## 11. Pistes pour réduire la latence (sans accès au serveur)

Sur **hébergement mutualisé**, on ne peut pas utiliser l’IP. Voici des pistes côté client / réseau :

1. **DNS plus rapide**  
   Le goulot peut être la résolution de `iot.olution.info` par le DNS de la box. Détail des options :
   - **DNS sur la box** : configurer des serveurs DNS personnalisés dans la box (DHCP) ; tous les appareils recevant une IP par DHCP, dont l’ESP32, utiliseront ces DNS. Voir [§ 10.1](#101-dns-sur-la-box-détail) pour le détail par opérateur.
   - **ESP32** : l’API Arduino-ESP32 ne propose pas de `setDNS()` après connexion DHCP ; on ne peut pas forcer un DNS côté firmware sans passer en IP statique + `WiFi.config(ip, gw, mask, dns1, dns2)`.

### 11.1 DNS sur la box (détail)

**Principe**  
L’ESP32 se connecte en DHCP ; il reçoit son adresse IP, la passerelle et les **serveurs DNS** via l’option DHCP 6. Si la box utilise des DNS lents ou surchargés, la résolution de `iot.olution.info` peut prendre plusieurs secondes. En configurant des DNS plus réactifs **dans la box**, celle-ci les enverra à tous les clients DHCP, dont l’ESP32, sans modifier le firmware.

**Adresses DNS courantes (à saisir dans la box)**  
- Google : **8.8.8.8**, **8.8.4.4**  
- Cloudflare : **1.1.1.1**, **1.0.0.1**  
- Quad9 : **9.9.9.9**, **149.112.112.112**

**Comment faire selon l’opérateur**

| Opérateur | Box | Accès / chemin | Notes |
|-----------|-----|----------------|-------|
| **Free** | Freebox | **mafreebox.freebox.fr** → Paramètres → **Mode avancé** → **Serveur DHCP**. Décocher « Utiliser les DNS de Free », saisir DNS 1 et DNS 2 (ex. 8.8.8.8, 8.8.4.4). | Redémarrer la Freebox pour appliquer. Les clients gardent l’ancien DNS jusqu’au renouvellement du bail ; redémarrer l’ESP32 ou attendre. |
| **Bouygues** | BBox | **mabbox.bytel.fr** → **DHCP** / **Options DHCP**. Saisir DNS primaire et secondaire. Certaines BBox imposent une règle (redirection port 53) si la box force ses DNS ; voir doc ou support. | Redémarrage box ou attente ~2 min selon version. |
| **SFR** | Box 7 / NB6 | **http://192.168.1.1** (admin / mot de passe WiFi) → **Réseau V4** → **DHCP**. Renseigner les serveurs DNS personnalisés si l’interface le propose. | Désactiver complètement le DHCP peut couper la TV ; ne modifier que les DNS si possible. |
| **Orange** | Livebox | **Limitation** : Livebox 4 et suivantes n’exposent souvent pas le réglage DNS, ou imposent les DNS Orange. | Voir contournements ci‑dessous. |

**Orange (Livebox) – contournements**  
- **Routeur secondaire** : placer un routeur derrière la Livebox, configurer ce routeur en DHCP avec DNS 8.8.8.8 / 1.1.1.1 ; connecter l’ESP32 au Wi‑Fi du routeur secondaire. L’ESP32 recevra alors les DNS du routeur.  
- **DNS sur chaque appareil** : possible sur PC/téléphone ; sur l’ESP32, impossible sans passer en IP statique + `WiFi.config(ip, gw, mask, dns1, dns2)` dans le firmware.  
- **Pi‑hole / serveur DNS local** : installer un résolveur DNS sur le LAN et faire pointer la Livebox (ou un routeur secondaire) vers ce serveur, si l’interface le permet.

**Vérification**  
Après changement sur la box, redémarrer l’ESP32 (ou attendre le renouvellement du bail DHCP). Le firmware affiche le DNS reçu dans certains logs (mailer, OTA) via `WiFi.dnsIP()`. Si la latence des POST baisse nettement, le goulot était bien le DNS.

2. **Réutilisation de connexion TCP (keep-alive)**  
   La première requête paie DNS + TCP ; les suivantes pourraient réutiliser la même connexion. Réduire les appels à `_http.end()` / `_client.stop()` et réutiliser le même client pour le même hôte est une piste d’évolution (plus complexe, à valider selon le comportement de HTTPClient).

3. **Accepter la latence**  
   Les POST réussissent (code 200) avec un timeout firmware de 18–26 s ; le système reste fonctionnel, la latence est surtout perçue au niveau des logs ou du cycle de sync.

4. **Réseau**  
   Changer de box / opérateur ou rapprocher l’ESP32 du routeur peut améliorer le chemin réseau (moins de pertes, meilleur RTT).

---

## 12. Configuration serveur pour accepter l’IP (diagnostic latence)

**Hébergement mutualisé :** en général on ne peut pas modifier Nginx/Apache ; l’accès par IP donne **403** ou **404**. Il faut garder le hostname (iot.olution.info) et accepter la latence DNS/connexion. La suite s’applique uniquement si vous avez la main sur la config (VPS, serveur dédié).

Quand l’ESP32 envoie les requêtes vers **l’IP** (109.234.162.74) avec l’en-tête **Host: iot.olution.info**, le serveur web doit router ces requêtes vers la même application que pour le nom d’hôte. Sinon, on obtient un **404**.

### Nginx

Le bloc `server` qui gère `iot.olution.info` doit aussi accepter les requêtes reçues sur l’IP. Ajouter l’IP à `server_name` :

```nginx
server {
    listen 80;
    server_name iot.olution.info 109.234.162.74;
    # ... root, index, location /ffp3, php-fpm, etc.
}
```

Si le bloc est dans un fichier inclus (ex. `sites-available/iot.olution.info`), éditer ce fichier puis recharger Nginx :

```bash
sudo nginx -t && sudo systemctl reload nginx
```

### Apache

Dans le VirtualHost qui gère `iot.olution.info`, ajouter l’IP à `ServerAlias` (ou à `ServerName` si un seul nom) :

```apache
<VirtualHost *:80>
    ServerName iot.olution.info
    ServerAlias 109.234.162.74
    # ... DocumentRoot, Directory, etc.
</VirtualHost>
```

Puis recharger Apache :

```bash
sudo apache2ctl configtest && sudo systemctl reload apache2
```

### Vérification

Après modification, depuis une machine ayant accès au serveur ou à Internet :

```bash
curl -s -o NUL -w "%{http_code}" -X POST "http://109.234.162.74/ffp3/post-data-test" -H "Host: iot.olution.info" -d "api_key=XXX&sensor=test&version=1"
```

Un code **200** (ou **401** si clé invalide) confirme que le vhost route correctement ; **404** indique que la config n’est pas encore prise en compte ou que le bloc/vhost n’est pas le bon.

---

## 13. Références

- Section 11 (DNS sur la box), section 12 (config Nginx/Apache) pour accepter l’IP 109.234.162.74 avec Host iot.olution.info.
- Analyse détaillée session 2026-02-15 (mails, heap, POST) : [ANALYSE_MONITOR_2026-02-15_MAILS_HEAP_POST.md](ANALYSE_MONITOR_2026-02-15_MAILS_HEAP_POST.md).
- Pistes de résolution (mails, heap, durée POST) : plan « Pistes résolution mails heap POST ».
- Contrôleur serveur : `ffp3/src/Controller/PostDataController.php` ; réponse 200 via `ResponseHelper::textClose()` (Content-Length, Connection: close). Endpoint diagnostic : `/ffp3/ping` (GET/POST). Fichier standalone n3 : `ffp3/tools/ping_standalone.php`. (validation clé ~l.90–105, insert + sync + instrumentation timing ~l.168–210).
- Timeout firmware : `include/config.h` `HTTP_POST_TIMEOUT_MS` (18 s), `HTTP_POST_RPC_TIMEOUT_MS` (26 s).
- Mesure durée côté firmware : `src/web_client.cpp` (requestStartMs, connectMs après begin(), log final : totalDurationMs, connect_ms, request_ms, rssi).
- Test sans DNS : `include/config.h` `BASE_URL_TEST_IP` = 109.234.162.74 (référence ; non utilisé par défaut sur hébergement mutualisé).
- Réception requête serveur : `PostDataController.php` log « PostData request received at=... ts=... » en début de handle().

---

*Document généré pour usage ultérieur (diagnostic, comparaison après évolution réseau ou serveur).*
