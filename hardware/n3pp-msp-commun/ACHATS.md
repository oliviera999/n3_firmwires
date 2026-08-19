# Liste d'achat — carte commune n3pp + msp (rev 0.2)

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
| PCB rev 0.2 | `exports/gerbers-n3pp-msp-commun-v0.2.zip` chez JLCPCB/PCBWay — 210×105 mm, 2 couches, qté mini 5 (~10-20 €). Finition standard 1 oz suffisante. |
| ⚠️ Charges relais | Carte **sans zone secteur isolée** : charges **12/24 V uniquement** sur les borniers relais (pompes, éclairage LED, électrovannes BT). Pour du **230 V**, utiliser la carte dédiée [`ffp5cs-wroom-prod-230v`](../ffp5cs-wroom-prod-230v/ACHATS.md). |

## 2. Composants à souder (BOM par profil)

### 2a. Socle COMMUN (à souder dans tous les cas)

| Qté | Référence | Rôle |
|---|---|---|
| 1 | P-MOSFET **NDP6020P** (TO-220, logic-level) | Q7 — interrupteur haut du rail capteurs `+3V3_SW` (GPIO13) |
| 1 | P-MOSFET **BS250** (TO-92, brochage D-G-S) | Q8 — interrupteur haut du pont diviseur batterie |
| 2 | Transistor **BC337-40** (TO-92) | Q1 (grille Q7) + Q9 (grille Q8) |
| 1 | Schottky **1N5822** | anti-retour VIN (USB et alim 5 V ensemble sans conflit) |
| 1 | LED 5 mm rouge + 1 verte | témoin rail capteurs + présence 5 V (DNP si profil batterie) |
| 3 | Résistance 1 kΩ 1/4 W | base Q1, LED ×2 |
| 2 | Résistance 10 kΩ | pull-down base Q1 (rail coupé par défaut), pont LDR canal E |
| 3 | Résistance 100 kΩ | pull-ups grilles Q7/Q8 + base Q9 |
| 2 | Résistance 4,7 kΩ | pull-ups I2C SDA/SCL |
| 2 | Résistance 2,2 kΩ | pont diviseur batterie → GPIO36 (`n3_defaults.h`) |
| 1 | Condensateur 1000 µF/16 V radial | réservoir rail 5 V (relais + WiFi) |
| 1 | Condensateur 470 µF/16 V radial | C5 — réservoir rail 3V3 (pointes WiFi en alim batterie/LDO) |
| 2 | Condensateur 100 nF céramique | découplage rail capteurs + I2C |
| 1 | Embase **jack DC 5,5/2,1 mm** | entrée alim |
| 1 | Header **1×2** + cavalier | JP1 — isole le rail 3V3 de J29 (flash USB sur station batterie) |
| 4 | Bornier à vis **2 p, pas 5,08 mm** | entrée 5 V, mesure batterie, distribution 5 V, entrée/distribution 3V3 |
| 5 | Bornier à vis **3 p, pas 5,08 mm** | 5 entrées ADC (3V3_SW/AIN/GND) |
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
| les deux | 0-2 | **BME280 I2C** (temp+hum+**pression**) | remplace le(s) DHT sans reflash (firmwares msp ≥ 2.73 / n3pp ≥ 4.69 : détection auto 0x76/0x77, repli DHT). Sur port I2C libre, **fils Dupont croisés** (ports GND/VCC/SCL/SDA vs modules VIN/GND/SCL/SDA) |
| les deux | 0-3 | Autres modules I2C optionnels (DS3231…) | 3 ports libres J15/J16/J17 |
| n3pp | 0-1 | **DHT11/DHT22** (ou BME280 I2C, voir ci-dessus) | air serre (GPIO18) |
| n3pp | 4 | **Capteurs humidité sol capacitifs** (sortie AO) | canaux ADC A-D |
| n3pp | 1 | **LDR** + 2 fils | luminosité (canal ADC E, pont 10 k sur carte) |
| msp | 0-2 | **DHT11/DHT22** (ou BME280 I2C, voir ci-dessus) | intérieur (GPIO26) + extérieur (GPIO15) — bus I2C < ~50 cm : garder un DHT pour un capteur très déporté |
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
| (option batterie) | voir §5 bis ci-dessous — **ne jamais brancher la 18650 brute sur J1/jack** |

## 5 bis. Alimentation batterie 18650 + solaire (TP4056)

La carte n'a **pas d'entrée batterie directe** : son rail 5 V attend du vrai 5 V.
⚠️ **Ne jamais injecter la 18650 (3,0–4,2 V) dans J1 ou le jack** : après la
Schottky D5 et le dropout du régulateur AMS1117 du DevKit, le 3,3 V s'effondre
sous les pointes WiFi (resets aléatoires), et les relais SRD-**05** ne collent
pas de façon fiable sous 5 V.

### Deux branchements corrects

**Voie A — la station utilise relais et/ou servos** (pompe n3pp, tracker msp) :

