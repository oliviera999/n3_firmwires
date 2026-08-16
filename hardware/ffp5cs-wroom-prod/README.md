# Carte porteuse ffp5cs `wroom-prod` (KiCad)

Plan PCB **généré depuis le code du firmware** `ffp5cs` (env PlatformIO `wroom-prod`,
`-DBOARD_WROOM`) : une **carte porteuse (carrier board)** pour module **ESP32 DevKit V1
30 broches** qui intègre tout le câblage aujourd'hui réalisé « en volant » — relais,
adaptation de niveau des HC-SR04, pull-ups, borniers.

```
pinmap.json                  Source de vérité (broches ← ffp5cs/include/pins.h, section WROOM)
kicad/ffp5cs-wroom-prod.kicad_pro / .kicad_sch / .kicad_pcb   Projet KiCad (format v8, ouvrable KiCad 8/9/10)
BOM.csv                      Nomenclature (séparateur ;)
exports/gerbers-*.zip        Fichiers de fabrication (Gerbers + perçages Excellon)
exports/schema.pdf, pcb-*.svg   Rendus pour revue sans KiCad
fritzing/*.fzz               Vue breadboard du câblage (Fritzing >= 1.0)
generator/generate.py        Régénère schéma + PCB (non routé) + BOM depuis pinmap.json
generator/generate_fritzing.py  Régénère le .fzz de câblage
generator/footprints/        Empreintes officielles KiCad 8.0.9 vendorées (+ empreinte DevKit maison)
tools/check_pinmap_vs_firmware.py   Garde anti-dérive code ↔ plan
```

## Ce que couvre la carte (extrait du firmware)

| Fonction (code) | GPIO | Réalisation sur la carte |
|---|---|---|
| `Pins::POMPE_AQUA` (`actuators.h`, actif HAUT) | 16 | Relais K1 SRD-05 + BC337, bornier NO/COM/NC |
| `Pins::POMPE_RESERV` | 18 | Relais K2, idem |
| `Pins::RADIATEURS` | 2 | Relais K3, idem (strapping → pull-down base) |
| `Pins::LUMIERE` | 15 | Relais K4, idem (strapping) |
| `Pins::SERVO_GROS` | 12 | Header servo J15, série 220 Ω + **pull-down 10k obligatoire** (MTDI) |
| `Pins::SERVO_PETITS` | 13 | Header servo J16, série 220 Ω |
| `Pins::ULTRASON_AQUA/TANK/POTA` (mono-broche TRIG=ECHO, `sensor_ultrasonic.cpp`) | 4 / 19 / 33 | JST 4 pts J7-J9, écho 5 V → 3,3 V par pont 1k/2k |
| `Pins::DHT_PIN` (DHT11, `-DUSE_DHT22` en option) | 27 | JST 3 pts J10 + pull-up 10k |
| `Pins::ONE_WIRE_BUS` (DS18B20) | 26 | Bornier J11 + pull-up 4,7k |
| `Pins::LUMINOSITE` (ADC1, entrée seule) | 34 | Bornier J12 pour LDR déportée + 10k vers GND |
| `Pins::I2C_SDA/SCL` — OLED SSD1306 0x3C (`FEATURE_OLED=1`) | 21 / 22 | Support J13 (GND/VCC/SCL/SDA) + pull-ups 4,7k |
| Extension I2C : **3 ports libres** (DS3231, capteurs…) | 21 / 22 | Supports J14, J18, J19 (même brochage GND/VCC/SCL/SDA) |
| GPIO libres (5, 14, 17, 23, 25, 32, 35) + EN | — | Header J17 |

Alimentation : **5 V / 3 A** (jack 5,5/2,1 centre + OU bornier), condensateur réservoir
1000 µF, LED de présence. Le DevKit est alimenté par VIN **à travers une Schottky
1N5822** : on peut brancher l'USB (flash/debug) alim 5 V connectée sans retour de courant.
Les capteurs logiques (DHT, DS18B20, LDR, OLED) sont sur le **3V3 du DevKit** ;
HC-SR04, servos et relais sur le rail 5 V.

## Choix de conception (et pourquoi)

- **Carrier board plutôt que carte à ESP32 soudé** : le module `esp32dev` reste flashable
  par son USB (`pio run -e wroom-prod -t upload`), remplaçable en 10 s, et on évite tout
  le circuit USB-UART/auto-program. 100 % traversant → soudable à la main, fabricable
  partout (JLCPCB/PCBWay, 2 couches, 150 × 100 mm).
- **Relais commandés actif-HAUT** via NPN : conforme à `actuators.h`
  (`on()` → `digitalWrite(HIGH)`). Ne PAS utiliser un module relais du commerce
  actif-BAS sur ces sorties sans adaptation.
- **Broches de strapping** : GPIO2 et GPIO15 (relais) ont un pull-down 10k sur la base du
  transistor → niveau bas au boot, flash série non perturbé. GPIO12 (MTDI, servo gros) a
  un pull-down 10k dédié (R22) : un niveau haut au boot sélectionnerait un VDD_SDIO à
  1,8 V et **brickerait le démarrage**.
- **HC-SR04 en mono-broche** : le firmware pilote TRIG et lit ECHO sur le même GPIO.
  Câblage par canal : GPIO —(direct)— TRIG ; ECHO —1k— GPIO ; GPIO —2k— GND.
  L'écho 5 V est ramené à ~3,3 V, le trigger 3,3 V est accepté par le capteur alimenté en 5 V.
