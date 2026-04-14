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

void LectureCapteurs() {
  // Lire l'humidité du sol (ADC filtré)
  N3Analog::AnalogResult rHum = N3Analog::readFilteredAnalog(cfgHumidSol);
  HumidSol = rHum.valid ? rHum.value : 1;
  if (HumidSol == 0) HumidSol = 1;
  Serial.printf("[SENSOR] HumidSol=%d\n", HumidSol);


  // Lire la détection de pluie
  Pluie = analogRead(27);
  if (Pluie == 0) {
    Pluie = 1;
  }
  delay(100);
  Serial.printf("[SENSOR] Pluie=%d\n", Pluie);


  /*
  // Agrégation des valeurs du diviseur de tension
  int sumPontDiv = 0;
  int PontDiv;
  for (int i = 0; i < numReadings; i++) {
    sumPontDiv += analogRead(pontdiv);
    delay(30);
  }
  PontDiv = sumPontDiv / numReadings;
  delay(100);*/

  // Lire la température et l'humidité de l'air intérieur
  tempAirInt = dhtint.readTemperature();
  delay(100);
  humidAirInt = dhtint.readHumidity();
  delay(100);
  if (isnan(humidAirInt) || isnan(tempAirInt)) {
    Serial.println("[DHT][WARN] Lecture interieur invalide, fallback 20C / 50%");
    tempAirInt = 20.0f;
    humidAirInt = 50.0f;
  }

  // Lire la température et l'humidité de l'air extérieur
  tempAirExt = dhtext.readTemperature();
  delay(100);
  humidAirExt = dhtext.readHumidity();
  delay(100);
  if (isnan(humidAirExt) || isnan(tempAirExt)) {
    Serial.println("[DHT][WARN] Lecture exterieur invalide, fallback 20C / 50%");
    tempAirExt = 20.0f;
    humidAirExt = 50.0f;
  }

  // Obtenir la température du sol
  sensors.requestTemperatures();
  temperatureSol = sensors.getTempCByIndex(0);
  if (temperatureSol == -127.00) {
    temperatureSol = sensors.getTempCByIndex(0);
    delay(200);
  }
  if (temperatureSol == 25.00) {
    temperatureSol = sensors.getTempCByIndex(0);
    delay(200);
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
      delay(30);  // Attendre que le servomoteur se positionne

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

      if (displayOk) {
        display.clearDisplay();
        display.setTextSize(2);
        display.setCursor(0, 0);
        display.print("Moy 1 ");
        display.println(average1);
        display.print("Moy 2 ");
        display.println(average2);
        display.display();
      }
      delay(50);

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
      delay(30);  // Attendre que le servomoteur se positionne

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

      if (displayOk) {
        display.clearDisplay();
        display.setTextSize(2);
        display.setCursor(0, 0);
        display.print("Moy 3 ");
        display.println(average3);
        display.print("Moy 4 ");
        display.println(average4);
        display.display();
      }
      delay(50);

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
