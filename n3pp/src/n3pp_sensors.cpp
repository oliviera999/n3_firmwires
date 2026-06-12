#include "n3pp_sensors.h"
#include "n3pp_globals.h"
#include "n3_battery.h"
#include "n3_analog_sensors.h"

static const uint16_t DHT_READ_DELAY_MS = 150;
static const uint16_t BATTERY_OLED_DELAY_MS = 500;

// Bornes physiques DHT11/DHT22 (specs constructeur).
// Valeurs hors plage = capteur deconnecte / faux contact, on remet le fallback.
static const float N3PP_DHT_TEMP_MIN = -40.0f;
static const float N3PP_DHT_TEMP_MAX = 80.0f;
static const float N3PP_DHT_HUM_MIN = 0.0f;
static const float N3PP_DHT_HUM_MAX = 100.0f;
static const float N3PP_DHT_TEMP_FALLBACK = 20.0f;
static const float N3PP_DHT_HUM_FALLBACK = 50.0f;

// Valeurs ADC sentinelle pour detecter un capteur d'humidite sol/luminosite debranche.
// 0 = ligne tiree a la masse, 4080+ = ligne flottante a Vcc.
static const uint16_t N3PP_ANALOG_DISCONNECT_LOW = 5;
static const uint16_t N3PP_ANALOG_DISCONNECT_HIGH = 4080;
static const uint16_t N3PP_ANALOG_FALLBACK = 1;

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

// Lit un capteur d'humidite sol et detecte le cas "debranche" (lecture
// tres basse ou saturee a la masse / au rail).
static int readSoilSensor(uint8_t pin, const char* label) {
  N3Analog::AnalogConfig c = cfgHumid;
  c.pin = pin;
  N3Analog::AnalogResult r = N3Analog::readFilteredAnalog(c);
  if (!r.valid) {
    Serial.printf("[SOIL][WARN] %s lecture invalide, fallback=%u\n", label, N3PP_ANALOG_FALLBACK);
    return N3PP_ANALOG_FALLBACK;
  }
  if (r.value <= N3PP_ANALOG_DISCONNECT_LOW || r.value >= N3PP_ANALOG_DISCONNECT_HIGH) {
    Serial.printf("[SOIL][WARN] %s probable debranchement (raw=%u), fallback=%u\n",
                  label, r.value, N3PP_ANALOG_FALLBACK);
    return N3PP_ANALOG_FALLBACK;
  }
  return r.value;
}

void lectureCapteurs() {
  Humid1 = readSoilSensor(humidite1, "humidite1");
  Serial.print("Capteur humidite 1 : ");
  Serial.println(Humid1);

  Humid2 = readSoilSensor(humidite2, "humidite2");
  Serial.print("Capteur humidite 2 : ");
  Serial.println(Humid2);

  Humid3 = readSoilSensor(humidite3, "humidite3");
  Serial.print("Capteur humidite 3 : ");
  Serial.println(Humid3);

  Humid4 = readSoilSensor(humidite4, "humidite4");
  Serial.print("Capteur humidite 4 : ");
  Serial.println(Humid4);

  // Moyenne sur les capteurs valides uniquement : un capteur debranche
  // (fallback=1, lu comme "tres sec") tirait la moyenne vers le bas et
  // pouvait declencher un arrosage intempestif.
  {
    const int soilVals[4] = { Humid1, Humid2, Humid3, Humid4 };
    long validSum = 0;
    soilValidCount = 0;
    for (int i = 0; i < 4; ++i) {
      if (soilVals[i] != N3PP_ANALOG_FALLBACK) {
        validSum += soilVals[i];
        soilValidCount++;
      }
    }
    HumidMoy = (soilValidCount > 0) ? (int)(validSum / soilValidCount)
                                    : N3PP_ANALOG_FALLBACK;
  }
  Serial.print("Capteur humidite moyenne : ");
  Serial.print(HumidMoy);
  Serial.printf(" (capteurs valides: %d/4)\n", soilValidCount);

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
  delay(DHT_READ_DELAY_MS);
  h = dht.readHumidity();
  delay(DHT_READ_DELAY_MS);
  Serial.printf("[DHT] raw t=%.1fC h=%.1f%%\n", temperatureAir, h);

  // Validation isnan + bornes physiques (-40..80 C, 0..100 %).
  // Toute valeur hors plage = capteur deconnecte / faux contact => fallback sur.
  if (isnan(temperatureAir) || temperatureAir < N3PP_DHT_TEMP_MIN || temperatureAir > N3PP_DHT_TEMP_MAX) {
    Serial.printf("[DHT][WARN] Temperature invalide (%.1fC), fallback %.1fC\n",
                  temperatureAir, N3PP_DHT_TEMP_FALLBACK);
    temperatureAir = N3PP_DHT_TEMP_FALLBACK;
  }
  if (isnan(h) || h < N3PP_DHT_HUM_MIN || h > N3PP_DHT_HUM_MAX) {
    Serial.printf("[DHT][WARN] Humidite invalide (%.1f%%), fallback %.1f%%\n", h, N3PP_DHT_HUM_FALLBACK);
    h = N3PP_DHT_HUM_FALLBACK;
  }

  // Luminosite avec meme detection debranchement que les capteurs sol.
  N3Analog::AnalogConfig cLum = cfgLumi;
  cLum.pin = LUMINOSITE;
  N3Analog::AnalogResult rLum = N3Analog::readFilteredAnalog(cLum);
  if (!rLum.valid || rLum.value <= N3PP_ANALOG_DISCONNECT_LOW || rLum.value >= N3PP_ANALOG_DISCONNECT_HIGH) {
    Serial.printf("[LUM][WARN] Luminosite invalide (raw=%u), fallback=%u\n",
                  rLum.value, N3PP_ANALOG_FALLBACK);
    photocellReading = N3PP_ANALOG_FALLBACK;
  } else {
    photocellReading = rLum.value;
  }
}

void batterie() {
  // PontDiv est deja calcule (filtre median + moyenne) dans lectureCapteurs(),
  // ne pas l'ecraser ici avec une lecture brute analogRead() (cause de l'audit
  // 4.38 : valeur differente du POST envoye au serveur).
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
