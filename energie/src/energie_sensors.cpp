#include "energie_sensors.h"

#include <Preferences.h>
#include <Wire.h>
#include <math.h>

#include "n3_battery.h"

namespace {

constexpr const char* kNvsNamespace = "energie";

Preferences s_prefs;
uint16_t s_avgSamples = ENERGIE_INA_AVG_SAMPLES;
bool s_lowBatteryPending = false;
uint32_t s_lastSocSaveMs = 0;
float s_lastSavedSoc = NAN;
uint32_t s_bootCount = 0;

N3Power::LeadAcidConfig makeBatteryConfig() {
  N3Power::LeadAcidConfig c;
  c.capacityAh = ENERGIE_BAT_CAPACITY_AH;
  c.chargeEfficiency = ENERGIE_BAT_CHARGE_EFFICIENCY;
  c.restCurrentA = ENERGIE_BAT_REST_CURRENT_A;
  c.restSeconds = ENERGIE_BAT_REST_SECONDS;
  return c;
}

N3Power::LowBatteryAlertConfig makeAlertConfig() {
  N3Power::LowBatteryAlertConfig c;
  c.thresholdV = ENERGIE_ALERT_LOW_V;
  c.holdSeconds = ENERGIE_ALERT_HOLD_S;
  c.rearmV = ENERGIE_ALERT_REARM_V;
  c.cooldownSeconds = ENERGIE_ALERT_COOLDOWN_S;
  c.chargingCurrentA = ENERGIE_ALERT_CHARGING_A;
  return c;
}

void key(char* out, size_t size, const char* prefix, uint8_t ch) { snprintf(out, size, "%s%u", prefix, ch); }

void printCalibration(const EnergieChannel& c) {
  const N3Ina226::Calibration& cal = c.dev.calibration();
  Serial.printf("[INA][%s] 0x%02X shunt=%.4f ohm imax=%.3f A -> CAL=%u LSB=%.2f uA max mesurable=%.3f A gain=%.4f%s\n",
                c.name, c.address, c.settings.shuntOhm, c.settings.maxCurrentA, cal.cal,
                cal.currentLsbA * 1e6f, cal.measurableMaxA, c.settings.gain, c.settings.invert ? " (inverse)" : "");
  if (cal.imaxBeyondShunt) {
    Serial.printf("[INA][%s][WARN] imax %.2f A > pleine echelle du shunt (%.3f A) : saturation au-dela\n", c.name,
                  c.settings.maxCurrentA, cal.shuntLimitA);
  }
  if (cal.calClamped) {
    Serial.printf("[INA][%s][WARN] CAL borne a 0x7FFF (shunt tres faible) : resolution %.1f uA\n", c.name,
                  cal.currentLsbA * 1e6f);
  }
}

}  // namespace

EnergieChannel g_channels[ENERGIE_CH_COUNT] = {
    {"PV", "Panneau", ENERGIE_PANNEAU_ADDR,
     {ENERGIE_PANNEAU_SHUNT_OHM, ENERGIE_PANNEAU_IMAX_A, 1.0f, ENERGIE_PANNEAU_INVERT}},
    {"BAT", "Batterie", ENERGIE_BATTERIE_ADDR,
     {ENERGIE_BATTERIE_SHUNT_OHM, ENERGIE_BATTERIE_IMAX_A, 1.0f, ENERGIE_BATTERIE_INVERT}},
    {"CONSO", "Conso", ENERGIE_CONSO_ADDR,
     {ENERGIE_CONSO_SHUNT_OHM, ENERGIE_CONSO_IMAX_A, 1.0f, ENERGIE_CONSO_INVERT}},
};

N3Power::LeadAcidSoc g_batterySoc(makeBatteryConfig());
N3Power::LowBatteryAlert g_lowBatteryAlert(makeAlertConfig());

