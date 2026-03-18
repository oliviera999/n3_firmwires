/* MeteoStationPrototype (msp1) — Capteurs
 * LectureCapteurs, batterie, Light_val (tracker solaire)
 */

#include "msp_sensors.h"
#include "msp_config.h"
#include "msp_globals.h"
#include <Arduino.h>
#include "n3_battery.h"
#include "n3_analog_sensors.h"

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
  Serial.println(HumidSol);


  // Lire la détection de pluie
  Pluie = analogRead(27);
  if (Pluie == 0) {
    Pluie = 1;
  }
  delay(100);
  Serial.println(Pluie);


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
    // Echec de la lecture DHT pour l'intérieur
  }

  // Lire la température et l'humidité de l'air extérieur
  tempAirExt = dhtext.readTemperature();
  delay(100);
  humidAirExt = dhtext.readHumidity();
  delay(100);
  if (isnan(humidAirExt) || isnan(tempAirExt)) {
    // Echec de la lecture DHT pour l'extérieur
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
  Serial.println(PontDiv);

  N3BatteryResult res = n3BatteryRead(batteryConfig, (void*)samples, (void*)&sampleIndex, (void*)&sampleTotal);
  avgPontDiv = res.rawAvg;
  measuredVoltage = res.measuredVoltage;
  batteryVoltage = res.batteryVoltage;

  Serial.print("Valeur  : ");
  Serial.print(avgPontDiv);
  Serial.print(" Tension brute : ");
  Serial.print(measuredVoltage);
  Serial.println(" V");

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
    delay(2000);

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
    delay(2000);
  }
  //batteryVoltage =

  // Afficher la tension mesurée
  Serial.print("Tension mesurée (filtrée) : ");
  Serial.print(batteryVoltage);
  Serial.println(" V");
}

void Light_val() {
  photocellReadingMoy = ((analogRead(LUMINOSITEa) + analogRead(LUMINOSITEb) + analogRead(LUMINOSITEc) + analogRead(LUMINOSITEd)) / 4);
  if (photocellReadingMoy > 50) {
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
        Serial.print("position max a : ");
        Serial.println(posLumMax1);
      }
      if (average2 > photocellReadingB) {
        photocellReadingB = average2;
        posLumMax2 = pos;
        Serial.print("position max b : ");
        Serial.println(posLumMax2);
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
        Serial.print("position max c : ");
        Serial.println(posLumMax3);
      }
      if (average4 > photocellReadingD) {
        photocellReadingD = average4;
        posLumMax4 = pos;
        Serial.print("position max d : ");
        Serial.println(posLumMax4);
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
    Serial.print("Capteur A : ");
    Serial.println(photocellReadingA);
    Serial.print("Capteur B : ");
    Serial.println(photocellReadingB);
    Serial.print("Capteur C : ");
    Serial.println(photocellReadingC);
    Serial.print("Capteur D : ");
    Serial.println(photocellReadingD);
    // Affichage des positions finales
    Serial.print("Position de luminosité max pour Servo 1 : ");
    Serial.println(AngleServoGD);
    Serial.print("Position de luminosité max pour Servo 2 : ");
    Serial.println(AngleServoHB);

    photocellReadingMoy = (photocellReadingA + photocellReadingB + photocellReadingC + photocellReadingD) / 4;
    Serial.print("photocellReadingMoy :");
    Serial.println(photocellReadingMoy);
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
