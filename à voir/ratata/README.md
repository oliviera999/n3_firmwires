# Ratata – Kit ZYC0108-EN (voiture / robot)

*Contexte : [salle aérée n³](https://n3.olution.info) · Firmwares : [README firmwires](../README.md).*

Ce dossier contient les **sources et la documentation du kit robot/voiture ZYC0108-EN** (nom de projet **ratata**) utilisé en démo dans la salle n³. Le kit fournit **huit exemples** : sept pour **Arduino UNO** (moteurs, servo, ultrason, évitement, suivi de ligne, etc.) et un pour **ESP32-CAM** (voiture avec caméra WiFi et stream HTTP).

**Pas de serveur dédié** : usage local (démo, pilotage) ; la variante ESP32-CAM peut exposer un stream HTTP en direct.

---

## 1. Structure du dossier

```
ratata/
├── README.md                   # Ce fichier
├── platformio.ini              # 8 environnements (7× UNO, 1× ESP32-CAM)
├── src/                        # Sources par exemple (build PlatformIO)
│   ├── 1_Auto_move/main.cpp
│   ├── 2_servo_Angle/main.cpp
│   ├── 3_Ultrasonic_follow/main.cpp
│   ├── 4_Obstacle_Avoidance/main.cpp
│   ├── 5_Tracking/main.cpp
│   ├── 6.1_ESP32_Car/main.cpp
│   ├── 6.2_Arduino_UNO/main.cpp
│   └── test/main.cpp
└── ZYC0108-EN/
    └── ZYC0108-EN/
        ├── 1_Get_start/        # Documentation et pilotes
        │   ├── 1_Assembly_guide/
        │   ├── 2_Arduino software/
        │   ├── 3_Libraries/
        │   ├── CH340 Driver File-MAC/
        │   └── CH340 Driver File-Windows/
        ├── 2_Arduino_Code/     # Exemples de code (kit d’origine, peut rester vides)
        │   ├── 1_Auto_move/
        │   ├── 2_servo_Angle/
        │   ├── 3_Ultrasonic_follow/
        │   ├── 4_Obstacle_Avoidance/
        │   ├── 5_Tracking/
        │   ├── 6.1_ESP32_Car/
        │   ├── 6.2_Arduino_UNO/
        │   └── test/
        └── 3_References/
```

- **platformio.ini** : définit les 8 environnements ; chaque env utilise le `main.cpp` du sous-dossier correspondant dans `src/`.
- **src/** : un `main.cpp` par exemple (placeholders compilables ; à remplacer par le code du kit ou par le projet ANM-P4F).
- **1_Get_start** : guide d’assemblage, logiciel Arduino, librairies, pilotes USB CH340 (Windows/Mac).
- **2_Arduino_Code** : structure du kit d’origine (`.ino` si fournis par le fabricant).
- **3_References** : documentation de référence du kit.

Compilation : `pio run -e 1_auto_move` (ou un autre env), `pio run -e <env> -t upload`. Voir [README firmwires](../README.md).

---

## 2. Les huit exemples (2_Arduino_Code)

| N° | Dossier | Carte | Description |
|----|---------|--------|-------------|
| 1 | **1_Auto_move** | Arduino UNO | Déplacement de base (avancer, reculer, tourner). |
| 2 | **2_servo_Angle** | Arduino UNO | Contrôle du servo (angle). |
| 3 | **3_Ultrasonic_follow** | Arduino UNO | Suivi d’obstacle avec capteur ultrason. |
| 4 | **4_Obstacle_Avoidance** | Arduino UNO | Évitement d’obstacles (ultrason). |
| 5 | **5_Tracking** | Arduino UNO | Suivi de ligne (capteurs au sol). |
| 6a | **6.1_ESP32_Car** | ESP32-CAM | Voiture avec caméra WiFi ; stream HTTP possible. |
| 6b | **6.2_Arduino_UNO** | Arduino UNO | Variante « deux Arduino » (UNO). |
| – | **test** | Arduino UNO | Exemple de test / debug. |

- **6.1_ESP32_Car** : seul exemple sur ESP32-CAM ; peut inclure un serveur HTTP (ex. `app_httpd.cpp`, `camera_index.h`) pour le flux vidéo.
- Les exemples **1 à 5** et **6.2**, **test** utilisent l’**Arduino UNO** et le câblage commun décrit ci‑dessous.

---

## 3. Broches et matériel (UNO)

Broches **communes** documentées pour les exemples UNO du kit :

| Fonction | Broches | Remarque |
|----------|---------|----------|
| **Moteurs (PWM)** | 5, 6 | Vitesse des moteurs. |
| **Servo** | 9 | Contrôle d’angle. |
| **Ultrason (Trig / Echo)** | 12, 13 | Capteur de distance. |
| **Suivi de ligne** | A0, A1, A2 | Capteurs analogiques au sol. |
| **Régulateur de sortie** | 74HCT595N | Circuit dé shift-register pour sorties (ex. sens moteurs). |

À vérifier dans le schéma ou la doc du kit pour les broches exactes du 74HCT595N (données, horloge, verrouillage).

---

## 4. Compilation et upload

- **Avec l’IDE Arduino** : ouvrir le `.ino` du sous-dossier souhaité dans `ZYC0108-EN/ZYC0108-EN/2_Arduino_Code/` et flasher sur UNO ou ESP32-CAM selon l’exemple.
- **Avec PlatformIO** (si `platformio.ini` et `src/` sont en place à la racine de `ratata/`) :
  - UNO : `pio run -e 1_auto_move` (ou `2_servo_angle`, `3_ultrasonic_follow`, etc.) puis `pio run -e <env> -t upload`.
  - ESP32-CAM : `pio run -e 6_1_esp32_car` puis upload et `pio device monitor -e 6_1_esp32_car`.

Adapter `upload_port` et `monitor_port` dans `platformio.ini` (ex. `COM3`, `/dev/ttyUSB0`).

---

## 5. Inventaire et nommage (salle n³)

Pour un appareil déployé en salle, l’identifier dans [docs/inventaire_appareils.md](../../docs/inventaire_appareils.md) avec le préfixe **n3-** (ex. `n3-ratata-01`), type « Kit ZYC0108-EN / ratata », emplacement et version du firmware utilisé.

---

## 6. Références projet

- **README firmwires** : [../README.md](../README.md) (tableau des projets, commandes communes).
- **Analyse arborescence** : [../../ANALYSE_ARBORESCENCE.md](../../ANALYSE_ARBORESCENCE.md) (§ Ratata – Kit ZYC0108-EN).
- **Recommandations IoT** : [../../RECOMMANDATIONS_IOT.md](../../RECOMMANDATIONS_IOT.md) (robot/démo, usage local).

---

## 7. Infos en ligne et produits similaires

La référence **ZYC0108-EN** ne remonte pas à une fiche produit publique identifiée ; elle appartient très probablement à la **gamme ZYC / TSCINBUNY** (kits robot voiture Arduino + ESP32-CAM). Voici des liens utiles et des kits similaires pour specs et tutoriels.

### Fabricant / revendeurs (gamme ZYC)

- **TSCINBUNY** (site officiel, EN/FR) : [tscinbuny.com](https://tscinbuny.com)  
  - [Robot Car Kit For Arduino Uno R3](https://tscinbuny.com/fr/products/tscinbuny-robot-car-kit-for-arduino-uno-project-4wd-with-rubber-wheels-obstacle-avoidance-line-tracking-complete-kit-programable-robotic-with-codes) (SKU ZYC0043) : 4WD, suivi de ligne, évitement d’obstacles, L298N, ultrason, servo SG90, code Arduino.  
  - [ESP32 Camera 4WD Robot Kits](https://tscinbuny.com/products/esp32-camera-4wd-robot-kits-for-arduino-programmable) (SKU ZYC0211) : ESP32 caméra, évitement ultrason, suivi infra rouge, stream image WiFi.  
  - Contact : tscinbunyzhiyi@gmail.com (manuels, codes, support).
- **RoboticsDNA** (Inde) : [roboticsdna.in](https://roboticsdna.in) — propose d’autres kits ZYC (ex. ZYC0076 ESP32-CAM WiFi, ZYC0069 4WD Bluetooth, ZYC0068 ultrason + IR).

### Spécifications typiques (kits voiture ZYC / TSCINBUNY)

| Élément | Valeur courante |
|--------|------------------|
| Alimentation | 5 V (ou 9–12 V selon kit), 2× 18650 3,7 V (non fournis) |
| Pilote moteur | L298N |
| Ultrason | HC-SR04 (2–400 cm) |
| Servo | SG90 |
| Code | C/Arduino, exemples fournis, manuels téléchargeables (lien/QR dans la boîte) |

### Tutoriels génériques (ESP32-CAM voiture WiFi)

- [Commander une voiture équipée d’une caméra par WiFi avec l’ESP32-CAM](https://www.robotique.tech/tutoriel/commander-une-voiture-equipee-dune-camera-via-une-connexion-wifi-avec-la-carte-esp32-cam/) (robotique.tech).

Pour obtenir une fiche ou un manuel spécifique au **ZYC0108-EN**, contacter le vendeur du kit ou TSCINBUNY en indiquant la référence exacte.

---

## 8. Projets utilisant ce type de robot

Projets open source ou tutoriels basés sur une **voiture robot Arduino UNO + ESP32-CAM** (L298N, ultrason, suivi de ligne, stream vidéo WiFi), réutilisables comme inspiration pour le kit ZYC0108-EN / ratata.

### Projet le plus intéressant à exploiter (recommandation)

**ANM-P4F/ESP32-CAM-ROBOT** ([GitHub](https://github.com/ANM-P4F/ESP32-CAM-ROBOT)) + tutoriel [Hackster « ESP32-CAM Simple Surveillance RC Car »](https://www.hackster.io/longpth/esp32-cam-simple-surveillance-rc-car-cba6c0).

| Critère | Pourquoi c’est adapté |
|--------|------------------------|
| **Architecture** | Identique au kit : **ESP32-CAM** (stream + serveur web) + **Arduino UNO** (moteurs). Correspond directement aux exemples **6.1_ESP32_Car** et **6.2_Arduino_UNO**. |
| **Documentation** | Tutoriel Hackster détaillé (schémas, câblage, flux logiciel), code commenté, explication WebSocket vs TCP. |
| **Stack** | WebSocket pour la vidéo, serveur HTTP pour la page de contrôle ; pas de serveur backend dédié — idéal pour une démo locale en salle n³. |
| **Contrôle** | Depuis n’importe quel navigateur (PC ou smartphone) sur le WiFi local : pas d’app à installer. |
| **Réutilisation** | Deux dépôts de code (ESP32CamRobot, ESP32CamRobotMotors) faciles à adapter aux broches du ZYC0108-EN (PWM 5/6, etc.). |

**Pistes d’exploitation** : remplacer ou compléter le code de `6.1_ESP32_Car` / `6.2_Arduino_UNO` par celui de ce projet ; ajouter éventuellement des commandes pour le servo (inclinaison caméra) ou la LED, puis documenter le câblage spécifique au kit dans le README ratata.

**Alternative** (plus de fonctions, plus complexe) : [Arduino ESP32 All in One Robot](https://www.hackster.io/ciurisci/arduino-esp32-all-in-one-robot-b9b1c6) — évitement + suivi de ligne + suivi d’objet + stream + app Android ; intéressant pour étendre les exemples 3, 4 et 5 du kit une fois la base surveillance en place.

### GitHub

| Projet | Description |
|--------|-------------|
| [ANM-P4F/ESP32-CAM-ROBOT](https://github.com/ANM-P4F/ESP32-CAM-ROBOT) | Robot de surveillance : ESP32-CAM + Arduino UNO. Stream vidéo par WebSocket, contrôle depuis le navigateur (Chrome). Code ESP32 (serveur HTTP + WebSocket) et sketch UNO pour les moteurs. ~40 étoiles. |
| [VJLIVE/ESP32-IOT-Car](https://github.com/VJLIVE/ESP32-IOT-Car) | Voiture IoT : stream en direct, commande vocale, détection d’obstacles. ESP32-CAM + Arduino + L298N. Orienté surveillance, secours, transport. |
| [SamRepository/EPS32Cam_RobotCar](https://github.com/SamRepository/EPS32Cam_RobotCar) | Voiture robot ESP32-CAM avec schémas et application web pour contrôle à distance et suivi en temps réel. |
| [buzzlightier001/Blynk-Esp32Cam-NodeMCU-Line-Follower-Obstacle-Avoider-Bot](https://github.com/buzzlightier001/Blynk-Esp32Cam-NodeMCU-Line-Follower-Obstacle-Avoider-Bot) | Suivi de ligne + évitement d’obstacles, pilotage via l’app Blynk. |
| [FaresM0hamed/Line-Follower-Car-Robot](https://github.com/FaresM0hamed/line-follower-car-robot) | Suivi de ligne Arduino (capteurs IR + L298N), code simple. |
| [SANJAY-K-04/Line-Follower-Robot-Using-ESP32](https://github.com/SANJAY-K-04/line-follower-robot-using-esp32) | Suivi de ligne avec ESP32 et capteurs IR. |

### Hackster.io

| Projet | Description |
|--------|-------------|
| [ESP32-CAM Simple Surveillance RC Car](https://www.hackster.io/longpth/esp32-cam-simple-surveillance-rc-car-cba6c0) | RC car de surveillance : ESP32-CAM + Arduino UNO, contrôle depuis le navigateur (smartphone), WebSocket pour la vidéo. Instructions détaillées, schémas, code (lié au dépôt ANM-P4F/ESP32-CAM-ROBOT). |
| [Arduino ESP32 All in One Robot](https://www.hackster.io/ciurisci/arduino-esp32-all-in-one-robot-b9b1c6) | Robot « tout-en-un » : évitement, suivi de ligne, suivi d’objet, stream vidéo. UNO + ESP32-CAM, L298, HC-SR04, module suivi 5 voies, IR, servos ; app Android. |

### Autres

- **Cirkit Designer** : [ESP32-CAM Controlled Robotic Car with Ultrasonic Obstacle Avoidance and IR Sensors](https://docs.cirkitdesigner.com/project/published/2ee09785-6c80-4bb2-9054-da0f61ff86c1/esp32-cam-controlled-robotic-car-with-ultrasonic-obstacle-avoidance-and-ir-sensors) — guide et schéma éditable (ultrason + IR).
- **Adeept** : [Smart Car Kit for ESP32-WROVER](http://www.adeept.com/esp32-car_p0414.html) — kit commercial avec suivi de ligne, évitement, OLED, ultrason, vidéo ESP32-CAM ; compatible Arduino IDE, nombreuses leçons.
