/* MeteoStationPrototype (msp1) — Capteurs
 * LectureCapteurs, batterie, Light_val (tracker solaire)
 */

#include "msp_sensors.h"
#include "msp_config.h"
#include "msp_globals.h"
#include <Arduino.h>
#include "n3_battery.h"
#include "n3_analog_sensors.h"

static const uint16_t BATTERY_OLED_DELAY_MS = 500;
static const int LIGHT_SCAN_MIN_THRESHOLD = 50;
static const bool MSP_VERBOSE_LIGHT_SCAN = false;
// Délais du scan servo (réduits — audit algo 2026-06). Les anciens 30/50 ms
// cumulaient ~23 s par balayage ; un servo avance de 1° en ~10-15 ms et l'ADC
// se stabilise en quelques ms. ⚠ Réductions à valider sur cible (qualité du
// suivi) : revenir à 30/50 si le tracker devient erratique.
static const int SCAN_SERVO_SETTLE_MS = 15;   // était 30
static const int SCAN_LDR_SETTLE_MS = 5;      // était 50
// L'OLED (clearDisplay+display ≈ 25-30 ms I2C) n'est rafraîchie qu'une
// position sur N pendant le balayage.
static const int SCAN_DISPLAY_EVERY = 8;
static bool s_lastServoModeAutoLogKnown = false;
static bool s_lastServoModeAutoLogged = true;

static int clampServoAngle(int value, int minAngle, int maxAngle) {
  if (value < minAngle) return minAngle;
  if (value > maxAngle) return maxAngle;
  return value;
}

static void applyManualServoTargets() {
  const int requestedGd = AngleServoGD;
  const int requestedHb = AngleServoHB;
  const int clampedGd = clampServoAngle(requestedGd, minAngleServoGD, maxAngleServoGD);
  const int clampedHb = clampServoAngle(requestedHb, minAngleServoHB, maxAngleServoHB);

  if (clampedGd != requestedGd || clampedHb != requestedHb) {
    Serial.printf("[SERVO][MANUAL][WARN] clamp GD:%d->%d HB:%d->%d\n",
                  requestedGd, clampedGd, requestedHb, clampedHb);
  }

  AngleServoGD = clampedGd;
  AngleServoHB = clampedHb;
  servogd.write(AngleServoGD);
  servohb.write(AngleServoHB);
  Serial.printf("[SERVO][APPLY] mode=MANUEL angleGD=%d angleHB=%d\n", AngleServoGD, AngleServoHB);
}

static const N3BatteryConfig batteryConfig = {
  pontdiv, (uint32_t)N3_BATTERY_R1, (uint32_t)N3_BATTERY_R2, N3_BATTERY_VREF, NUM_SAMPLES
};

static const N3Analog::AnalogConfig cfgHumidSol = {
  .pin = HumiditeSol, .numSamples = 8, .delayMs = 2,
  .filterMode = N3Analog::MEDIANE_PUIS_MOYENNE, .outlierMax = 100,
  .minValid = 0, .maxValid = 4095, .fallback = 1, .emaAlpha = 0.0f
};

// Plage physique acceptable pour la DS18B20 (sol exterieur Casablanca).
static const float MSP_TEMP_MIN = -20.0f;
static const float MSP_TEMP_MAX = 70.0f;
static const float MSP_TEMP_FALLBACK = 20.0f;

// Bornes physiques DHT11/DHT22.
static const float MSP_DHT_TEMP_MIN = -40.0f;
static const float MSP_DHT_TEMP_MAX = 80.0f;
static const float MSP_DHT_HUM_MIN = 0.0f;
static const float MSP_DHT_HUM_MAX = 100.0f;
static const float MSP_DHT_TEMP_FALLBACK = 20.0f;
static const float MSP_DHT_HUM_FALLBACK = 50.0f;

