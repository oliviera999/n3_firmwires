# Carte porteuse ffp5cs `wroom-prod` (KiCad)

Plan PCB **généré depuis le code du firmware** `ffp5cs` (env PlatformIO `wroom-prod`,
`-DBOARD_WROOM`) : une **carte porteuse (carrier board)** pour module **ESP32 DevKit V1
30 broches** qui intègre tout le câblage aujourd'hui réalisé « en volant » — relais,
adaptation de niveau des HC-SR04, pull-ups, borniers.

```
pinmap.json                  Source de vérité (broches ← ffp5cs/include/pins.h, section WROOM)
kicad/ffp5cs-wroom-prod.kicad_pro / .kicad_sch / .kicad_pcb   Projet KiCad (format v8, ouvrable KiCad 8/9/10)
BOM.csv                      Nomenclature (séparateur ;)
generator/generate.py        Régénère schéma + PCB + BOM depuis pinmap.json
generator/footprints/        Empreintes officielles KiCad 8.0.9 vendorées (+ empreinte DevKit maison)
tools/check_pinmap_vs_firmware.py   Garde anti-dérive code ↔ plan (voir CI ci-dessous)
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
| Extension I2C (DS3231 = S3 uniquement, prévu quand même) | 21 / 22 | Support J14 |
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

## À vérifier avant fabrication (limites connues)

1. **Entraxe du DevKit** : l'empreinte suppose un DevKit V1 **30 broches, rangées
   espacées de 25,4 mm** (2 × supports 1×15). Mesurer votre exemplaire ; les variantes
   38 broches (DevKitC) ont un autre brochage → adapter `generate.py` (table `DEVKIT_A/B`).
2. **Routage** : le PCB est livré **placé et « ratsnesté » (chevelu), non routé**, avec
   deux plans de masse (F.Cu/B.Cu, à refondre dans KiCad : `B` puis clic droit → *Fill all
   zones*). Router les pistes 230 V/charge des relais en 2 mm mini avec isolation ≥ 3 mm
   si vous commutez du secteur (recommandé : rester en 12/24 V).
3. **Relais NO/NC** : mapping issu du couple symbole/empreinte officiel KiCad
   (SANYOU SRD Form C : bobine 2-5, COM 1, NO 3, NC 4). Contrôler à l'ohmmètre au
   premier montage avant de brancher une charge.
4. **ERC/DRC** : les étiquettes locales alimentent les nets (pas de power-flags) ; l'ERC
   émet des avertissements « input power pin not driven », sans conséquence.

## Régénérer / vérifier

```bash
cd hardware/ffp5cs-wroom-prod
python3 generator/generate.py                 # schéma + PCB + BOM depuis pinmap.json
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
