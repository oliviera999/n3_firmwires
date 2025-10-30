# API JSON Contract (ESP32 ⇄ Serveur Distant)

Cette fiche résume le schéma JSON attendu entre l'ESP32 (firmware v11.83+) et le serveur distant (`ffp3/`). Elle sert de référence pour vérifier que l'interface embarquée, le backend PHP/MySQL et la SPA restent synchronisés.

## 1. `GET /api/status`

| Champ | Type | Description |
| --- | --- | --- |
| `TempAir` | float | Température air (°C) validée côté ESP32 |
| `Humidite` | float | Humidité (%) |
| `TempEau` | float | Température eau (°C) |
| `EauAquarium` | int | Distance ultrason aquarium (cm, faible = niveau haut) |
| `EauPotager` | int | Distance ultrason potager |
| `EauReserve` | int | Distance ultrason réserve |
| `Luminosite` | int | Valeur capteur lumière |
| `etatPompeAqua` | bool/int | 1 si pompe aquarium ON |
| `etatPompeTank` | bool/int | 1 si pompe réservoir ON |
| `etatHeat` | bool/int | 1 si chauffage ON |
| `etatUV` | bool/int | 1 si éclairage ON |
| `bouffeMatin/Midi/Soir` | int | Heures programmées |
| `tempsGros` | int | Durée nourrissage gros (s) |
| `tempsPetits` | int | Durée nourrissage petits (s) |
| `aqThreshold` | int | Seuil bas aquarium |
| `tankThreshold` | int | Seuil bas réserve |
| `chauffageThreshold` | float | Seuil déclenchement chauffage |
| `limFlood` | int | Seuil alerte débordement |
| `FreqWakeUp` | int | Périodicité réveil (s) |
| `WakeUp` | int | 1 si réveil forcé |
| `bouffePetits` | string | `"1"` si cycle petits en cours |
| `bouffeGros` | string | `"1"` si cycle gros en cours |
| `mail` | string | Adresse de notification |
| `mailNotif` | string/bool | `checked/1/true` si email actif |

## 2. `POST /api/feed`

Payload `application/x-www-form-urlencoded` généré par le firmware pour tracer nourrissage :

| Champ | Exemple | Notes |
| --- | --- | --- |
| `api_key` | `FFP_KEY` | Validé côté serveur |
| `sensor` | `FFP5CS` | Identifiant station |
| `bouffePetits` | `1` puis `0` | Transition déclenchée côté ESP32 |
| `bouffeGros` | `1` puis `0` | idem |
| `108`, `109` | `0/1` | Fallback GPIO numériques |

## 3. `POST /api/config`

Le serveur renvoie un objet JSON cohérent avec les clés utilisées dans `sendFullUpdate`.

| Champ | Attendu | Commentaire |
| --- | --- | --- |
| `tankThreshold` | int | Seuil réserve |
| `aqThreshold` | int | Seuil aquarium |
| `limFlood` | int | Seuil débordement |
| `tempsRemplissageSec` | int | Durée remplissage (s) |
| `chauffageThreshold` | float | Seuil chauffage |
| `FreqWakeUp` | int | Périodicité réveil |
| `mail` | string | Adresse email |
| `mailNotif` | bool/int/string | Utiliser valeurs truthy | 
| `forceWakeUp` | bool/int/string | Active GPIO 115 |

## 4. Convention de casse

- ESP32 et serveur s'échangent des clés en `camelCase` (héritage historique, compat SPA).
- Les alias numériques (`108`, `109`, `115`, etc.) doivent toujours accompagner les champs logiques correspondants pour compatibilité rétro (BDD + outils).
- Les valeurs booléennes envoyées par le serveur doivent accepter : `true/false`, `1/0`, `"checked"`, `"on"`, `"yes"`.

## 5. Checklist de validation

1. **Avant commit** :
   - Lancer `pwsh ./scripts/verify_version.ps1`.
   - Lancer `pwsh ./scripts/run_ci_checks.ps1` (build + analyse log 90s).
2. **Serveur distant** :
   - Maintenir la même casse de clé que le firmware.
   - Journaliser les requêtes et limiter à 1 req/s par capteur.
   - Documenter toute modification dans ce fichier.

Ce document complète les guides `docs/guides/CI_CD_STATUS.md` et `docs/technical/` existants, et doit être mis à jour à chaque ajout/renommage de clé JSON.

