# Liste d'achat — carte commune n3pp + msp (rev 0.1)

Tout ce qu'il faut pour fabriquer, monter et mettre en service **une station n3pp
(serre/élevage)** ou **une station msp (météo)** sur la carte commune
[`n3pp-msp-commun`](README.md). La section 2 est dérivée de [`BOM.csv`](BOM.csv)
(source de vérité, régénérée par `generator/generate.py`, colonne *Profil*) ; le
détail réf. par réf. est dans [`ASSEMBLAGE-N3PP.md`](ASSEMBLAGE-N3PP.md) /
[`ASSEMBLAGE-MSP.md`](ASSEMBLAGE-MSP.md).

> 💶 Budget indicatif par station, hors outillage : **40-70 €** (PCB ~15 €
> les 5, DevKit ~8 €, composants carte ~10-15 €, périphériques 15-30 € selon profil).
> Pour équiper **les deux stations** : commander 1 seule série de PCB et 2 jeux de
> composants — c'est tout l'intérêt de la carte commune.

## 1. Circuit imprimé

| Quoi | Détail |
|---|---|
| PCB rev 0.1 | `exports/gerbers-n3pp-msp-commun-v0.1.zip` chez JLCPCB/PCBWay — 210×105 mm, 2 couches, qté mini 5 (~10-20 €). Finition standard 1 oz suffisante. |
| ⚠️ Charges relais | Carte **sans zone secteur isolée** : charges **12/24 V uniquement** sur les borniers relais (pompes, éclairage LED, électrovannes BT). Pour du **230 V**, utiliser la carte dédiée [`ffp5cs-wroom-prod-230v`](../ffp5cs-wroom-prod-230v/ACHATS.md). |

## 2. Composants à souder (BOM par profil)

### 2a. Socle COMMUN (à souder dans tous les cas)

