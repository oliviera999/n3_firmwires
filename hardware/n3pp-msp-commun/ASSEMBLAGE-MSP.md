# Feuille d'assemblage — profil msp (station météo)

> Générée par `generator/generate.py` — ne pas éditer à la main.
> Carte commune n3pp + msp rev 0.1 : un seul PCB, deux profils de peuplement.
> Le firmware msp actuel fonctionne SANS modification sur ce profil.

## À souder (51 composants)

| Réf | Valeur | Description |
|---|---|---|
| A1 | ESP32 DevKit V1 | Module ESP32-WROOM-32 DevKit V1 30 broches, sur 2 supports 1x15 |
| C1 | 1000u/16V | Réservoir rail 5V (relais + servos + WiFi) |
| C2 | 470u/16V | Découplage rail 5V servos |
| C3 | 100n | Découplage 3V3 capteurs |
| C4 | 100n | Découplage 3V3 I2C |
| D1 | 1N4007 | Diode de roue libre bobine |
| D5 | 1N5822 | Schottky 3A vers VIN DevKit (anti-retour si USB branché) |
| J1 | Bornier_5.08 | Entrée 5V alternative (bornier, 1=+5V 2=GND) |
| J2 | Jack 5.5/2.1 | Entrée 5V 3A (jack, centre = +) |
| J3 | Bornier_5.08 | Bornier charge (1=NO 2=COM 3=NC) |
| J10 | JST-XH | DHT INT msp (1=3V3 2=DATA 3=GND) |
| J11 | JST-XH | DHT EXT msp (1=3V3 2=DATA 3=GND) |
| J12 | Bornier_5.08 | Sonde DS18B20 étanche (1=3V3 2=DATA 3=GND) |
| J13 | JST-XH | Capteur pluie — sortie NUMERIQUE DO (1=3V3 2=DO 3=GND) |
| J14 | Support OLED | OLED SSD1306 128x64 I2C 0x3C (1=GND 2=VCC 3=SCL 4=SDA) |
| J15 | Support I2C libre | Port I2C libre 1 (1=GND 2=VCC 3=SCL 4=SDA) |
| J16 | Support I2C libre | Port I2C libre 2 (1=GND 2=VCC 3=SCL 4=SDA) |
| J17 | Support I2C libre | Port I2C libre 3 (1=GND 2=VCC 3=SCL 4=SDA) |
| J18 | Header GPIO libres | GPIO libres (1=3V3 2=GND 3=EN 4=IO4 5=IO5 6=RX0 7=TX0) |
| J19 | Rail alim Dupont | Rail alim modules (1=5V 2=5V 3=GND 4=GND 5=3V3 6=3V3) |
| J20 | Header servo | Servo G/D tracker (1=SIG 2=+5V 3=GND) |
| J21 | Header servo | Servo H/B tracker (1=SIG 2=+5V 3=GND) |
| J22 | Bornier_5.08 | Entrée ADC_A (1=3V3 2=AIN 3=GND) — n3pp: humidite1 (sol) / msp: LUMINOSITEa (LDR) |
| J23 | Bornier_5.08 | Entrée ADC_B (1=3V3 2=AIN 3=GND) — n3pp: humidite2 (sol) / msp: HumiditeSol (module AO) |
| J24 | Bornier_5.08 | Entrée ADC_C (1=3V3 2=AIN 3=GND) — n3pp: humidite3 (sol) / msp: LUMINOSITEc (LDR) |
| J25 | Bornier_5.08 | Entrée ADC_D (1=3V3 2=AIN 3=GND) — n3pp: humidite4 (sol) / msp: LUMINOSITEb (LDR) |
| J26 | Bornier_5.08 | Entrée ADC_E (1=3V3 2=AIN 3=GND) — n3pp: LUMINOSITE (LDR) / msp: LUMINOSITEd (LDR) |
| J27 | Bornier_5.08 | Mesure batterie (1=VBAT 2=GND, pont 2.2k/2.2k -> GPIO36) |
| J28 | Bornier_5.08 | Distribution 5V (1=+5V 2=GND) |
| J29 | Bornier_5.08 | Distribution 3V3 (1=+3V3 2=GND) |
| K1 | SRD-05VDC-SL-C | Relais 5V SPDT 10A (Songle/Sanyou SRD) |
| LED1 | rouge | LED témoin relais ON (DNP si profil batterie) |
| LED5 | verte | LED présence 5V (DNP si profil batterie) |
| Q1 | BC337-40 | Transistor NPN commande relais (1=C 2=B 3=E) |
| R1 | 1k | Résistance base transistor |
| R5 | 10k | Pull-down base (état sûr au boot) |
| R9 | 1k | Résistance LED témoin (DNP si profil batterie) |
| R13 | 1k | Résistance LED présence 5V (DNP si profil batterie) |
| R14 | 10k | Bas de pont ADC_A (peupler si LDR ; DNP si capteur AO) |
| R16 | 10k | Bas de pont ADC_C (peupler si LDR ; DNP si capteur AO) |
| R17 | 10k | Bas de pont ADC_D (peupler si LDR ; DNP si capteur AO) |
| R18 | 10k | Bas de pont ADC_E (peupler si LDR ; DNP si capteur AO) |
| R20 | 220 | Série signal servo G/D (GPIO25) |
| R22 | 10k | Pull-up data DHT INT msp |
| R23 | 10k | Pull-up data DHT EXT msp |
| R24 | 4.7k | Pull-up bus 1-Wire DS18B20 |
| R25 | 4.7k | Pull-up I2C SDA |
| R26 | 4.7k | Pull-up I2C SCL |
| R27 | 220 | Série signal servo H/B (GPIO14) |
| R34 | 2.2k | Pont batterie haut (N3_BATTERY_R1=2200) |
| R35 | 2.2k | Pont batterie bas (N3_BATTERY_R2=2180, 2.2k mesurée) |