static void loadSettings() {
  s_prefs.begin(kNvsNamespace, false);
  s_bootCount = s_prefs.getUInt("boot", 0) + 1;
  s_prefs.putUInt("boot", s_bootCount);
  s_avgSamples = static_cast<uint16_t>(s_prefs.getUShort("avg", ENERGIE_INA_AVG_SAMPLES));
  char k[8];
  for (uint8_t i = 0; i < ENERGIE_CH_COUNT; ++i) {
    EnergieChannelSettings& s = g_channels[i].settings;
    key(k, sizeof(k), "sh", i);
    s.shuntOhm = s_prefs.getFloat(k, s.shuntOhm);
    key(k, sizeof(k), "im", i);
    s.maxCurrentA = s_prefs.getFloat(k, s.maxCurrentA);
    key(k, sizeof(k), "g", i);
    s.gain = s_prefs.getFloat(k, s.gain);
    key(k, sizeof(k), "inv", i);
    s.invert = s_prefs.getBool(k, s.invert);
  }
  const float soc = s_prefs.getFloat("soc", NAN);
  if (!isnan(soc)) {
    g_batterySoc.seed(soc);
    s_lastSavedSoc = soc;
    Serial.printf("[BAT] SoC restaure depuis NVS : %.1f %%\n", soc);
  }
}

void sensorsSaveSettings(uint8_t ch) {
  if (ch >= ENERGIE_CH_COUNT) return;
  const EnergieChannelSettings& s = g_channels[ch].settings;
  char k[8];
  key(k, sizeof(k), "sh", ch);
  s_prefs.putFloat(k, s.shuntOhm);
  key(k, sizeof(k), "im", ch);
  s_prefs.putFloat(k, s.maxCurrentA);
  key(k, sizeof(k), "g", ch);
  s_prefs.putFloat(k, s.gain);
  key(k, sizeof(k), "inv", ch);
  s_prefs.putBool(k, s.invert);
}

bool sensorsSetupChannel(uint8_t ch) {
  if (ch >= ENERGIE_CH_COUNT) return false;
  EnergieChannel& c = g_channels[ch];
  c.dev.setGain(c.settings.gain);
  c.dev.setInvert(c.settings.invert);
  c.present = c.dev.probe();
  if (!c.present) {
    c.configured = false;
    Serial.printf("[INA][%s] absent a 0x%02X (ou pas un INA226)\n", c.name, c.address);
    return false;
  }
  c.configured = c.dev.setup(s_avgSamples, ENERGIE_INA_CONV_US, c.settings.shuntOhm, c.settings.maxCurrentA);
  c.acc.invalidate();
  if (c.configured) {
    printCalibration(c);
  } else {
    Serial.printf("[INA][%s][ERR] configuration refusee (shunt/imax invalides ?)\n", c.name);
  }
  return c.configured;
}

void sensorsBegin() {
  loadSettings();
  for (uint8_t i = 0; i < ENERGIE_CH_COUNT; ++i) {
    g_channels[i].dev.begin(Wire, g_channels[i].address);
    sensorsSetupChannel(i);
  }
  Serial.printf("[INA] moyennage %u x %u us -> %.0f ms par mesure\n", s_avgSamples, ENERGIE_INA_CONV_US,
                N3Ina226::cycleTimeUs(g_channels[0].dev.configWord()) / 1000.0f);
#if ENERGIE_PIN_ADC_VBAT >= 0
  analogReadResolution(12);
#endif
}

void sensorsSample(uint32_t nowMs) {
  for (uint8_t i = 0; i < ENERGIE_CH_COUNT; ++i) {
    EnergieChannel& c = g_channels[i];
    if (!c.configured) {
      // Module absent ou de-alimente : nouvelle tentative a chaque mesure (peu couteux).
      c.lastOk = false;
      c.acc.invalidate();
      if (c.dev.probe()) {
        Serial.printf("[INA][%s] detecte, configuration\n", c.name);
        if (sensorsSetupChannel(i)) c.windowReinit = true;
      }
      continue;
    }
    if (c.dev.configLost()) {
      // Rail +3V3_SW coupe / faux contact : CAL revenu a 0 -> courant lu a 0.
      ++c.reinitCount;
      c.windowReinit = true;
      c.acc.invalidate();
      Serial.printf("[INA][%s][WARN] configuration perdue -> re-initialisation (#%lu)\n", c.name,
                    static_cast<unsigned long>(c.reinitCount));
      if (!c.dev.restore()) {
        c.configured = false;
        c.present = false;
        c.lastOk = false;
        continue;
      }
    }
    N3Ina226Reading r;
    c.lastOk = c.dev.read(r);
    if (!c.lastOk) {
      c.acc.invalidate();
      continue;
    }
    c.last = r;
    if (r.saturated) c.windowSaturated = true;
    const N3Power::Increment inc = c.acc.add(nowMs, r.busV, r.currentA, r.powerW);
    if (i == ENERGIE_CH_BATTERIE) {
      g_batterySoc.update(r.busV, r.currentA, inc.ah, inc.dtSeconds);
#if ENERGIE_ALERT_ENABLED
      if (g_lowBatteryAlert.update(nowMs, r.busV, r.currentA)) s_lowBatteryPending = true;
#endif
    }
  }
}

