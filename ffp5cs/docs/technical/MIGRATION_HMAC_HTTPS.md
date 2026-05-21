# Migration contrat firmware <-> serveur : HTTPS + HMAC + signature OTA

> v13.80 (audit général 2026-05) — démarrage de la migration en **mode dual rétrocompatible**.
> v13.90 — bascule par défaut sur HTTPS + HMAC.
> v13.85 (futur) — signature OTA Ed25519.

## Vue d'ensemble

Avant l'audit, le contrat firmware <-> serveur reposait sur :
- HTTP clair pour POST `/ffp3/post-data*`, GET `/ffp3/api/outputs*/state`, `/ffp3/heartbeat*`.
- `api_key` envoyé en clair dans le body POST.
- OTA téléchargé en HTTP avec vérification MD5 seul (MITM possible si DNS détourné).

L'audit a identifié trois failles BLOQUANTES côté déploiement Internet :
1. **`api_key` en clair sur HTTP** : interceptable sur le LAN (sniffing) ou en transit Internet (FAI).
2. **Pas d'HMAC** : impossible de vérifier l'intégrité ou l'authenticité du message côté serveur.
3. **OTA HTTP + MD5** : un attaquant pouvant intercepter peut remplacer le binaire.

Le serveur (App\Security\SignatureValidator) supportait déjà HMAC-SHA256, mais le firmware ne l'utilisait pas.

## Architecture v13.80 (mode dual)

### Endpoint HTTPS opt-in

`include/server_url_config.h` introduit le flag de build `USE_HTTPS_ENDPOINTS` :
- Sans flag (par défaut) : `BASE_URL = http://iot.olution.info` (legacy).
- Avec flag : intention `BASE_URL = https://iot.olution.info` (TLS).

Depuis v13.81, l'env PlatformIO `wroom-prod-https` est volontairement bloqué par un garde-fou
de compilation tant que `WebClient` POST/GET/heartbeat ne possède pas de transport TLS validé.
Le `BASE_URL_SECURE` (HTTPS) reste utilisé pour la metadata OTA dans tous les cas via
`ota_manager.cpp`, qui utilise un chemin TLS séparé.

### HMAC-SHA256 en complément d'`api_key`

Le module `include/hmac_sign.h` + `src/hmac_sign.cpp` fournit :
- `HmacSign::isEnabled()` : true si `Secrets::API_SIG_SECRET` est configuré (différent de placeholder).
- `HmacSign::generateNonce(out, n)` : nonce 16 hex via `esp_fill_random` (HW RNG).
- `HmacSign::computeHmacHex(secret, ts, nonce, body, out, n)` : HMAC-SHA256 lower-hex via mbedtls/md.h.

`src/web_client.cpp::httpRequest()` ajoute trois en-têtes HTTP **en complément** du body `api_key=...` :
- `X-Sig-Timestamp` : `time(nullptr)` (epoch UTC).
- `X-Sig-Nonce` : 16 hex aléatoires.
- `X-Sig-Hmac` : HMAC-SHA256 lower-hex de `timestamp + "\n" + nonce + "\n" + body`.

Si `Secrets::API_SIG_SECRET` n'est pas configuré, les en-têtes ne sont pas envoyés (rétrocompat full legacy).

### Contrat serveur (App\Security\SignatureValidator)

Le serveur doit **tolérer** les deux modes pendant la transition :
- Mode legacy : `api_key=...` dans le body → valider via `.env API_KEY`.
- Mode HMAC : si en-têtes `X-Sig-*` présents → recalculer l'HMAC avec `.env API_SIG_SECRET` et comparer. Si valide → autoriser même sans `api_key`.

À vérifier côté `serveur/src/Security/SignatureValidator.php` avant de pousser v13.80 sur la prod : que les routes acceptent les en-têtes `X-Sig-*` ET tombent en fallback `api_key` si absents.

## Configuration (secrets_config.h)

Pour activer HMAC sur un firmware :

```cpp
#pragma once
namespace Secrets {
    constexpr const char* API_KEY = "votre_cle_api";
    constexpr const char* API_SIG_SECRET = "votre_secret_hmac_partage_long";
    // ...
}
#define SECRETS_INCLUDE_API_SIG_SECRET 1
```

Doit correspondre à `.env API_SIG_SECRET` côté serveur. Recommandé : ≥ 32 caractères aléatoires.

## Plan de migration

### Phase 1 — v13.80 (en cours)
- Firmware : mode dual HMAC prêt ; HTTPS métier préparé mais bloqué en v13.81 jusqu'à transport TLS.
- Serveur : `SignatureValidator` doit tolérer les deux modes (`api_key` legacy et `X-Sig-*`).
- Pilote : ne flasher `wroom-prod-https` qu'après retrait du garde-fou via un transport TLS testé.

### Phase 2 — Validation pilote (1-2 semaines)
- Monitoring continu (logs `[HTTP] Verdict 2xx`, taux d'erreur).
- Vérifier les flux POST data, GET outputs/state, heartbeat OTA en HTTPS.
- Vérifier que le serveur consigne `X-Sig-*` correctement.
- Aucun reboot anormal, heap stable.

### Phase 3 — v13.85 (signature OTA Ed25519)
- Ajout : `Secrets::OTA_PUBLIC_KEY_HEX` (32 octets = 64 hex).
- `metadata.json` enrichi avec un champ `signature` (Ed25519 du binaire).
- `ota_manager.cpp` vérifie la signature **en plus** du MD5 si la clé publique est configurée.
- Pipeline serveur (`scripts/publish_ota.ps1`) signe chaque binaire avec la clé privée (jamais embarquée firmware).

### Phase 4 — v13.90 (bascule par défaut)
- `USE_HTTPS_ENDPOINTS` activé par défaut sur `wroom-prod` et `wroom-s3-prod`.
- Flag `USE_LEGACY_HTTP` pour fallback explicite (transition).
- HMAC obligatoire si `API_SIG_SECRET` configuré ; échec auth → fallback NVS (queue offline).
- Signature OTA Ed25519 obligatoire en prod (refus OTA si absente).

## Compatibilité

| Mode | api_key | X-Sig-* | HTTPS | Statut |
|---|---|---|---|---|
| Legacy (≤ v13.70) | obligatoire | absent | non | Supporté en v13.80, refusé v13.90+ prod |
| Dual (v13.80/13.81) | obligatoire | présent | non (HTTPS métier bloqué en 13.81) | Configuration recommandée pour HMAC pilote |
| Cible (v13.90) | optionnel (fallback) | obligatoire | défaut | Production future |

## Sécurité

- **HMAC** : protège contre la modification du body (replay attaqué via le nonce + timestamp serveur).
- **HTTPS** : protège contre l'écoute du body et des en-têtes (sniffing LAN ou MITM Internet).
- **Signature OTA Ed25519** : protège contre la substitution du binaire OTA, même en HTTP (la signature est validée localement).

Recommandé : activer les trois en production v13.90+.

## Références

- `firmwires/ffp5cs/include/hmac_sign.h` / `src/hmac_sign.cpp` (v13.80)
- `firmwires/ffp5cs/include/server_url_config.h` (flag `USE_HTTPS_ENDPOINTS`)
- `firmwires/ffp5cs/include/secrets_config.h.example` (modèle config)
- `firmwires/ffp5cs/platformio.ini` (env `wroom-prod-https`)
- `serveur/src/Security/SignatureValidator.php` (serveur)