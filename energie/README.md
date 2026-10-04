# Banc d'essai énergie — INA226 (ESP32-S3)

Firmware **de test sur banc** pour découvrir et valider les moniteurs de puissance **INA226** avant
leur intégration au module d'aquaponie mobile et autonome (futur ffp5cs S3, carte **n3-universal**).
Il suit trois voies — **production du panneau solaire**, **batterie plomb/AGM 12 V**, **consommation
des périphériques** — et envoie les mesures à la famille serveur **`energie`** (page temps réel +
courbes sur `/energie-test` pour le banc).

> Aucun firmware existant n'est modifié. La logique INA226 / énergie / batterie vit dans la brique
> partagée [`shared/n3_power`](../shared/n3_power/) (testée en natif), destinée à être reprise par ffp5cs.

## Matériel

| Élément | Choix |
|---------|-------|
| Carte | ESP32-S3-DevKitC-1 **N16R8** (même module que le profil S3 de ffp5cs). GPIO33-37 interdits (PSRAM octale). |
| I2C | **SDA = GPIO8, SCL = GPIO9**, 100 kHz (identique aux 3 pinmaps S3 de ffp5cs et au PCB n3-universal). |
| INA226 | Panneau **0x40** (A1=GND, A0=GND), batterie **0x41** (A0=VS), conso **0x44** (A1=VS). |
| Autres I2C possibles | OLED 0x3C, DS3231 0x68 + EEPROM 0x57, BME280 0x76 — aucune collision avec 0x40-0x4F. |
| Port série | port USB « UART » du DevKit, 115200 bauds (`ARDUINO_USB_CDC_ON_BOOT=0`, comme ffp5cs). |

Compatibilité **carte n3-universal** (le banc peut tourner sur DevKit nu ou sur la carte) :

- GPIO3 (GATE du rail **+3V3_SW** qui alimente les prises I2C J13/J14/J21/J22/J28) est mis à HAUT au
  boot — sans effet sur un DevKit nu. Si JP1 n'est pas en 1-2, ce rail est **coupé au boot**.
- Relais K1-K4 (GPIO15-18), K5 (**GPIO48 = LED RGB du DevKit**), K6 (GPIO45) forcés à BAS (OFF).
  Le firmware n'utilise jamais `LED_BUILTIN` / `neopixelWrite`.
- Si un INA perd son alimentation, son registre CAL revient à 0 (courant lu = 0) : le firmware relit
  CONFIG/CAL à chaque mesure et **ré-initialise** automatiquement (bit « ré-init » de `InaStatus`).
- La broche ALERT des INA n'est routée sur aucun PCB : lecture en **polling 1 Hz**.

### Shunts — point critique

La pleine échelle de l'INA226 est **±81,92 mV** sur le shunt :

| Shunt | Courant max |
|-------|-------------|
| 0,1 Ω (module bleu « R100 », défaut) | **0,82 A** |
| 0,01 Ω (R010) | 8,2 A |
| 5 mΩ | 16,4 A |

