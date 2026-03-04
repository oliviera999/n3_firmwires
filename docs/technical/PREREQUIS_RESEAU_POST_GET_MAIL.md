# Prérequis réseau pour POST, GET et Mail (FFP5CS)

**Objectif :** Pour que les envois POST (données capteurs), les GET (outputs/state) et l’envoi des mails SMTP fonctionnent correctement, l’ESP32 doit avoir un accès réseau et DNS opérationnel.

---

## 0. Configuration WiFi (connexion au réseau)

Pour que le firmware se connecte au WiFi et puisse publier les données (POST, GET, Mail) :

- **Fichier `include/secrets.h`** : copier `include/secrets.h.example` vers `include/secrets.h`, renseigner au moins un réseau dans `WIFI_LIST` (ex. `{"VotreSSID", "VotreMotDePasse"}`). Sans credentials, le firmware logue « Aucun reseau configure » et démarre l'AP de secours uniquement.
- **Après erase flash** : les credentials sont ceux compilés dans `secrets.h` (pas de liste en NVS). Mettre les bons SSID/mot de passe avant le build.
- **Réseau en portée** : le réseau configuré doit être détectable au scan, sinon « WiFi connect failed; starting AP ».

---

## 1. Prérequis communs

- **WiFi connecté** avec accès **Internet** (pas uniquement réseau local).
- **DNS fonctionnel** : les noms d’hôtes doivent être résolus. En cas de « DNS Failed », les requêtes échouent (code -1, timeouts).

---

## 2. POST (données capteurs vers serveur distant)

- **Hôte :** `iot.olution.info` (ou l’URL configurée dans `ServerConfig::BASE_URL`).
- **Prérequis :** Résolution DNS de cet hôte et accès HTTP (port 80) ou HTTPS selon la config.
- **Comportement :** En cas d’échec (DNS, timeout, file pleine), les payloads sont mis en queue et réessayés plus tard (offline-first).

---

## 3. GET (outputs/state depuis le serveur)

- **Hôte :** Idem (ex. `iot.olution.info`).
- **Prérequis :** Même que POST. Sans DNS/réseau, le GET échoue (code -1, pas de cache NVS utilisé si vide).

---

## 4. Mail (SMTP)

- **Hôte :** `smtp.gmail.com` (config SMTP dans `config.h`).
- **Prérequis :** Résolution DNS de `smtp.gmail.com` et accès sortant sur le port **465** (SSL).
- **Comportement :** Si « DNS Failed for smtp.gmail.com », la connexion SMTP échoue ; le mail peut être remis en queue (retry).

---

## 5. Vérification rapide (côté PC)

Depuis une machine sur le même réseau (ou le même accès Internet) que l’ESP32 :

- `ping iot.olution.info`
- `nslookup smtp.gmail.com`
- `nslookup pool.ntp.org` (pour NTP)

Si ces résolutions échouent, les POST/GET/Mail et la sync NTP échoueront aussi sur l’ESP32. Vérifier la box (DNS), le réseau 4G ou le pare-feu.

---

## 6. Références

- Timeouts et constantes : `include/config.h` (NetworkConfig, OTAConfig).
- Contrat POST/GET : `docs/technical/VARIABLE_NAMING.md`, serveur ffp3 (PostDataController, SensorData).