// Sentinelles ADC pour capteur pluie debranche (saturation rail/masse).
// Le capteur Funduino retourne ~4095 sec, ~0 mouille ; on differencie une lecture
// flottante (broche non cablee) d'une vraie absence d'eau via la valeur sentinelle 1.
static const int MSP_PLUIE_DISCONNECT = 1;

void LectureCapteurs() {
  // Humidite du sol (ADC filtre)
  N3Analog::AnalogResult rHum = N3Analog::readFilteredAnalog(cfgHumidSol);
  HumidSol = rHum.valid ? rHum.value : 1;
  if (HumidSol == 0) HumidSol = 1;
  Serial.printf("[SENSOR] HumidSol=%d\n", HumidSol);

  // Detection pluie (analogique). PLUIE est defini dans msp_config.h (GPIO 27).
  // Avant v2.42 : analogRead(27) en dur (non testable si la broche change).
  // Distinction sec vs debranche :
  //   * 0..3 = ligne flottante / capteur deconnecte  -> sentinelle 1
  //   * sinon valeur capteur (4095 = sec, 0..4094 = humide selon mouillage)
  int pluieRaw = analogRead(PLUIE);
  if (pluieRaw <= 3) {
    Serial.printf("[PLUIE][WARN] Lecture suspecte (raw=%d, capteur deconnecte?), sentinelle=%d\n",
                  pluieRaw, MSP_PLUIE_DISCONNECT);
    Pluie = MSP_PLUIE_DISCONNECT;
  } else {
    Pluie = pluieRaw;
  }
  delay(100);
  Serial.printf("[SENSOR] Pluie=%d\n", Pluie);

  // DHT interieur : isnan + bornes physiques (-40..80 C, 0..100 %).
  tempAirInt = dhtint.readTemperature();
  delay(100);
  humidAirInt = dhtint.readHumidity();
  delay(100);
  if (isnan(tempAirInt) || tempAirInt < MSP_DHT_TEMP_MIN || tempAirInt > MSP_DHT_TEMP_MAX) {
    Serial.printf("[DHT][INT][WARN] Temperature invalide (%.1fC), fallback %.1fC\n",
                  tempAirInt, MSP_DHT_TEMP_FALLBACK);
    tempAirInt = MSP_DHT_TEMP_FALLBACK;
  }
  if (isnan(humidAirInt) || humidAirInt < MSP_DHT_HUM_MIN || humidAirInt > MSP_DHT_HUM_MAX) {
    Serial.printf("[DHT][INT][WARN] Humidite invalide (%.1f%%), fallback %.1f%%\n",
                  humidAirInt, MSP_DHT_HUM_FALLBACK);
    humidAirInt = MSP_DHT_HUM_FALLBACK;
  }

  // DHT exterieur : meme validation.
  tempAirExt = dhtext.readTemperature();
  delay(100);
  humidAirExt = dhtext.readHumidity();
  delay(100);
  if (isnan(tempAirExt) || tempAirExt < MSP_DHT_TEMP_MIN || tempAirExt > MSP_DHT_TEMP_MAX) {
    Serial.printf("[DHT][EXT][WARN] Temperature invalide (%.1fC), fallback %.1fC\n",
                  tempAirExt, MSP_DHT_TEMP_FALLBACK);
    tempAirExt = MSP_DHT_TEMP_FALLBACK;
  }
  if (isnan(humidAirExt) || humidAirExt < MSP_DHT_HUM_MIN || humidAirExt > MSP_DHT_HUM_MAX) {
    Serial.printf("[DHT][EXT][WARN] Humidite invalide (%.1f%%), fallback %.1f%%\n",
                  humidAirExt, MSP_DHT_HUM_FALLBACK);
    humidAirExt = MSP_DHT_HUM_FALLBACK;
  }

  // Temperature sol (DS18B20). Avant v2.42 : magique 25.00 traite comme erreur.
  // Maintenant : test explicite DEVICE_DISCONNECTED_C, retry une fois, fallback 20C.
  sensors.requestTemperatures();
  temperatureSol = sensors.getTempCByIndex(0);
  if (temperatureSol == DEVICE_DISCONNECTED_C || isnan(temperatureSol)) {
    Serial.println("[DS18B20][WARN] Lecture invalide, retry...");
    delay(200);
    sensors.requestTemperatures();
    temperatureSol = sensors.getTempCByIndex(0);
  }
  if (temperatureSol == DEVICE_DISCONNECTED_C || isnan(temperatureSol) ||
      temperatureSol < MSP_TEMP_MIN || temperatureSol > MSP_TEMP_MAX) {
    Serial.printf("[DS18B20][WARN] Temperature sol invalide (%.2fC), fallback %.1fC\n",
                  temperatureSol, MSP_TEMP_FALLBACK);
    temperatureSol = MSP_TEMP_FALLBACK;
  } else {
    Serial.printf("[DS18B20] TempEau=%.2fC\n", temperatureSol);
  }
}

