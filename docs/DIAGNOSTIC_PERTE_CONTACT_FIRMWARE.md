# Pertes de contact diagnosticables — volet firmware

> Pendant firmware de n3_serveur `docs/DIAGNOSTIC_FFP3_PERTE_CONTACT_2026-11-06.md` (analyse de
> l'incident) et `docs/PROCEDURE_PERTE_CONTACT.md` (procédure en une requête MCP). Versions :
> **ffp5cs 15.34**, **n3pp 4.76**, **msp 2.79** ; libs `n3_common` 1.9.0, `n3_data` 1.4.0,
> `n3_time` 1.5.0. Serveur requis : **n3_serveur ≥ 6.59.0**.

## 1. Ce que l'incident du 06/11/2026 a montré

Le WROOM FFP3 (15.31) s'est tu à 16:40:27, juste après la vérification OTA qui suit le mail de
réveil, alors que la 15.33 venait d'être publiée sur le canal prod. Rien, côté module, ne permettait
de trancher entre OTA bloquée, crash en boucle, Wi-Fi, horloge, alimentation ou serveur :

| Manque | Réponse 15.34 / 4.76 / 2.79 |
|---|---|
| Aucune trace survivant au reboot | journal de bord **persistant en NVS**, renvoyé jusqu'à acquittement |
| Aucune étape OTA connue du serveur | `ota_check/start/progress/done/fail/paused` persistés **avant** le flash et poussés à `/ota-report` |
| Image validée par simple uptime (120 s) | ffp5cs : validation seulement après **POST signé accepté**, sinon rollback à 15 min |
| Horloge en avance d'un cycle de veille (401 au réveil) | correction du double comptage, resync NTP à chaque réveil, repli heure serveur, jamais de signature sans horloge fiable |
| Cause de reset / partition / boots ratés invisibles | heartbeat enrichi `rst`, `part`, `ota`, `fails`, `safe`, `clk`, `wifi_*`, `boot=1` |
| Module qui re-tente l'OTA en boucle sans contact | **mode sans échec** après 3 boots/réveils sans contact sain |

## 2. Vocabulaire des événements (`e`) et détail (`d`)

| Événement | Émis par | Détail typique |
|---|---|---|
| `boot` / `wake` | ContactDiag::begin (ffp5cs) / n3ContactDiagBegin | `rst=PANIC part=ota_1 ota=pending fails=2 v=15.34` |
| `wifi_ok` / `wifi_fail` | connexion boot et réveil | `boot rssi=-67 in 3120ms` / `wake status=6 after=15000ms` |
| `ntp_ok` / `ntp_fail` | sync NTP boot / réveil | `wake slept=2000s` |
| `clock_fix` | ffp5cs `getSleptTime` | `sleep +45s (slept=2045s adv=2000s)` — correction réellement appliquée |
| `clock_srv` | heure serveur appliquée (NTP KO) | `offset=+2045s rtt=640ms` |
| `clock_drift` | NTP OK mais écart serveur > 60 s | `offset=-75s ntp=1` |
| `http_401` / `http_4xx` / `http_5xx` / `http_fail` | WebClient / n3_data | `hmac ntp=1 clk=2045` / `code=-11 connect=0ms rssi=-80` |
| `contact_ok` | premier 2xx d'une requête signée | `uptime=42s` — remet `fails` à 0 |
| `sleep_in` / `sleep_out` | veille légère (ffp5cs) / deep sleep (`sleep_in` seul) | `2000s heap=81234` / `slept=2000s wifi=1` |
| `ota_check` | MAJ détectée | `remote=15.35 local=15.34 size=1583115` |
| `ota_check_fail` / `ota_paused` | metadata KO / 503 | `Erreur GET métadonnées: -1` / `503 canal en pause ou canari` |
| `ota_skip_safe` | mode sans échec | `safe mode` |
| `ota_start` / `ota_progress` / `ota_done` / `ota_fail` | téléchargement | `15.34->15.35 1545KB` / `50% 790000/1583115` / `… boot=ota_1` / `Update.end échec…` |
| `ota_pending` / `ota_valid` / `ota_rollback` / `ota_pending_reboot` | ffp5cs probation | `prev=15.34 now=15.35 part=ota_1` / `contact serveur authentifie accepte v=15.35` |

Chaque événement porte `q` (séquence monotone), `t` (epoch firmware, 0 si l'horloge n'était pas
plausible) et `u` (uptime en s). Le serveur dédoublonne par séquence (`device_events`).

## 3. Transport

- **Heartbeat** (`/heartbeat*`) : champs ajoutés après `ip=` ; le CRC32 historique ne couvre toujours
  que `uptime&free&min&reboots`. `log=[…]` embarque les événements non acquittés (≤ 360 car.).
- **`POST /<famille>/ota-report`** (`/ota-report`, `/ota-report-test`, `/ota-report3`, `/n3pp/ota-report`,
  `/msp1/ota-report`…, dérivé de l'URL heartbeat) : `src`, `board`, `version`, `uptime`,
  `events=[{q,t,u,e,d}]` ; **HMAC obligatoire** (pas de repli `api_key`) ; réponse `{"acked":[…]}`.
  Appelé après chaque heartbeat et autour de `ota_start` / `ota_done` / `ota_fail` ; sans horloge fiable
  rien n'est envoyé (le serveur rejetterait).
- **Heure serveur** : chaque réponse POST porte `X-Server-Ts` ; le firmware calcule
  `clk = horloge_module − horloge_serveur` (latence/2 compensée) et l'applique si NTP a échoué.
- **GET OTA** : `X-Ota-Board`, `X-Ota-Version` ; `503` = canal en pause / canari (pas une panne).

Rien de secret ne transite : pas de clé, pas de jeton, pas de GPIO 100/101.

## 4. Sûreté OTA (ffp5cs 15.34)

```
flash OK ──reboot──▶ boot (ota=pending) ──Wi-Fi + heure fiable + POST signé 2xx──▶ ota_valid
                            │
                            └── 15 min sans contact sain ──▶ ota_rollback ──▶ reboot sur l'ancienne image
```

- `TimingConfig::OTA_VALIDATION_DEADLINE_MS` = 15 min d'uptime (`millis()` continue pendant la veille
  légère). Un module sain poste dans la première minute : la validation est quasi immédiate.
- Un reboot délibéré (web, GPIO 110) **avant** contact sain laisse l'image en attente
  (`ota_pending_reboot`) : le bootloader revient à l'ancienne image.
- Effet de bord assumé : une coupure Wi-Fi > 15 min juste après une OTA fait revenir l'ancienne
  version ; la suivante vérification OTA (2 h) re-télécharge. Le serveur le voit (`ota_done` →
  `boot` → `ota_rollback`) et l'alerte « module muet après téléchargement OTA » ne part que si le module
  ne revient pas du tout.
- n3pp / msp : rollback **non activé** (opt-in `N3_OTA_ROLLBACK_ENABLE`, voir `OTA_ROLLBACK_OPT_IN.md`) ;
  le portage journalise mais ne touche pas au cycle OTA.

## 5. Mode sans échec

`fails` = boots (ffp5cs) ou réveils (n3pp/msp) consécutifs sans contact sain ; persisté **au tout
début** du boot (un crash immédiat compte). À 3 : `safe=1` dans le heartbeat, aucune OTA ; ffp5cs
suspend en plus nourrissage, remplissage et alertes dérivées et **maintient la pompe aquarium** ;
poll / POST / heartbeat / veille continuent. Un POST signé accepté remet le compteur à zéro ; le mode
cesse au boot suivant. Le serveur n'a rien à faire : il voit `safe=1` et décide (reflash USB, pause
du canal OTA, etc.).