Le module réel (panneau 50-80 Wc ≈ 3-5 A, batterie, conso pompe/chauffage) dépasse 0,8 A sur les
**trois** voies : prévoir des **shunts externes de 5-10 mΩ** (fils de mesure Kelvin vers l'INA, le
fort courant ne passe pas par la carte logique). Le seuil « 0,01 Ω si > 3,2 A » de la doc
n3-universal est un chiffre INA219, pas INA226. Sur le banc, R100 suffit pour apprendre à faible
courant ; le firmware signale toute saturation (`SAT!` sur l'OLED, bit de `InaStatus`).

### Câblage recommandé

- Shunts **côté +** (high-side, mode commun INA226 0-36 V), **masse commune = négatif batterie** ;
  `VBUS` de chaque INA sur le point dont on veut la tension.
- **Régulateur MPPT** (en général à négatif commun) : INA panneau entre panneau + et entrée du
  régulateur → `PanneauV` = vraie tension panneau.
- **Régulateur PWM à positif commun** (négatif commuté) : le négatif du panneau n'est PAS la masse
  batterie ; `PanneauV` lit alors ≈ la tension batterie. Le courant (côté +) reste juste, la
  puissance devient « puissance livrée côté batterie ». À garder en tête en lisant les courbes.
- Convention de signe : **courant batterie > 0 = charge**, < 0 = décharge (`inv` si câblé à l'envers).
- Tension panneau à vide ≈ 22 V (plus par grand froid) : OK pour l'INA226 (36 V max).

## Secrets et build

Le banc utilise le **`credentials.h` racine** (le même que n3pp/msp : `WIFI_SSIDx/WIFI_PASSx`,
`API_KEY`, `API_SIG_SECRET`, `SMTP_*`) — copier `credentials.h.example` si besoin.

```bash
cd energie
pio run -e s3-bench            # banc : POST /energie-test/, OTA energie-test
pio run -e s3-bench -t upload
pio device monitor -e s3-bench # 115200 bauds
```

| Env | Rôle |
|-----|------|
| `s3-bench` (défaut) | Banc : HTTP, `TEST_MODE` → `/energie-test/post-data`, canal OTA test |
| `s3-bench-https` | Même chose en TLS (compile-check CI) |
| `s3-prod` | Futur module réel : HTTPS → `/energie/post-data`, canal OTA prod |

Toolchain `platformio/espressif32@6.13.0` (arduino-esp32 2.0.17) = celle de la cible ffp5cs S3,
sans son `custom_sdkconfig` (build ~40 s). Partitions `partitions_s3_16mb.csv` = copie de celles
du profil S3 de ffp5cs (images OTA ≤ 0x6F0000).

## Procédure de découverte pas à pas

1. **`scan`** — les trois INA doivent apparaître en 0x40 / 0x41 / 0x44 (sinon : adresse A0/A1,
   alimentation 3,3 V du module, pull-ups).
2. **`dump bat`** — `MFG=0x5449 TI`, `DIE=0x2260 INA226`, `CAL` = valeur attendue (0 = config perdue).
   Le dump donne aussi la tension shunt brute et le courant recalculé via R — utile pour vérifier le shunt.
3. **`cfg`** — calibration de chaque voie (CAL, LSB courant, courant max mesurable, avertissements).
4. Si le shunt n'est pas un R100 : **`shunt <voie> <ohm>`** puis **`imax <voie> <A>`**.
5. Courant stable connu (charge résistive) : comparer au multimètre puis **`cal <voie> <A_multimètre>`**
   (corrige le gain, borné ×0,5-×2, persisté en NVS).
6. Vérifier le **sens** : batterie en charge → `BatterieI` > 0, sinon **`inv bat 1`**.
7. Suivre les courbes sur **`/energie-test`** (envoi toutes les 10 s).

## Console série (taper `help`)

| Commande | Effet |
|----------|-------|
| `scan` | scan du bus I2C avec le nom probable de chaque adresse |
| `cfg` | réglages, calibration, état batterie / alerte / réseau |
| `dump <voie>` | registres bruts et décodés de l'INA226 |
| `csv on\|off` | trame `csv,ms,PanneauV,PanneauI,PanneauP,BatterieV,…,SoC,BatterieAh` à 1 Hz |
| `shunt` / `imax` / `inv` / `gain` / `cal` | réglages par voie (persistés en NVS) |
| `avg <n>` | moyennage matériel (128 par défaut ≈ 282 ms : lisse le hachage d'un régulateur PWM) |
| `soc <pct>` | impose l'état de charge batterie |
| `reset` | remet à zéro les cumuls Wh/Ah |
| `post` / `ota` / `mailtest` | envoi serveur, vérification OTA, mail de test immédiats |
| `factory` / `reboot` | efface les réglages NVS (retour à `energie_config.h`) / redémarre |

`<voie>` = `0|1|2` ou `pan|bat|con`.

## Ce qui est envoyé au serveur (toutes les 10 s)

Contrat détaillé côté serveur : `docs/API_ENERGIE.md` (dépôt n3_serveur). Résumé :

- `PanneauV/I/P/Imax`, `BatterieV/Vmin/I/Imin/Imax/P`, `ConsoV/I/P/Imax` : moyennes et extrêmes de la fenêtre.
- `EnergiePanneauWh`, `EnergieConsoWh` : énergie de la fenêtre (en cas d'échec d'envoi, reportée
  sur l'envoi suivant → les sommes restent justes).
- `BatterieAh` : charge nette intégrée depuis le boot (ou `reset`) ; `BatterieSoc` : état de charge (%).
- `BatterieVadc` : pont diviseur ADC_VBAT de la carte (GPIO7, 100k/27k), vide si désactivé
  (`ENERGIE_PIN_ADC_VBAT`). ⚠️ Ce pont sature vers 14,4-14,7 V (absorption AGM) : l'INA reste la référence.
- `InaStatus` : bit *i* = voie présente, bit 4+*i* = saturation shunt, bit 8+*i* = ré-initialisation
  (voie 0 panneau, 1 batterie, 2 conso) ; `I2cErreurs`, `Rssi`, `Uptime`, `FreeHeap`, `BootCount`.

## État de charge et alerte batterie (plomb/AGM 12 V)

- SoC = **tension de repos (table OCV AGM)** recalée après 30 min de repos (|I| < 0,12 A ≈ C/100)
  + **comptage coulométrique** entre deux recalages (capacité 12 Ah, rendement de charge 0,90).
  Sans valeur en NVS, le premier SoC vient de l'OCV au boot (estimation grossière, `~` dans `cfg`).
- Alerte mail (SMTP de `credentials.h`) : tension < **11,8 V** tenue 60 s hors charge, réarmement
  au-dessus de **12,4 V**, au plus une alerte toutes les **6 h**. Seuils dans `energie_config.h`.
- La doc n3-universal suppose une batterie **gel** ; la batterie retenue est **AGM** → régler le
  profil du régulateur en conséquence.
