# Analyse du dépôt firmwires – Recommandations

*Dernière mise à jour : mars 2025. Voir aussi [RECOMMANDATIONS_IOT.md](../RECOMMANDATIONS_IOT.md) à la racine.*

## État des lieux

### Structure actuelle

| Dossier    | Type              | Description |
|-----------|-------------------|-------------|
| **n3pp4_2** | Projet PlatformIO | **N3PhasmesProto** – Contrôle serre / aquaponie (ESP32) : température/humidité air, 4 capteurs humidité sol, pompe, luminosité, nourrissage poisson, mail d’alerte, serveur web, NTP, OLED. `src/main.cpp` (~1300 lignes). |
| **msp2_5**  | Projet PlatformIO | **MeteoStationPrototype** – Station météo + tracker solaire (ESP32) : 2 DHT, 2 humidité, sonde DS18B20, 4 capteurs luminosité, 2 servos, relais, mail, serveur web, NTP, OLED. Source principale : `src/main.cpp`. |

### Points communs (duplication)

Les deux firmwares partagent la même stack logicielle :

- WiFi, AsyncTCP, ESPAsyncWebServer  
- DHT, Adafruit (GFX, SSD1306), Arduino_JSON  
- ESP Mail Client, ESP32Time, Preferences  
- Serveur web auto-hébergé, NTP, envoi d’e-mails, affichage OLED  

---

## Problèmes identifiés

1. ~~**Pas de dépôt Git à la racine**~~ **Résolu.**  
   Le workspace `firmwires` n’est pas un dépôt Git. Seul `msp2_5` contient un `.gitignore`.

2. **Nom de fichier source avec espace**  
   **Résolu.** msp2_5 utilise `src/main.cpp`. (Les espaces dans les noms de fichiers peuvent poser des problèmes (outils, scripts, CI). PlatformIO s’attend par convention à un `main.cpp` dans `src/`.

3. **Partition manquante**  
   `platformio.ini` référence `board_build.partitions = min_spiffs.csv` mais ce fichier n’est pas présent dans le projet (utilisation possible d’une partition par défaut du plateau).

4. **Configuration figée**  
   `upload_port = COM3` et `monitor_port = COM3` sont en dur : à adapter selon la machine ou à documenter.

5. **Fichiers générés et bruit**  
   Beaucoup de `desktop.ini` et tout le contenu de `.pio/` (build + libs). `.gitignore` de `msp2_5` ignore déjà `.pio`, mais pas les `desktop.ini`.

6. ~~**Documentation**~~ **Résolu.** README à la racine IOT_n3 et dans firmwires.

7. **Code monolithique**  
   Chaque firmware est un gros fichier unique, ce qui complique la maintenance et la réutilisation du code commun.

---

## Recommandations

### Priorité haute

1. **Initialiser Git à la racine**
   - `git init` dans `c:\IOT_n3\firmwires`
   - Créer un `.gitignore` racine avec au minimum :
     - `.pio/`
     - `*.pyc`, `__pycache__/`
     - `desktop.ini`
     - Fichiers d’environnement / secrets (ex. `credentials.h` si vous en ajoutez)

2. **Renommer le fichier source principal de msp2_5**
   - Remplacer `msp2_5 copy.cpp` par `main.cpp` dans `msp2_5/src/`
   - Vérifier que `pio run` et `pio run -t upload` fonctionnent après renommage.

3. **README à la racine**
   - Décrire brièvement :
     - **n3pp4_2** : N3PhasmesProto, comment ouvrir avec l’IDE Arduino ou PlatformIO
     - **msp2_5** : MeteoStationPrototype, commandes `pio run`, `pio run -t upload`
   - Préciser la carte (ESP32) et les ports (ex. COM3 à adapter).

### Priorité moyenne

4. ~~**Partition SPIFFS**~~ **Fait.** Ligne supprimée dans `msp2_5/platformio.ini`.

5. **Port série**
   - Dans `platformio.ini`, mettre par exemple `upload_port = COM3` avec un commentaire du type « à adapter selon la machine »,  
   - Ou utiliser des variables d’environnement / `platformio.ini` par environnement pour ne pas versionner un COM fixe.

6. **Nettoyage**
   - Ajouter `desktop.ini` au `.gitignore` racine pour éviter de versionner ces fichiers Windows.

### Priorité basse (refactor long terme)

7. **Extraire le code commun**
   - Créer un dossier partagé (ex. `common/` à la racine ou dans chaque projet) pour :
     - Connexion WiFi  
     - Serveur web (routes, réponses JSON)  
     - NTP / ESP32Time  
     - Envoi d’e-mails (ESP Mail Client)  
     - Affichage OLED (Adafruit SSD1306)  
   - Les deux projets pourraient inclure ces sources (via chemins relatifs ou lib PlatformIO) pour limiter la duplication.

8. **Modulariser les firmwares**
   - Découper en modules (config, capteurs, actionneurs, web, mail, affichage) en `.cpp`/`.h` dans `src/` et `include/` pour améliorer la lisibilité et les tests.

9. **n3pp4_2** — Migré en projet PlatformIO (`platformio.ini` + `src/main.cpp`). Les deux firmwares sont désormais gérés de la même façon.

---

## Résumé des actions (état actuel)

| Action | Où | État |
|--------|-----|------|
| Git + .gitignore + submodules | Racine IOT_n3 | Fait |
| main.cpp pour msp2_5 | msp2_5/src/ | Fait |
| README (description + build/upload) | Racine IOT_n3 et firmwires | Fait |
| Suppression ligne min_spiffs.csv | msp2_5/platformio.ini | Fait |

Suite : [RECOMMANDATIONS_IOT.md](../RECOMMANDATIONS_IOT.md), [docs/inventaire_appareils.md](../docs/inventaire_appareils.md).
