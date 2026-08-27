# Étude pinmap « n3-universal » — verdict machine

Générée par `etude_pinmap.py` (source de vérité : ce script).


## WROOM (site A1) — ✅ TIENT

- 22 nets affectés, 1 broche(s) libre(s) : [12]
- ⚠️ Broches à précaution documentée :
  - GPIO2 (ONEWIRE) : strapping : éviter pull-up fort pendant le flash UART
  - GPIO15 (DHT_INT) : strapping : doit être haut au boot (pull-up OK)

## S3 pragmatique (site A2) — ✅ TIENT

- 26 nets affectés, 0 broche(s) libre(s) : []
- ⚠️ Broches à précaution documentée :
  - GPIO3 (GATE) : strapping JTAG-sel : OK si JTAG inutilisé ; pull-up admissible
  - GPIO38 (DHT_INT) : LED RGB DevKitC-1 v1.1 : scintillement cosmétique si utilisé
  - GPIO45 (AUX2) : strapping VDD_SPI : NE JAMAIS tirer haut au boot (pas de pull-up !)
  - GPIO48 (AUX1) : LED RGB DevKitC-1 v1.0 : scintillement cosmétique si utilisé

## S3 stricte (site A2) — ❌ 4 problème(s)

- 26 nets affectés, 0 broche(s) libre(s) : []
- ❌ S3 stricte (site A2): GPIO3 (GATE) hors des broches admissibles
- ❌ S3 stricte (site A2): GPIO38 (DHT_INT) hors des broches admissibles
- ❌ S3 stricte (site A2): GPIO48 (AUX1) hors des broches admissibles
- ❌ S3 stricte (site A2): GPIO45 (AUX2) hors des broches admissibles

## WROOM + SD universelle (option) — ✅ TIENT

- 23 nets affectés, 0 broche(s) libre(s) : []
- ⚠️ Broches à précaution documentée :
  - GPIO2 (ONEWIRE) : strapping : éviter pull-up fort pendant le flash UART
  - GPIO12 (SD_MISO) : strapping MTDI : ligne MISO SANS pull-up (socket nu)
  - GPIO14 (US3) : net US3 = SD_CS — ffp5cs-WROOM arbitre par env (wroom-sd)
  - GPIO15 (DHT_INT) : strapping : doit être haut au boot (pull-up OK)
  - GPIO23 (AUX1) : net AUX1 = SD_CLK — idem
  - GPIO25 (AUX2) : net AUX2 = SD_MOSI — idem

## Nets partagés entre rôles (firmwares disjoints par construction)

- `ADC_A` : **msp**=LUMINOSITEa / **n3pp**=Humid1
- `ADC_B` : **msp**=LUMINOSITEb / **n3pp**=Humid2
- `ADC_C` : **msp**=LUMINOSITEc / **n3pp**=Humid3
- `ADC_D` : **msp**=LUMINOSITEd / **n3pp**=Humid4
- `ADC_E` : **msp**=HumiditeSol / **n3pp**=Luminosite / **ffp5cs**=LUMINOSITE (LDR)
- `ADC_VBAT` : **msp**=pontdiv / **n3pp**=pontdiv
- `DHT_INT` : **msp**=DHTPININT / **n3pp**=DHT / **ffp5cs**=DHT_PIN
- `GATE` : **msp**=RELAIS (gate) / **n3pp**=RELAIS (gate)
- `I2C_SCL` : **msp**=OLED/BME280 / **n3pp**=OLED/BME280 / **ffp5cs**=OLED/DS3231/INA
- `I2C_SDA` : **msp**=OLED/BME280 / **n3pp**=OLED/BME280 / **ffp5cs**=OLED/DS3231/INA
- `K1` : **n3pp**=pompe arrosage / **ffp5cs**=POMPE_AQUA
- `ONEWIRE` : **msp**=TempEau (DS18B20) / **ffp5cs**=DS18B20
- `SERVO1` : **msp**=SERVOGD (tracker) / **ffp5cs**=SERVO_GROS
- `SERVO2` : **msp**=SERVOHB (tracker) / **ffp5cs**=SERVO_PETITS
- `US1` : **ffp5cs**=ULTRASON_AQUA / **msp**=PLUIE (DO)
- `US2` : **ffp5cs**=ULTRASON_TANK / **msp**=DHT_EXT (legacy, préférer BME280 0x77)

## Ajouts I2C (coût GPIO nul)

- RTC **DS3231** (0x68) — support dédié ; déjà géré par ffp5cs (`USE_RTC_DS3231`).
- **2-3 × INA219/226** (0x40/0x41/0x44) — mesure courant panneau/batterie/charge ;
  à alimenter sur **+3V3_SW** (0,7-1 mA chacun sinon en veille).
- BME280 0x76/0x77, OLED 0x3C : inchangés. 7 périphériques I2C = charge de bus OK à 100 kHz.

## microSD

- Slot **unique**, câblé au site S3 (natif, GPIO 10/12/13/14) ET au site WROOM sur
  CS=14(US3), CLK=23(AUX1), MOSI=25(AUX2), MISO=12 (sans pull-up).
- **msp et n3pp ne renoncent à RIEN** : US3/AUX1/AUX2 ne sont utilisés que par ffp5cs.
- **ffp5cs-sur-WROOM arbitre par env de build** : `wroom-sd` = SD active (renonce à
  US_POTA + AUX1/AUX2) ; env standard = 3 ultrasons + AUX, sans SD. En S3 : pas d'arbitrage.
- Côté firmware : ouvrir la SD hors `BOARD_S3` (budget flash OK) ; pour msp/n3pp,
  ajouter un module de journal (lib partagée) — la flash interne (LittleFS) reste
  une alternative sans matériel. Horloge SPI ≤ ~10 MHz (stubs vers JST/headers).