bool sensorsConsumeLowBatteryAlert() {
  const bool pending = s_lowBatteryPending;
  s_lowBatteryPending = false;
  return pending;
}

EnergieWindow sensorsTakeWindow() {
  EnergieWindow w = {};
  uint32_t errors = 0;
  for (uint8_t i = 0; i < ENERGIE_CH_COUNT; ++i) {
    EnergieChannel& c = g_channels[i];
    w.ch[i] = c.acc.take();
    w.valid[i] = c.configured && w.ch[i].samples > 0;
    if (c.configured) w.inaStatus |= static_cast<uint16_t>(1u << (ENERGIE_STATUS_PRESENT_SHIFT + i));
    if (c.windowSaturated) w.inaStatus |= static_cast<uint16_t>(1u << (ENERGIE_STATUS_SATURATED_SHIFT + i));
    if (c.windowReinit) w.inaStatus |= static_cast<uint16_t>(1u << (ENERGIE_STATUS_REINIT_SHIFT + i));
    c.windowSaturated = false;
    c.windowReinit = false;
    errors += c.dev.i2cErrors();
  }
  w.i2cErrors = errors;
  w.batterieSoc = g_batterySoc.seeded() ? g_batterySoc.socPercent() : NAN;
  w.batterieAh = g_batterySoc.netAh();
  w.batterieVadc = sensorsReadVadc();
  return w;
}

void sensorsSetAveraging(uint16_t samples) {
  s_avgSamples = N3Ina226::avgSamplesForCode(N3Ina226::avgCodeForSamples(samples));
  s_prefs.putUShort("avg", s_avgSamples);
  for (uint8_t i = 0; i < ENERGIE_CH_COUNT; ++i) sensorsSetupChannel(i);
}

uint16_t sensorsAveraging() { return s_avgSamples; }

void sensorsResetCounters() {
  for (uint8_t i = 0; i < ENERGIE_CH_COUNT; ++i) g_channels[i].acc.resetTotals();
  g_batterySoc.resetAh();
}

void sensorsSeedSoc(float socPercent) {
  g_batterySoc.seed(socPercent);
  s_prefs.putFloat("soc", g_batterySoc.socPercent());
  s_lastSavedSoc = g_batterySoc.socPercent();
}

void sensorsMaybeSaveSoc(uint32_t nowMs) {
  if (!g_batterySoc.seeded()) return;
  if (nowMs - s_lastSocSaveMs < ENERGIE_SOC_SAVE_MS) return;
  s_lastSocSaveMs = nowMs;
  const float soc = g_batterySoc.socPercent();
  // Evite d'user la flash pour une valeur inchangee.
  if (!isnan(s_lastSavedSoc) && fabsf(soc - s_lastSavedSoc) < 0.5f) return;
  s_prefs.putFloat("soc", soc);
  s_lastSavedSoc = soc;
}

void sensorsFactoryReset() {
  const uint32_t boot = s_bootCount;
  s_prefs.clear();
  s_prefs.putUInt("boot", boot);
}

float sensorsReadVadc() {
#if ENERGIE_PIN_ADC_VBAT >= 0
  const N3BatteryConfig cfg = {static_cast<uint8_t>(ENERGIE_PIN_ADC_VBAT), ENERGIE_VBAT_R1, ENERGIE_VBAT_R2,
                               ENERGIE_VBAT_VREF, 16};
  return n3BatteryRead(cfg, nullptr, nullptr, nullptr).measuredVoltage;
#else
  return NAN;
#endif
}

int sensorsChannelFromArg(const char* arg) {
  if (arg == nullptr || arg[0] == '\0') return -1;
  if (arg[0] >= '0' && arg[0] <= '2' && arg[1] == '\0') return arg[0] - '0';
  if (strncasecmp(arg, "pan", 3) == 0 || strncasecmp(arg, "pv", 2) == 0) return ENERGIE_CH_PANNEAU;
  if (strncasecmp(arg, "bat", 3) == 0) return ENERGIE_CH_BATTERIE;
  if (strncasecmp(arg, "con", 3) == 0) return ENERGIE_CH_CONSO;
  return -1;
}

uint32_t sensorsBootCount() { return s_bootCount; }
