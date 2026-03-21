# Rapport d’analyse – Dossier firmwires

**Date :** 5 mars 2026 (mise à jour doc : mars 2026)  
**Périmètre :** `c:\IOT_n3\firmwires`

---

## 1. Vue d’ensemble

Le dossier **firmwires** regroupe plusieurs projets de firmware pour microcontrôleurs (principalement **ESP32**), avec une documentation centralisée pour deux projets principaux et d’autres dossiers non décrits à la racine.

| Élément | Détail |
|--------|--------|
| Projets principaux documentés | **n3pp** (N3PhasmesProto), **msp** (MeteoStationPrototype) |
| Autres dossiers présents | ratata, uploadphotosserver_ffp3_1_5_deppsleep, uploadphotosserver_msp1, uploadphotosserver_n3pp_1_6_deppsleep |
| Outil de build | PlatformIO (Arduino, ESP32) |
| Dépôt Git | Aucun à la racine (réponse « No » pour « Is directory a git repo ») |

---

## 2. Structure actuelle

```
firmwires/
├── .gitignore
├── README.md
├── RECOMMANDATIONS.md
├── RAPPORT_ANALYSE.md          ← ce rapport
├── n3pp/                    # N3PhasmesProto (serre / aquaponie)
│   ├── platformio.ini
│   ├── src/main.cpp
│   └── n3pp.ino             # ancien sketch Arduino (référence)
├── msp/                     # MeteoStationPrototype (météo + tracker solaire)
│   ├── platformio.ini
│   ├── src/main.cpp
│   ├── lib/, include/, test/
│   └── .gitignore
├── ratata/                     # Projet ZYC0108-EN (Arduino, voiture/robot)
├── uploadphotosserver_ffp3_1_5_deppsleep/
├── uploadphotosserver_msp1/
└── uploadphotosserver_n3pp_1_6_deppsleep/
```

- Les **deux projets principaux** ont bien un `src/main.cpp` (plus de fichier « msp copy.cpp »).
- Un **.gitignore** existe à la racine (`.pio/`, `desktop.ini`, etc.).
- **RECOMMANDATIONS.md** décrit une analyse antérieure ; plusieurs points ont été traités (README racine, main.cpp, .gitignore).

---

## 3. Projets principaux

### 3.1 N3PhasmesProto (n3pp) – v4.2

- **Rôle :** Contrôle serre / aquaponie (température, humidité air, 4× humidité sol, pompe, luminosité, relais, deep sleep, NTP, OLED, mails, serveur web).
- **Cible :** ESP32 (board `esp32dev`).
- **Stack :** AsyncTCP, ESPAsyncWebServer, DHT, ESP Mail Client, Arduino_JSON, Adafruit GFX/SSD1306, ESP32Time.
- **Serveur :** `http://iot.olution.info` (n3pp, board=3).

### 3.2 MeteoStationPrototype (msp) – v2.5

- **Rôle :** Station météo + tracker solaire (2× DHT, humidité sol, pluie, DS18B20, 4 LDR, 2 servos, relais, NTP, OLED, mails, serveur web).
- **Cible :** ESP32 (board `esp32dev`).
- **Stack :** idem + OneWire, DallasTemperature, ESP32Servo, Preferences, ESPmDNS.
- **Partition :** `board_build.partitions = min_spiffs.csv` dans `platformio.ini` (fichier `min_spiffs.csv` absent du dépôt ; utilisation de la partition par défaut du plateau).
- **Serveur :** `http://iot.olution.info` (msp1, board=2).

---

## 4. Problèmes et points d’attention

### 4.1 Sécurité et bonnes pratiques

| Problème | Détail |
|----------|--------|
| **Identifiants en clair** | Mots de passe WiFi (`<wifi_password>`, etc.) et SMTP (Gmail) dans `src/main.cpp` des deux projets. Risque en cas de partage ou mise sous Git public. |
| **Clé API** | `apiKeyValue = "your_api_key_here"` en dur ; à externaliser (fichier non versionné ou variables d’environnement) pour la production. |

### 4.2 Configuration et build

| Problème | Détail |
|----------|--------|
| **Port série** | `upload_port = COM3` et `monitor_port = COM3` en dur dans les deux `platformio.ini`. À adapter selon la machine ou à documenter clairement. |
| **Partition msp** | `board_build.partitions = min_spiffs.csv` sans fichier `min_spiffs.csv` dans le projet. Soit ajouter le fichier, soit retirer la ligne pour utiliser la partition par défaut. |

