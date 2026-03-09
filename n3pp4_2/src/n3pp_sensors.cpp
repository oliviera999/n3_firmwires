#include "n3pp_sensors.h"
#include "n3pp_globals.h"
#include "n3_battery.h"

static const N3BatteryConfig batteryConfig = {
  pontdiv, N3_BATTERY_R1, N3_BATTERY_R2, N3_BATTERY_VREF, NUM_SAMPLES
};

void lectureCapteurs() {
  Humid1 = analogRead(humidite1);
  if (Humid1 == 0) Humid1 = 1;
  Serial.print("Capteur humidite 1 : ");
  Serial.println(Humid1);
  delay(100);

  Humid2 = analogRead(humidite2);
  if (Humid2 == 0) Humid2 = 1;
  Serial.print("Capteur humidite 2 : ");
  Serial.println(Humid2);
  delay(100);

  Humid3 = analogRead(humidite3);
  if (Humid3 == 0) Humid3 = 1;
  Serial.print("Capteur humidite 3 : ");
  Serial.println(Humid3);
  delay(100);

  Humid4 = analogRead(humidite4);
  if (Humid4 == 0) Humid4 = 1;
  Serial.print("Capteur humidite 4 : ");
  Serial.println(Humid4);
  delay(100);

  HumidMoy = (Humid1 + Humid2 + Humid3 + Humid4) / 4;
  Serial.print("Capteur humidite moyenne : ");
  Serial.println(HumidMoy);

  PontDiv = analogRead(pontdiv);
  Serial.print("pontdiv : ");
  Serial.print(PontDiv);
  delay(100);

  Serial.print("seuilsec2 : ");
  Serial.println(SeuilSec);
  Serial.print("tempsArrosage2 : ");
  Serial.println(tempsArrosage);

  temperatureAir = dht.readTemperature();
  Serial.println(temperatureAir);
  delay(500);
  h = dht.readHumidity();
  delay(500);
  Serial.println(h);
  if (isnan(h) || isnan(temperatureAir)) {
    Serial.println("Echec de lecture du DHT");
    if (isnan(temperatureAir)) temperatureAir = 0.0f;
    if (isnan(h)) h = 0.0f;
  }

  photocellReading = analogRead(LUMINOSITE);
  if (photocellReading == 0) photocellReading = 1;
}

void batterie() {
  PontDiv = analogRead(pontdiv);
  Serial.println(PontDiv);

  N3BatteryResult res = n3BatteryRead(batteryConfig, samples, &sampleIndex, &sampleTotal);
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

  Serial.print("Tension mesuree (filtree) : ");
  Serial.print(batteryVoltage);
  Serial.println(" V");
}