void batterie() {
  PontDiv = analogRead(pontdiv);
  Serial.printf("[BATT] PontDiv=%d\n", PontDiv);

  N3BatteryResult res = n3BatteryRead(batteryConfig, (void*)samples, (void*)&sampleIndex, (void*)&sampleTotal);
  avgPontDiv = res.rawAvg;
  measuredVoltage = res.measuredVoltage;
  batteryVoltage = res.batteryVoltage;

  Serial.printf("[BATT] ADC=%d tension_brute=%.2f V\n", avgPontDiv, measuredVoltage);

  int battPercent = 100 - ((2100 - avgPontDiv) * 0.2);
  int batteryVoltage2 = avgPontDiv * 4.2 / 2100;

  if (displayOk) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("Read = ");
    display.println(avgPontDiv);
    display.print("Brut = ");
    display.println(measuredVoltage);
    display.print("Batt = ");
    display.println(batteryVoltage);
    display.print("Percent = ");
    display.println(battPercent);
    display.display();
    delay(BATTERY_OLED_DELAY_MS);

    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("Read = ");
    display.println(avgPontDiv);
    display.println(" ");
    display.print("Batt = ");
    display.println(batteryVoltage2);
    display.print("Percent = ");
    display.println(battPercent);
    display.display();
    delay(BATTERY_OLED_DELAY_MS);
  }
  //batteryVoltage =

  // Afficher la tension mesurée
  Serial.printf("[BATT] tension_filtree=%.2f V\n", batteryVoltage);
}

