#include "n3pp_sensors.h"
#include "n3pp_globals.h"
#include "n3_battery.h"
#include "n3_analog_sensors.h"

static const uint16_t DHT_READ_DELAY_MS = 150;
static const uint16_t BATTERY_OLED_DELAY_MS = 500;

static const N3BatteryConfig batteryConfig = {
  pontdiv, (uint32_t)N3_BATTERY_R1, (uint32_t)N3_BATTERY_R2, N3_BATTERY_VREF, NUM_SAMPLES
};

/* Config lecture ADC filtrée : 8 échantillons, médiane + rejet outliers, plage 0–4095 */
static const N3Analog::AnalogConfig cfgHumid = {
  .pin = 0, .numSamples = 8, .delayMs = 2,
  .filterMode = N3Analog::MEDIANE_PUIS_MOYENNE, .outlierMax = 100,
  .minValid = 0, .maxValid = 4095, .fallback = 1, .emaAlpha = 0.0f
};

static const N3Analog::AnalogConfig cfgLumi = {
  .pin = 0, .numSamples = 12, .delayMs = 1,
  .filterMode = N3Analog::MOYENNE, .outlierMax = 0,
  .minValid = 0, .maxValid = 4095, .fallback = 1, .emaAlpha = 0.0f
};

void lectureCapteurs() {
  N3Analog::AnalogConfig c = cfgHumid;
  c.pin = humidite1;
  N3Analog::AnalogResult r1 = N3Analog::readFilteredAnalog(c);
  Humid1 = r1.valid ? r1.value : 1;
  if (Humid1 == 0) Humid1 = 1;
  Serial.print("Capteur humidite 1 : ");
  Serial.println(Humid1);

  c.pin = humidite2;
  N3Analog::AnalogResult r2 = N3Analog::readFilteredAnalog(c);
  Humid2 = r2.valid ? r2.value : 1;
  if (Humid2 == 0) Humid2 = 1;
  Serial.print("Capteur humidite 2 : ");
  Serial.println(Humid2);

  c.pin = humidite3;
  N3Analog::AnalogResult r3 = N3Analog::readFilteredAnalog(c);
  Humid3 = r3.valid ? r3.value : 1;
  if (Humid3 == 0) Humid3 = 1;
  Serial.print("Capteur humidite 3 : ");
  Serial.println(Humid3);

  c.pin = humidite4;
  N3Analog::AnalogResult r4 = N3Analog::readFilteredAnalog(c);
  Humid4 = r4.valid ? r4.value : 1;
  if (Humid4 == 0) Humid4 = 1;
  Serial.print("Capteur humidite 4 : ");
  Serial.println(Humid4);

  HumidMoy = (Humid1 + Humid2 + Humid3 + Humid4) / 4;
  Serial.print("Capteur humidite moyenne : ");
  Serial.println(HumidMoy);

  N3Analog::AnalogConfig cPont = cfgHumid;
  cPont.pin = pontdiv;
  cPont.numSamples = 8;
  N3Analog::AnalogResult rPont = N3Analog::readFilteredAnalog(cPont);
  PontDiv = rPont.valid ? rPont.value : (uint16_t)0;
  Serial.print("pontdiv : ");
  Serial.print(PontDiv);

  Serial.print("seuilsec2 : ");
  Serial.println(SeuilSec);
  Serial.print("tempsArrosage2 : ");
  Serial.println(tempsArrosage);

  temperatureAir = dht.readTemperature();
  Serial.println(temperatureAir);
  delay(DHT_READ_DELAY_MS);
  h = dht.readHumidity();
  delay(DHT_READ_DELAY_MS);
  Serial.println(h);
  if (isnan(h) || isnan(temperatureAir)) {
    Serial.println("Echec de lecture du DHT, fallback 20C / 50%");
    if (isnan(temperatureAir)) temperatureAir = 20.0f;
    if (isnan(h)) h = 50.0f;
  }

  N3Analog::AnalogConfig cLum = cfgLumi;
  cLum.pin = LUMINOSITE;
  N3Analog::AnalogResult rLum = N3Analog::readFilteredAnalog(cLum);
  photocellReading = rLum.valid ? rLum.value : 1;
  if (photocellReading == 0) photocellReading = 1;
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
  delay(BATTERY_OLED_DELAY_MS);

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
  delay(BATTERY_OLED_DELAY_MS);

  Serial.print("Tension mesuree (filtree) : ");
  Serial.print(batteryVoltage);
  Serial.println(" V");
}