## 6. Scénario de banc (à rejouer avant toute publication prod)

Décrit dans `ffp5cs/VERSION.md` (15.34, « Scénario de rollback documenté ») : image test incapable
de joindre le serveur → `ota_start … ota_done` → `boot ota=pending` → 15 min → `ota_rollback` →
retour sur l'image précédente et `contact_ok`. Pour n3pp / msp : couper le Wi-Fi sur trois réveils
→ `fails=3`, `safe=1`, `ota_skip_safe` ; rétablir → `contact_ok`, `fails=0`.

## 7. Ce qui reste impossible à distance (et le matériel qui le couvrirait)

| Invérifiable depuis le firmware / le serveur | Pourquoi | Matériel |
|---|---|---|
| Module totalement mort (brownout prolongé, alimentation coupée, boucle de reset avant Wi-Fi) | aucun paquet ne part ; `rst=BROWNOUT` n'arrive qu'au boot **suivant** | superviseur / watchdog externe (ex. TPL5010) qui coupe et relance l'alimentation, prise connectée pilotable, relais de reset commandé par la caméra ou un second ESP |
| Tension d'alimentation réelle au moment de la chute | pas de mesure sur le WROOM | banc INA226 (`energie/`) ou pont diviseur sur l'entrée 5 V, remonté dans le POST |
| Coupure Internet du site vs panne du routeur Wi-Fi | le module ne distingue pas « associé sans Internet » | la caméra du même site sert de témoin (déjà exploité par `iot_contact_timeline`) ; un ping passerelle vs DNS dans `wifi_fail` affinerait |
| Image qui boucle **avant** `ContactDiag::begin` (NVS corrompue, panic dans le bootloader) | rien n'est journalisé | sortie UART captée par un second module ou enregistreur série |
| Heure exacte d'une veille perdue quand l'horloge n'était pas plausible | `t=0` dans l'événement ; seul `u` (uptime) est fiable | RTC DS3231 (déjà optionnel sur ffp5cs : `USE_RTC_DS3231`) |