- **GPIO34 entrée seule, sans pull-up interne** : pont diviseur LDR obligatoire sur la
  carte (LDR déportée entre 3V3 et l'entrée, 10k vers GND).
- **RX0/TX0 laissés libres** (non routés) : réservés à l'USB-UART du module.
- **3 ports I2C libres** (J14/J18/J19) en plus de l'OLED : avec les pull-ups 4,7 k
  déjà en place, 3-4 modules simultanés sont confortables (adresses différentes,
  bus total < ~50 cm) — au-delà, réduire les pull-ups à 2,2 k.

## À vérifier avant fabrication (limites connues)

1. **Entraxe du DevKit** : l'empreinte suppose un DevKit V1 **30 broches, rangées
   espacées de 25,4 mm** (2 × supports 1×15). Mesurer votre exemplaire ; les variantes
   38 broches (DevKitC) ont un autre brochage → adapter `generate.py` (table `DEVKIT_A/B`).
2. **Routage (rev 0.5, dimensionné 12/24 V)** : PCB routé par `generator/route_lv.py`
   (freerouting + vias de couture GND + reliefs thermiques) avec des
   **classes de nets** — contacts relais NO/COM/NC en **2 mm** (≈ 5-6 A continus en
   cuivre 1 oz, marge correcte jusqu'à ~8 A en pointe), rails +5V/VIN/GND en
   **1,2 mm**, signaux 0,4 mm. DRC KiCad 8 : 0 violation, 0 non-connecté ; Gerbers
   dans `exports/`. **Le 230 V reste interdit sur cette carte** (isolement non prévu :
   plans de masse proches des contacts, pas de fentes ni de zone secteur dédiée) —
   la sérigraphie le rappelle près des borniers. Pour du secteur, piloter des modules
   relais/contacteurs 230 V déportés. NB : `generate.py` régénère un PCB **non
   routé** — ne pas régénérer par-dessus `kicad/` sans vouloir perdre le routage.
3. **Relais NO/NC** : mapping issu du couple symbole/empreinte officiel KiCad
   (SANYOU SRD Form C : bobine 2-5, COM 1, NO 3, NC 4). Contrôler à l'ohmmètre au
   premier montage avant de brancher une charge.
4. **ERC/DRC** : les étiquettes locales alimentent les nets (pas de power-flags) ; l'ERC
   émet des avertissements « input power pin not driven », sans conséquence.

## Corrections rev 0.5

- **Supports du DevKit dans la BOM** (ligne « A1 (supports) » : 2 barrettes femelles
  1×15) — jusque-là seulement mentionnés en description.
- **Zone dégagée sous l'antenne WiFi** : keepout cuivre (pistes/vias/pour) sur les
  deux faces sous le débord antenne du module, note sérigraphiée déplacée côté antenne.
- **Checker étendu** : `tools/check_pinmap_vs_firmware.py` fige désormais aussi la
  topologie des canaux HC-SR04 (JST 1=+5V 2=GPIO 3=ECHO 4=GND, 1 k série écho,
  2 k pull-down) — le câblage capteur ne peut plus dériver silencieusement.
- GPIO 23/25 : déclarés `AUX1`/`AUX2` dans `pinmap.json` (firmware ≥ v15.26) ; sur
  cette carte ils restent en breakout J17 (piloter des modules relais externes) —
  les relais AUX embarqués sont sur la variante 230 V.

## Vue Fritzing (câblage de prototype)

`fritzing/ffp5cs-wroom-prod-cablage.fzz` (Fritzing ≥ 1.0) montre le **câblage
breadboard** périphériques ↔ DevKit : HC-SR04 + ponts 1k/2k, DS18B20, LDR, servos,
pull-ups, avec le code couleur rouge = 5 V, orange = 3V3, noir = GND. Le DevKit, le
DHT11, l'OLED et les sorties relais sont figurés par des **headers génériques
étiquetés** (pas de pièce core fidèle dans Fritzing) — survoler une pièce affiche son
titre avec l'ordre des broches. Fichier vérifié à l'ouverture (« Routing completed »,
aucune pièce manquante) ; les positions se réarrangent librement, les connexions
suivent. Le projet KiCad reste la référence pour la fabrication.

## Régénérer / vérifier

```bash
cd hardware/ffp5cs-wroom-prod
python3 generator/generate.py                 # schéma + PCB placé (NON routé) + BOM
python3 generator/route_lv.py                 # routage complet + vérifications
python3 tools/check_pinmap_vs_firmware.py     # cohérence pins.h (WROOM) ↔ pinmap ↔ schéma ↔ PCB
```

Toute modification de `ffp5cs/include/pins.h` (section WROOM) doit être répercutée dans
`pinmap.json` puis régénérée — le checker échoue (exit ≠ 0) tant que les trois ne sont
pas alignés. ⚠️ La régénération **écrase** `kicad/` : une fois le routage manuel commencé,
faire les retouches dans KiCad et cesser de régénérer (ou porter les retouches dans
`generate.py`).

## Licences

Les empreintes de `generator/footprints/` proviennent de la
[bibliothèque officielle KiCad](https://gitlab.com/kicad/libraries/kicad-footprints)
(tag 8.0.9), licence CC-BY-SA 4.0 avec exception d'usage pour les designs ;
`ESP32_DevKit_V1_30pin.kicad_mod` est généré par `generate.py`.