Plus : 2 supports femelles 1x15 (A1) — le DevKit s'enfiche, jamais soudé.

## À laisser VIDE — DNP (43 composants)

| Réf | Valeur | Description | Pourquoi DNP |
|---|---|---|---|
| D2 | 1N4007 | Diode de roue libre bobine | profil n3pp uniquement |
| D3 | 1N4007 | Diode de roue libre bobine | extension optionnelle (AUX / pont LDR) |
| D4 | 1N4007 | Diode de roue libre bobine | extension optionnelle (AUX / pont LDR) |
| D6 | 1N4007 | Diode de roue libre bobine | extension optionnelle (AUX / pont LDR) |
| D7 | 1N4007 | Diode de roue libre bobine | extension optionnelle (AUX / pont LDR) |
| J4 | Bornier_5.08 | Bornier charge (1=NO 2=COM 3=NC) | profil n3pp uniquement |
| J5 | Bornier_5.08 | Bornier charge (1=NO 2=COM 3=NC) | extension optionnelle (AUX / pont LDR) |
| J6 | Bornier_5.08 | Bornier charge (1=NO 2=COM 3=NC) | extension optionnelle (AUX / pont LDR) |
| J7 | Bornier_5.08 | Bornier charge (1=NO 2=COM 3=NC) | extension optionnelle (AUX / pont LDR) |
| J8 | Bornier_5.08 | Bornier charge (1=NO 2=COM 3=NC) | extension optionnelle (AUX / pont LDR) |
| J9 | JST-XH | DHT n3pp (1=3V3 2=DATA 3=GND) | profil n3pp uniquement |
| K2 | SRD-05VDC-SL-C | Relais 5V SPDT 10A (Songle/Sanyou SRD) | profil n3pp uniquement |
| K3 | SRD-05VDC-SL-C | Relais 5V SPDT 10A (Songle/Sanyou SRD) | extension optionnelle (AUX / pont LDR) |
| K4 | SRD-05VDC-SL-C | Relais 5V SPDT 10A (Songle/Sanyou SRD) | extension optionnelle (AUX / pont LDR) |
| K5 | SRD-05VDC-SL-C | Relais 5V SPDT 10A (Songle/Sanyou SRD) | extension optionnelle (AUX / pont LDR) |
| K6 | SRD-05VDC-SL-C | Relais 5V SPDT 10A (Songle/Sanyou SRD) | extension optionnelle (AUX / pont LDR) |
| LED2 | rouge | LED témoin relais ON (DNP si profil batterie) | profil n3pp uniquement |
| LED3 | rouge | LED témoin relais ON (DNP si profil batterie) | extension optionnelle (AUX / pont LDR) |
| LED4 | rouge | LED témoin relais ON (DNP si profil batterie) | extension optionnelle (AUX / pont LDR) |
| LED6 | rouge | LED témoin relais ON (DNP si profil batterie) | extension optionnelle (AUX / pont LDR) |
| LED7 | rouge | LED témoin relais ON (DNP si profil batterie) | extension optionnelle (AUX / pont LDR) |
| Q2 | BC337-40 | Transistor NPN commande relais (1=C 2=B 3=E) | profil n3pp uniquement |
| Q3 | BC337-40 | Transistor NPN commande relais (1=C 2=B 3=E) | extension optionnelle (AUX / pont LDR) |
| Q4 | BC337-40 | Transistor NPN commande relais (1=C 2=B 3=E) | extension optionnelle (AUX / pont LDR) |
| Q5 | BC337-40 | Transistor NPN commande relais (1=C 2=B 3=E) | extension optionnelle (AUX / pont LDR) |
| Q6 | BC337-40 | Transistor NPN commande relais (1=C 2=B 3=E) | extension optionnelle (AUX / pont LDR) |
| R2 | 1k | Résistance base transistor | profil n3pp uniquement |
| R3 | 1k | Résistance base transistor | extension optionnelle (AUX / pont LDR) |
| R4 | 1k | Résistance base transistor | extension optionnelle (AUX / pont LDR) |
| R6 | 10k | Pull-down base (état sûr au boot) | profil n3pp uniquement |
| R7 | 10k | Pull-down base (état sûr au boot) | extension optionnelle (AUX / pont LDR) |
| R8 | 10k | Pull-down base (état sûr au boot) | extension optionnelle (AUX / pont LDR) |
| R10 | 1k | Résistance LED témoin (DNP si profil batterie) | profil n3pp uniquement |
| R11 | 1k | Résistance LED témoin (DNP si profil batterie) | extension optionnelle (AUX / pont LDR) |
| R12 | 1k | Résistance LED témoin (DNP si profil batterie) | extension optionnelle (AUX / pont LDR) |
| R15 | 10k | Bas de pont ADC_B (peupler si LDR ; DNP si capteur AO) | extension optionnelle (AUX / pont LDR) |
| R21 | 10k | Pull-up data DHT n3pp | profil n3pp uniquement |
| R28 | 1k | Résistance base transistor | extension optionnelle (AUX / pont LDR) |
| R29 | 10k | Pull-down base (état sûr au boot) | extension optionnelle (AUX / pont LDR) |
| R30 | 1k | Résistance LED témoin (DNP si profil batterie) | extension optionnelle (AUX / pont LDR) |
| R31 | 1k | Résistance base transistor | extension optionnelle (AUX / pont LDR) |
| R32 | 10k | Pull-down base (état sûr au boot) | extension optionnelle (AUX / pont LDR) |
| R33 | 1k | Résistance LED témoin (DNP si profil batterie) | extension optionnelle (AUX / pont LDR) |

## Rappels

- Profil **sur batterie** : ne pas peupler non plus les LED témoin
  (LED1..LED7) ni leurs résistances série (R9..R12, R30, R33, R13) pour
  économiser ~2 mA par LED.
- Les canaux AUX (REL3..REL6) se peuplent plus tard sans toucher au reste :
  relais + transistor + diode + 3 résistances + bornier du canal choisi.
- GPIO2 (1-Wire msp) est une broche de strapping : si le flash USB échoue,
  débrancher la sonde DS18B20 le temps du flash.
- Capteur de pluie (msp) : brancher la sortie **DO** (numérique), pas AO —
  GPIO27 est sur ADC2, inutilisable en analogique quand le WiFi est actif.