```
panneau 5-6V ─> TP4056 (protégé) ─> module BOOST 5V (MT3608) ─> bornier J1 (+5V/GND)
                    └─ B± : 18650            └─ VBAT ─> bornier J27 (mesure)
```

**Voie B — station capteurs pure en deep sleep** (relais/servos non peuplés,
profil batterie des feuilles d'assemblage) :

```
panneau 5-6V ─> TP4056 (protégé) ─> LDO 3,3V HT7333 ─> bornier J29 ─[JP1 fermé]─> rail 3V3
                    └─ B± : 18650        └─ VBAT ─> bornier J27 (mesure)
```

La voie B court-circuite l'AMS1117 du DevKit (~5 mA de quiescent à lui seul) :
c'est la voie économe. Pour flasher en USB : **retirer le cavalier JP1** (il
isole le LDO du rail), flasher, remettre JP1. Le rail 5 V reste mort — voulu.

> 🔧 **Palier 2 (optionnel, recommandé en voie B)** : dessouder la **LED
> d'alimentation du DevKit** (ou sa résistance série, plus facile à saisir)
> économise encore ~2-3 mA de veille. Le plancher restant est la puce
> USB-série : ~1-1,5 mA (CP2102) ou ~3-4 mA (CH340) — **mesurer au multimètre**
> en série sur le fil batterie, et préférer les DevKit à CP2102 à l'achat.
> Un DevKit ainsi modifié n'a plus de témoin lumineux : le réserver aux
> stations batterie.

### À acheter en plus (par station sur batterie)

| Qté | Quoi | Remarque |
|---|---|---|
| 1 | **TP4056 avec protection** (DW01+FS8205, USB-C) | charge sur **OUT±**, jamais sur B± ; caveat connu : charge + conso simultanées faussent la fin de charge (acceptable ici) |
| 1 | **18650** ≥ 2500 mAh + support | cellule de marque (capacité réelle) |
| 1 | Panneau solaire **5-6 V, 1-2 W** (voie B) ou **2-3 W** (voie A) | 2 W = confort hiver |
| 1 | Voie B : LDO **HT7333** (TO-92) + 2 condensateurs 10 µF | ou MCP1700-3302 |
| 1 | Voie A : module boost **MT3608** réglé à 5,0 V | régler AVANT de brancher la carte |

### Autonomie — ordres de grandeur (18650 ~2000 mAh utiles, réveil 300 s par défaut)

Les firmwares dorment en deep sleep entre deux envois (`FreqWakeUp` = 300 s par
défaut, ajustable par le serveur) ; chaque réveil ≈ 10-15 s de WiFi actif
(~0,4 mAh). Sous le seuil batterie, le firmware bascule en sommeil d'urgence.

Depuis la rev 0.2, le rail capteurs `+3V3_SW` **et le pont diviseur** sont
coupés automatiquement en deep sleep (GPIO13) : en veille il ne reste que le
DevKit lui-même.

| Configuration (rev 0.2) | Veille | Conso/jour | Autonomie SANS soleil |
|---|---|---|---|
| Voie B, réveil 5 min | ~10 mA (DevKit brut) | ~360 mAh | ~5 jours |
| Voie B **+ palier 2** (LED DevKit ôtée, CP2102), 5 min | ~1,3 mA | ~150 mAh | **~2 semaines** |
| Voie B + palier 2, réveil 15 min | ~1,3 mA | ~71 mAh | **~4 semaines** |
| Voie B + palier 2, réveil 30 min | ~1,3 mA | ~51 mAh | ~40 jours |
| Voie A (boost + AMS1117), réveil 5 min | ~7-10 mA | ~300-360 mAh | ~1 semaine |
| Mode éveillé permanent (`WakeUp=1` serveur) | — | ~2900 mAh | **< 1 jour** — à réserver au secteur |

Un panneau 1-2 W bien exposé récolte ~250-400 mAh/j (ciel moyen) : il couvre
largement la voie B à 5 min ; l'hiver, allonger `FreqWakeUp` donne de la marge.

> 📝 **Note pont diviseur** : depuis la rev 0.2 il est **commuté** (Q8/Q9,
> actif seulement rail capteurs allumé) — ses ~0,85 mA ne comptent plus en
> veille, et les 2,2k/2,2k de `n3_defaults.h` gardent leur précision ADC.
> Le poste dominant est désormais le DevKit lui-même (d'où le palier 2),
> puis les réveils WiFi (d'où la cadence).

## 6. Outillage et contrôle avant mise en service

- Fer à souder + étain + pompe à dessouder (tout est traversant)
- **Multimètre** — contrôle NO/COM/NC des relais à l'ohmmètre avant branchement
- Rappels montage : orientation du DevKit (sérigraphies « USB » côté bord,
  « ANTENNE » vers la zone dégagée — pas de détrompeur !), sonde DS18B20 à
  débrancher si le flash USB échoue (GPIO2 strapping), sortie **DO** du capteur
  de pluie, charges inductives 12/24 V avec diode/snubber côté charge.