| Qté | Référence | Rôle |
|---|---|---|
| 1 | Relais **SRD-05VDC-SL-C** (5 V, SPDT 10 A) | REL1 — sortie relais des deux firmwares (GPIO13) |
| 1 | Transistor **BC337-40** (TO-92) | commande bobine REL1 |
| 1 | Diode **1N4007** | roue libre bobine REL1 |
| 1 | Schottky **1N5822** | anti-retour VIN (USB et alim 5 V ensemble sans conflit) |
| 1 | LED 5 mm rouge + 1 verte | témoin REL1 + présence 5 V (DNP si profil batterie) |
| 3 | Résistance 1 kΩ 1/4 W | base transistor, LED ×2 |
| 2 | Résistance 10 kΩ | pull-down base (état sûr au boot), pont LDR canal E |
| 2 | Résistance 4,7 kΩ | pull-ups I2C SDA/SCL |
| 2 | Résistance 2,2 kΩ | pont diviseur batterie → GPIO36 (`n3_defaults.h`) |
| 1 | Condensateur 1000 µF/16 V radial | réservoir rail 5 V (relais + WiFi) |
| 2 | Condensateur 100 nF céramique | découplage 3V3 capteurs + I2C |
| 1 | Embase **jack DC 5,5/2,1 mm** | entrée alim |
| 4 | Bornier à vis **2 p, pas 5,08 mm** | entrée 5 V, mesure batterie, distribution 5 V, distribution 3V3 |
| 6 | Bornier à vis **3 p, pas 5,08 mm** | sortie REL1 (NO/COM/NC) + 5 entrées ADC (3V3/AIN/GND) |
| 2 | **Barrette femelle 1×15**, pas 2,54 | supports du DevKit (il s'enfiche, jamais soudé) |
| 4 | Support femelle **1×4** | OLED + 3 ports I2C libres |
| 1 | Header mâle **1×7** | J18 — breakout GPIO libres (3V3 GND EN IO4 IO5 RX0 TX0) |
| 1 | Header mâle **1×6** | J19 — rail alim Dupont (2×5V 2×GND 2×3V3) |

### 2b. En PLUS pour une station n3pp (serre/élevage)

| Qté | Référence | Rôle |
|---|---|---|
| 1 | Relais SRD-05 + BC337 + 1N4007 + LED rouge | canal REL2 — pompe (GPIO12) |
| 2 | Résistance 1 kΩ | base + LED REL2 |
| 2 | Résistance 10 kΩ | pull-down base REL2 (strapping GPIO12) + pull-up DHT |
| 1 | Bornier à vis 3 p, 5,08 mm | sortie REL2 |
| 1 | Embase **JST-XH 3 p** verticale (B3B-XH-A) | DHT11/DHT22 (GPIO18) |

### 2c. En PLUS pour une station msp (météo)

| Qté | Référence | Rôle |
|---|---|---|
| 3 | Embase **JST-XH 3 p** verticale (B3B-XH-A) | DHT INT (GPIO26), DHT EXT (GPIO15), capteur pluie (GPIO27, **DO**) |
| 1 | Bornier à vis 3 p, 5,08 mm | sonde DS18B20 |
| 2 | Header mâle **1×3** | servos tracker G/D + H/B |
| 2 | Résistance 220 Ω | séries signal servos |
| 5 | Résistance 10 kΩ | pull-ups DHT ×2 + bas de pont LDR canaux A/C/D |
| 1 | Résistance 4,7 kΩ | pull-up 1-Wire DS18B20 |
| 1 | Condensateur 470 µF/16 V radial | découplage servos |

### 2d. Extension AUX (optionnel — par canal REL3..REL6 activé)

Par canal : 1 relais SRD-05 + 1 BC337 + 1 1N4007 + 1 LED rouge + 2× 1 kΩ +
1× 10 kΩ + 1 bornier 3 p. (Jusqu'à 4 canaux ; GPIO 16/17/19/23, à déclarer côté
firmware le jour voulu.) + 1× 10 kΩ si une LDR est branchée sur le canal ADC B.

> 💡 En pratique : un **assortiment de résistances 1/4 W** et un **lot de 5-10
> relais/BC337/1N4007** couvrent tous les profils et les extensions.

## 3. Cerveau et périphériques (hors carte)

| Profil | Qté | Quoi | Remarque |
|---|---|---|---|
| les deux | 1 | **ESP32 DevKit V1 30 broches** | ⚠️ bien **30** broches, rangées espacées de **25,4 mm** — pas la variante 36/38 broches. |
| les deux | 1 | **OLED SSD1306 128×64 I2C** (0x3C) | affichage local |
| les deux | 0-3 | Modules I2C optionnels (DS3231, BME280…) | 3 ports libres J15/J16/J17 |
| n3pp | 1 | **DHT11** (ou DHT22) | air serre (GPIO18) |
| n3pp | 4 | **Capteurs humidité sol capacitifs** (sortie AO) | canaux ADC A-D |
| n3pp | 1 | **LDR** + 2 fils | luminosité (canal ADC E, pont 10 k sur carte) |
| msp | 2 | **DHT11/DHT22** | intérieur (GPIO26) + extérieur (GPIO15) |
| msp | 1 | **DS18B20 étanche** (version câble) | température sol/extérieur |
| msp | 1 | **Module capteur pluie** avec sortie **DO** | ⚠️ brancher DO, pas AO (GPIO27 = ADC2, inutilisable WiFi actif) |
| msp | 4 | **LDR** + 1 module humidité sol (AO) | LDR sur canaux A/C/D/E, module sur canal B |
| msp | 2 | **Servos** (type SG90/MG996R selon tracker) | tracker solaire G/D + H/B |

## 4. Connectique côté câble

- Connecteurs **JST-XH 3 p femelles volants** avec cosses à sertir (n3pp : 1 ; msp : 3)
  — ou câbles JST-XH pré-sertis (plus simple)
- **Pince à sertir** JST/Dupont (~20 €) si sertissage maison
- Fils **Dupont femelle-femelle** (servos, breakout J18, rail J19, modules I2C)
- Câble 2 conducteurs (LDR, rallonge DS18B20, batterie) + gaine ou goulotte
- Câble souple adapté aux charges 12/24 V des relais (section selon courant)

## 5. Alimentation et boîtier

| Quoi | Exigence |
|---|---|
| Alim **5 V / 3 A** jack 5,5/2,1 | qualité correcte (relais + servos + WiFi) |
| Boîtier ≥ 230×125 mm | extérieur/serre : **IP54** + presse-étoupes conseillés |
| Visserie | 4× M3 + entretoises (fixation carte) |
| (option batterie) | batterie + son chargeur ; mesure via bornier J27 (pont 2,2k/2,2k) ; **ne pas peupler les LED** (voir feuilles d'assemblage) |

## 6. Outillage et contrôle avant mise en service

- Fer à souder + étain + pompe à dessouder (tout est traversant)
- **Multimètre** — contrôle NO/COM/NC des relais à l'ohmmètre avant branchement
- Rappels montage : orientation du DevKit (sérigraphies « USB » côté bord,
  « ANTENNE » vers la zone dégagée — pas de détrompeur !), sonde DS18B20 à
  débrancher si le flash USB échoue (GPIO2 strapping), sortie **DO** du capteur
  de pluie, charges inductives 12/24 V avec diode/snubber côté charge.
