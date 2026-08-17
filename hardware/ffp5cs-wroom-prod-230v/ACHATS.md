# Liste d'achat — carte porteuse ffp5cs 230 V (6 canaux, rev 0.3)

Tout ce qu'il faut pour fabriquer, monter et mettre en service la carte
[`ffp5cs-wroom-prod-230v`](README.md) et son système. La section 2 est dérivée de
[`BOM.csv`](BOM.csv) (source de vérité, régénérée par `generator/generate.py`) ;
le reste couvre ce qu'une BOM ne liste jamais : périphériques, connectique côté
câble, boîtier, outillage.

> 💶 Budget indicatif hors outillage : **60-90 €** (dont ~la moitié pour
> PCB + DevKit + capteurs).

## 1. Circuit imprimé

| Quoi | Détail |
|---|---|
| PCB rev 0.3 | `exports/gerbers-ffp5cs-wroom-prod-230v-v0.3.zip` chez JLCPCB/PCBWay — 234×110 mm, 2 couches, qté mini 5 (~15-25 €) |
| ⚠️ Option **cuivre 2 oz (70 µm)** | fortement conseillée : à 10 A continus, le cuivre standard 1 oz chauffe de ~55-60 °C sur les pistes secteur ; en 2 oz on retombe à ~20 °C. Quelques euros de plus. |

## 2. Composants à souder (BOM)

### Actifs et électromécanique

| Qté | Référence | Rôle |
|---|---|---|
| 6 | Relais **SRD-05VDC-SL-C** (Songle/Sanyou, 5 V, SPDT 10 A/250 VAC) | commutation des 6 charges |
| 6 | Transistor **BC337-40** (TO-92) | commande des bobines (le GPIO ne fournit que ~12 mA, la bobine en veut 72) |
| 6 | Diode **1N4007** | roue libre bobine (absorbe la pointe inverse à la coupure) |
| 1 | Schottky **1N5822** | anti-retour VIN (USB et alim 5 V branchés ensemble sans conflit) |
| 6 | LED 5 mm rouge | témoin relais ON |
| 1 | LED 5 mm verte | présence 5 V |

### Résistances (axiales 1/4 W — un assortiment fait l'affaire)

| Qté | Valeur | Rôle |
|---|---|---|
| 16 | 1 kΩ | bases transistors ×6, LEDs ×7, séries écho ultrason ×3 |
| 10 | 10 kΩ | pull-downs base ×6, strapping GPIO12, pull-up DHT, pont LDR |
| 3 | 2 kΩ | ponts diviseurs écho HC-SR04 (5 V → 3,3 V) |
| 3 | 4,7 kΩ | pull-up 1-Wire DS18B20 + pull-ups I2C SDA/SCL |
| 2 | 220 Ω | séries signal servos |

### Condensateurs

| Qté | Valeur | Rôle |
|---|---|---|
| 1 | 1000 µF / 16 V radial | réservoir rail 5 V (pointes relais/servos/WiFi) |
| 1 | 470 µF / 16 V radial | découplage servos |
| 2 | 100 nF céramique | filtrage 3V3 capteurs + bus I2C |

### Connectique à souder

| Qté | Quoi | Usage |
|---|---|---|
| 7 | Bornier à vis **3 p, pas 5,08 mm**, ≥ 250 V/10 A | 6× sorties relais 230 V (NC/COM/NO) + 1× DS18B20 |
| 4 | Bornier à vis **2 p, pas 5,08 mm** | entrée 5 V, LDR, distribution 5 V, distribution 3V3 |
| 3 | Embase **JST-XH 4 p** verticale (B4B-XH-A) | capteurs ultrason AQUA/TANK/POTA |
| 1 | Embase **JST-XH 3 p** verticale (B3B-XH-A) | DHT11/DHT22 |
| 2 | **Barrette femelle 1×15**, pas 2,54 | supports du DevKit (il s'enfiche, jamais soudé) |
| 4 | Support femelle **1×4** | OLED + 3 ports I2C libres |
| 1 | Header mâle **1×12** | J17 — breakout de tous les GPIO libres |
| 1 | Header mâle **1×6** | J20 — rail alim Dupont (5V/GND/3V3) |
| 2 | Header mâle **1×3** | servos |
| 1 | Embase **jack DC 5,5/2,1 mm** traversante | entrée alim |

## 3. Cerveau et périphériques (hors carte)

| Qté | Quoi | Remarque |
|---|---|---|
| 1 | **ESP32 DevKit V1 30 broches** | ⚠️ bien **30** broches, rangées espacées de **25,4 mm** — pas la variante 36/38 broches (brochage différent). Mesurer avant de souder les supports. |
| 3 | **HC-SR04** (ou JSN-SR04T étanche) | niveaux d'eau aquarium / réservoir / potager |
| 1 | **DHT11** (ou DHT22) | température/humidité air |
| 1 | **DS18B20 étanche** (version câble) | température eau |
| 1 | **LDR** + 2 fils | luminosité, déportée |
| 2 | Servos (gros + petits) | distributeurs de nourriture existants |
| 1 | **OLED SSD1306 128×64 I2C** (0x3C) | affichage local |
| 0-3 | Modules I2C optionnels (DS3231, BME280…) | 3 ports libres J14/J21/J22 |

## 4. Connectique côté câble (le poste qu'on oublie)

- Connecteurs **JST-XH femelles volants** : 3× 4 p + 1× 3 p, **avec cosses à sertir**
  — ou câbles JST-XH pré-sertis (plus simple)
- **Pince à sertir** JST/Dupont (~20 €) si sertissage maison — l'outil le plus rentable du lot
- Fils **Dupont femelle-femelle** (servos, GPIO J17, rail J20)
- Câble souple **1,5 mm² H05VV-F** pour les départs 230 V vers les charges
- Câble 2 conducteurs (LDR, rallonge DS18B20) + gaine ou goulotte

## 5. Alimentation et partie 230 V — sécurité

| Quoi | Exigence |
|---|---|
| Alim **5 V / 3 A** jack 5,5/2,1 | qualité correcte (Mean Well ou équivalent) |
| **Boîtier fermé** ≥ 260×140 mm | idéalement **IP54** (humidité d'aquaponie) + **presse-étoupes** par câble 230 V |
| Protection amont | circuit sur **différentiel 30 mA** + fusible/disjoncteur ≤ 16 A — il n'y a pas de fusible sur la carte |
| **Vernis de tropicalisation** | zone secteur, après soudure et tests |
| Visserie | 4× M3 + entretoises (fixation carte) |

## 6. Outillage et contrôle avant mise en service

- Fer à souder + étain + pompe à dessouder (tout est traversant, soudable à la main)
- **Multimètre** — contrôle NC/COM/NO à l'ohmmètre avant tout branchement de charge
- **Testeur d'isolement** (location possible) — test 1,5-2 kV entre borniers secteur et
  masse logique avant la première mise sous tension
- Rappels montage : orientation du DevKit (sérigraphies « USB » côté bord, « ANTENNE »
  vers l'intérieur — pas de détrompeur !), charges inductives avec snubber/varistance
  côté charge, jamais d'intervention carte sous tension.