void Light_val() {
  if (!s_lastServoModeAutoLogKnown || s_lastServoModeAutoLogged != servoModeAuto) {
    Serial.printf("[SERVO][MODE] source=runtime mode=%s\n", servoModeAuto ? "AUTO" : "MANUEL");
    s_lastServoModeAutoLogged = servoModeAuto;
    s_lastServoModeAutoLogKnown = true;
  }

  // Télémétrie : toujours remplir LuminositeA–D et LuminositeMoy pour le POST serveur.
  // Avant v2.41 : en mode servo manuel on retournait sans lecture → zéros côté BDD.
  // Sous le seuil de scan, le balayage était ignoré et A–D n'étaient pas mises à jour (souvent 0).
  photocellReadingA = analogRead(LUMINOSITEa);
  photocellReadingB = analogRead(LUMINOSITEb);
  photocellReadingC = analogRead(LUMINOSITEc);
  photocellReadingD = analogRead(LUMINOSITEd);
  photocellReadingMoy = (photocellReadingA + photocellReadingB + photocellReadingC + photocellReadingD) / 4;

  if (!servoModeAuto) {
    Serial.println("[SERVO][AUTO] scan=OFF raison=mode_manuel");
    applyManualServoTargets();
    return;
  }

  if (photocellReadingMoy > LIGHT_SCAN_MIN_THRESHOLD) {
    Serial.printf("[SERVO][AUTO] scan=ON lum=%d seuil=%d\n", photocellReadingMoy, LIGHT_SCAN_MIN_THRESHOLD);
    if (displayOk) {
      display.clearDisplay();
      display.setTextSize(2);
      display.setCursor(0, 0);
      display.println("Scan");
      display.print("LumMoy = ");
      display.println(photocellReadingMoy);
      display.display();
    }
    delay(750);
    // Initialisation des tableaux de lectures
    for (int i = 0; i < numReadings; i++) {
      readings1[i] = 0;
      readings2[i] = 0;
      readings3[i] = 0;
      readings4[i] = 0;
    }

    // Initialisation des variables
    photocellReadingA = photocellReadingB = photocellReadingC = photocellReadingD = 0;
    posLumMax1 = posLumMax2 = posLumMax3 = posLumMax4 = 0;
    total1 = total2 = total3 = total4 = 0;
    average1 = average2 = average3 = average4 = 0;
    readIndex = 0;


    // Balayage des positions et mesure de la luminosité pour les quatre capteurs

    // Balayage du premier servomoteur
    for (int pos = minAngleServoGD; pos <= maxAngleServoGD; pos++) {
      servogd.write(pos);
      delay(SCAN_SERVO_SETTLE_MS);  // Attendre que le servomoteur se positionne

      // Lecture et filtrage pour les capteurs associés au premier servomoteur
      int currentReading1 = analogRead(LUMINOSITEa);
      total1 = total1 - readings1[readIndex];
      readings1[readIndex] = currentReading1;
      total1 = total1 + readings1[readIndex];
      average1 = total1 / numReadings;

      int currentReading2 = analogRead(LUMINOSITEb);
      total2 = total2 - readings2[readIndex];
      readings2[readIndex] = currentReading2;
      total2 = total2 + readings2[readIndex];
      average2 = total2 / numReadings;
      readIndex = (readIndex + 1) % numReadings;

      if (displayOk && (pos % SCAN_DISPLAY_EVERY) == 0) {
        display.clearDisplay();
        display.setTextSize(2);
        display.setCursor(0, 0);
        display.print("Moy 1 ");
        display.println(average1);
        display.print("Moy 2 ");
        display.println(average2);
        display.display();
      }
      delay(SCAN_LDR_SETTLE_MS);

      // Enregistrement de la valeur maximale pour les capteurs 1 et 2
      if (average1 > photocellReadingA) {
        photocellReadingA = average1;
        posLumMax1 = pos;
        if (MSP_VERBOSE_LIGHT_SCAN) {
          Serial.printf("[SERVO][SCAN] nouveau max A pos=%d lum=%d\n", posLumMax1, photocellReadingA);
        }
      }
      if (average2 > photocellReadingB) {
        photocellReadingB = average2;
        posLumMax2 = pos;
        if (MSP_VERBOSE_LIGHT_SCAN) {
          Serial.printf("[SERVO][SCAN] nouveau max B pos=%d lum=%d\n", posLumMax2, photocellReadingB);
        }
      }
    }

    AngleServoGD = (posLumMax1 + posLumMax2) / 2;
    servogd.write(AngleServoGD);
    if (displayOk) {
      display.clearDisplay();
      display.setTextSize(2);
      display.setCursor(0, 0);
      display.print(posLumMax1);
      display.print(" ");
      display.print(posLumMax2);
      display.print("AngleM = ");
      display.println(AngleServoGD);
      display.display();
    }
    delay(750);

    // Balayage du second servomoteur
    readIndex = 0;
    for (int pos = minAngleServoHB; pos <= maxAngleServoHB; pos++) {
      servohb.write(pos);
      delay(SCAN_SERVO_SETTLE_MS);  // Attendre que le servomoteur se positionne

      // Lecture et filtrage pour les capteurs associés au second servomoteur
      int currentReading3 = analogRead(LUMINOSITEc);
      total3 = total3 - readings3[readIndex];
      readings3[readIndex] = currentReading3;
      total3 = total3 + readings3[readIndex];
      average3 = total3 / numReadings;

      int currentReading4 = analogRead(LUMINOSITEd);
      total4 = total4 - readings4[readIndex];
      readings4[readIndex] = currentReading4;
      total4 = total4 + readings4[readIndex];
      average4 = total4 / numReadings;
      readIndex = (readIndex + 1) % numReadings;

      if (displayOk && (pos % SCAN_DISPLAY_EVERY) == 0) {
        display.clearDisplay();
        display.setTextSize(2);
        display.setCursor(0, 0);
        display.print("Moy 3 ");
        display.println(average3);
        display.print("Moy 4 ");
        display.println(average4);
        display.display();
      }
      delay(SCAN_LDR_SETTLE_MS);

      // Enregistrement de la valeur maximale pour les capteurs 3 et 4
      if (average3 > photocellReadingC) {
        photocellReadingC = average3;
        posLumMax3 = pos;
        if (MSP_VERBOSE_LIGHT_SCAN) {
          Serial.printf("[SERVO][SCAN] nouveau max C pos=%d lum=%d\n", posLumMax3, photocellReadingC);
        }
      }
      if (average4 > photocellReadingD) {
        photocellReadingD = average4;
        posLumMax4 = pos;
        if (MSP_VERBOSE_LIGHT_SCAN) {
          Serial.printf("[SERVO][SCAN] nouveau max D pos=%d lum=%d\n", posLumMax4, photocellReadingD);
        }
      }
    }

    AngleServoHB = (posLumMax3 + posLumMax4) / 2;  // Calcul des positions finales pour servomoteur haut bas
    if (AngleServoHB > maxAngleServoHB) {
      AngleServoHB = maxAngleServoHB;
    } else if (AngleServoHB < minAngleServoHB) {
      AngleServoHB = minAngleServoHB;
    }
    servohb.write(AngleServoHB);
    if (displayOk) {
      display.clearDisplay();
      display.setTextSize(2);
      display.setCursor(0, 0);
      display.print(posLumMax3);
      display.print(" ");
      display.println(posLumMax4);
      display.print("AngleM = ");
      display.println(AngleServoHB);
      display.display();
    }
    delay(750);

    // Affichage des positions finales
    Serial.printf("[SERVO][SCAN] A=%d@%d B=%d@%d C=%d@%d D=%d@%d\n",
                  photocellReadingA, posLumMax1,
                  photocellReadingB, posLumMax2,
                  photocellReadingC, posLumMax3,
                  photocellReadingD, posLumMax4);
    Serial.printf("[SERVO] angleGD=%d angleHB=%d\n", AngleServoGD, AngleServoHB);

    photocellReadingMoy = (photocellReadingA + photocellReadingB + photocellReadingC + photocellReadingD) / 4;
    Serial.printf("[SENSOR] LuminositeMoy=%d\n", photocellReadingMoy);
    if (displayOk) {
      display.clearDisplay();
      display.setTextSize(2);
      display.setCursor(0, 0);
      display.print("LumMoy = ");
      display.println(photocellReadingMoy);
      display.print("AngleGD = ");
      display.println(AngleServoGD);
      display.print("AngleHB = ");
      display.println(AngleServoHB);
      display.display();
    }
    delay(750);
  } else {
    Serial.printf("[SERVO][AUTO] scan=SKIP lum=%d<=seuil=%d\n", photocellReadingMoy, LIGHT_SCAN_MIN_THRESHOLD);
    if (displayOk) {
      display.clearDisplay();
      display.setTextSize(2);
      display.setCursor(0, 0);
      display.println("Pas de scan");
      display.println("LumMoy = ");
      display.println(photocellReadingMoy);
      display.display();
    }
    delay(750);
  }
}