### 4.3 Qualité du code

| Projet | Problème | État |
|--------|----------|------|
| **n3pp** | Dans `batterie()`, `sampleTotal += analogRead(pontdiv)` — incohérence avec la moyenne glissante. | **Résolu** : le code utilise déjà `sampleTotal += samples[sampleIndex];`. |
| **n3pp** | Dans `affichageOLED()`, `digitalRead(HeureArrosage)` etc. sur des variables au lieu des broches. | **Résolu** : affichage direct des variables (`HeureArrosage`, `SeuilSec`, `SeuilPontDiv`, `WakeUp`). |
| **Les deux** | Code monolithique (un seul `main.cpp` très long), ce qui complique la maintenance et la réutilisation. | À traiter (refactor). |

### 4.4 Documentation et périmètre

| Problème | Détail |
|----------|--------|
| **README racine** | Décrit tous les projets, liens firmware ↔ serveur, contexte salle n³, inventaire (docs/). |
| **RECOMMANDATIONS.md** | Mis à jour : état actuel, main.cpp, partition, Git IOT_n3, submodules. |

### 4.5 Versionnement

- Dépôt Git à la racine **IOT_n3** ; **firmwires** (submodule) contient **ffp5cs** en dossier ordinaire ; **serveur** (submodule) contient **ffp3**. Voir RECOMMANDATIONS_IOT.md.

---

## 5. Synthèse des recommandations

### Priorité haute

1. ~~**Corriger le bug dans n3pp**~~ **Fait.**  
   Dans `batterie()`, remplacer  
   `sampleTotal += analogRead(pontdiv);`  
   par  
   `sampleTotal += samples[sampleIndex];`.

2. **Corriger l’affichage OLED dans n3pp**  
   Remplacer les `digitalRead(HeureArrosage)`, `digitalRead(SeuilSec)`, etc. par l’affichage direct des variables (`HeureArrosage`, `SeuilSec`, `SeuilPontDiv`, `WakeUp`).

3. **Sécuriser les secrets**  
   Externaliser WiFi, SMTP et clé API (fichier `credentials.h` ou équivalent non versionné, ou variables d’environnement / options de build).

### Priorité moyenne

4. ~~**Clarifier la partition msp**~~ **Fait.**  
   Ligne supprimée dans `msp/platformio.ini` (partition par défaut).

5. **Initialiser Git à la racine (IOT_n3)**  
   `git init` dans `firmwires`, et s’assurer que le `.gitignore` racine couvre bien `.pio/`, `desktop.ini`, et les futurs fichiers de secrets.

6. ~~**Documenter les autres dossiers**~~ **Fait.**  
   **Fait.** README racine et firmwires décrivent tous les projets (ratata, uploadphotosserver_*, ffp5cs).

7. ~~**Mettre à jour RECOMMANDATIONS.md**~~ **Fait.** Références à `msp copy.cpp` et à l’absence de README, et noter les actions déjà réalisées.

### Priorité basse (refactor)

8. **Modulariser le code**  
   Découper en modules (config, capteurs, actionneurs, web, mail, affichage) dans `src/` et `include/` pour les deux firmwares.

9. **Code commun**  
   Extraire WiFi, NTP, mail, OLED, requêtes HTTP dans un dossier partagé (ex. `common/`) pour limiter la duplication entre n3pp et msp.

---

## 6. Tableau récapitulatif

| Action | Où | Priorité | État |
|--------|-----|----------|------|
| Corriger `sampleTotal` dans `batterie()` | n3pp/src/main.cpp | Haute | Fait |
| Corriger affichage variables OLED | n3pp/src/main.cpp | Haute | Fait |
| Externaliser identifiants / clé API | n3pp, msp | Haute | Fait (n3pp, msp) |
| Gérer ou supprimer `min_spiffs.csv` | msp/platformio.ini | Moyenne | Fait |
| `git init` + submodules | Racine IOT_n3 | Moyenne | Fait |
| Documenter ratata et uploadphotosserver_* | README ou doc dédiée | Moyenne | Fait |
| Mettre à jour RECOMMANDATIONS.md | Racine | Moyenne | Fait |
| Modulariser et factoriser le code | n3pp, msp | Basse |

---

*Rapport généré à partir d’une analyse du dossier firmwires (structure, sources, configuration PlatformIO et documentation).*


