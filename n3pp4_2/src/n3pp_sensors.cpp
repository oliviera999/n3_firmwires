#include "n3pp_sensors.h"
#include "n3pp_globals.h"

void lectureCapteurs() {
  //variable locale niveau eau potager
  Humid1 = analogRead(humidite1);  // Lecture valeur analogique capteur humidité 1
  Humid1 = Humid1;
  if (Humid1 == 0) {
    Humid1 = 1;
  }
  Serial.print("Capteur humidité 1 : ");
  //WebSerial.print("Eau du potager : ");

  Serial.println(Humid1);
  //WebSerial.println(Humid1);
  delay(100);

  //variable locale niveau eau potager
  Humid2 = analogRead(humidite2);  // Lecture valeur analogique capteur humidité 2
  Humid2 = Humid2;

  if (Humid2 == 0) {
    Humid2 = 1;
  }
  Serial.print("Capteur humidité 2 : ");
  //WebSerial.print("temperature eau : ");
  Serial.println(Humid2);
  //WebSerial.print(Humid2);
  delay(100);

  //variable locale niveau eau potager
  Humid3 = analogRead(humidite3);  // Lecture valeur analogique capteur humidité 3
  if (Humid3 == 0) {
    Humid3 = 1;
  }
  Serial.print("Capteur humidité 3 : ");
  //WebSerial.print("temperature eau : ");
  if (Humid3 == 0) {
    Humid3 = 1;
  }
  Serial.println(Humid3);
  //WebSerial.print(Humid3);
  delay(100);

  //variable locale niveau eau potager
  Humid4 = analogRead(humidite4);  // Lecture valeur analogique capteur humidité 4
  if (Humid4 == 0) {
    Humid4 = 1;
  }
  Serial.print("Capteur humidité 4 : ");
  //WebSerial.print("temperature eau : ");
  Serial.println(Humid4);
  //WebSerial.print(Humid4);
  delay(100);

  HumidMoy = (Humid1 + Humid2 + Humid3 + Humid4) / 4;  // Moyenne des quatre capteurs humidité sol
  Serial.print("Capteur humidité moyenne : ");
  Serial.println(HumidMoy);

  //variable locale niveau eau potager
  PontDiv = analogRead(pontdiv);  // Lecture diviseur de tension (batterie)
  Serial.print("pontdiv : ");
  //WebSerial.print("temperature eau : ");
  Serial.print(PontDiv);
  //WebSerial.print(PontDiv);
  delay(100);

  Serial.print("seuilsec2 : ");
  Serial.println(SeuilSec);
  Serial.print("tempsArrosage2 : ");
  Serial.println(tempsArrosage);

  //variables locales temperature and humidity
  temperatureAir = dht.readTemperature();
  Serial.println(temperatureAir);
  delay(500);  //fait paniquer le coeur
  h = dht.readHumidity();
  delay(500);
  Serial.println(h);
  if (isnan(h) || isnan(temperatureAir)) {
    Serial.println("Echec de lecture du DHT");
    if (isnan(temperatureAir)) temperatureAir = 0.0f;
    if (isnan(h)) h = 0.0f;
  }

  //variable locale capteur de luminosité
  photocellReading = analogRead(LUMINOSITE);  // the analog reading from the analog resistor divider
  if (photocellReading == 0) {
    photocellReading = 1;
  }
  //variable locale transformation variable
  /* Rsensor = ((4096.0 - photocellReading)*10/photocellReading);
  Serial.print("La luminosité brute est de : ");
  //WebSerial.print("La luminosité brute est de : ");
  Serial.println(photocellReading);
  //WebSerial.println(photocellReading);
  Serial.print("La luminosité calculée est de : ");
  //WebSerial.print("La luminosité calculée est de : ");
  Serial.println(Rsensor);//show the ligth intensity on the serial monitor;
  //WebSerial.println(Rsensor);//show the ligth intensity on the serial monitor;
  //delay(500);
  lux=sensorRawToPhys(photocellReading);
  Serial.print(F("Raw value from sensor= "));
  Serial.println(photocellReading); // the analog reading
  Serial.print(F("Physical value from sensor = "));
  Serial.print(lux); // the analog reading
  Serial.println(F(" lumen")); // the analog reading*/
}

void batterie() {
  // Lire la valeur brute de l'ADC
  PontDiv = analogRead(pontdiv);
  Serial.println(PontDiv);

  // Calculer la moyenne mobile en ajoutant la nouvelle valeur et en retirant l'ancienne
  sampleTotal -= samples[sampleIndex];            // Soustraire l'ancienne valeur
  samples[sampleIndex] = analogRead(pontdiv);     // Ajouter la nouvelle valeur
  sampleTotal += analogRead(pontdiv);             // Mettre à jour le total
  sampleIndex = (sampleIndex + 1) % NUM_SAMPLES;  // Passer à l'échantillon suivant

  // Calculer la moyenne des échantillons
  avgPontDiv = sampleTotal / NUM_SAMPLES;
  Serial.print("Valeur  : ");
  Serial.print(avgPontDiv);

  // Calculer la tension réelle de la batterie
  //batt = avgPontDiv * (R1 + R2) / R2;


  // Convertir la valeur ADC moyenne en tension (sur une plage de 0 à 3,3V)
  measuredVoltage = (avgPontDiv / ADC_MAX_VALUE) * V_REF;
  //measuredVoltage = (measuredVoltage * (1 + calibration));
  // Afficher la tension mesurée
  Serial.print("Tension brute mesurée : ");
  Serial.print(measuredVoltage);
  Serial.println(" V");

  // Calculer la tension réelle de la batterie
  batteryVoltage = measuredVoltage * ((R1 + R2) / R2);

  int battPercent = 100-((2100 - avgPontDiv)*0.2);
  int batteryVoltage2 = avgPontDiv*4.2/2100;

  if (displayOk) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("Lecture = ");
    display.println(avgPontDiv);
    display.print("Brut = ");
    display.println(measuredVoltage);
    display.print("Batt = ");
    display.println(batteryVoltage);
    display.print("Pct = ");
    display.println(battPercent);
    display.display();
  }
  delay(2000);

  if (displayOk) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("Lecture = ");
    display.println(avgPontDiv);
    display.println(" ");
    display.print("Batt = ");
    display.println(batteryVoltage2);
    display.print("Pct = ");
    display.println(battPercent);
    display.display();
  }
  delay(2000);
  //batteryVoltage =	

  // Afficher la tension mesurée
  Serial.print("Tension mesurée (filtrée) : ");
  Serial.print(batteryVoltage);
  Serial.println(" V");
}
