#include "energie_console.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "energie_board.h"
#include "energie_config.h"
#include "energie_network.h"
#include "energie_sensors.h"

namespace {

constexpr size_t kLineMax = 64;
char s_line[kLineMax];
size_t s_len = 0;
bool s_csv = true;
bool s_postRequested = false;

void printCsvHeader() {
  Serial.println("csv,ms,PanneauV,PanneauI,PanneauP,BatterieV,BatterieI,BatterieP,ConsoV,ConsoI,ConsoP,SoC,BatterieAh");
}

void printHelp() {
  Serial.println(
      "\n=== Banc energie — commandes (voie = 0|1|2 ou pan|bat|con) ===\n"
      "  help                 cette aide\n"
      "  scan                 scan du bus I2C\n"
      "  cfg                  reglages, calibration, reseau\n"
      "  dump <voie>          registres bruts de l'INA226\n"
      "  csv on|off           trame CSV 1 Hz\n"
      "  shunt <voie> <ohm>   valeur du shunt (ex. 0.1 / 0.01)\n"
      "  imax <voie> <A>      courant max attendu (fixe la resolution)\n"
      "  inv <voie> 0|1       inverser le sens du courant\n"
      "  cal <voie> <A_ref>   corrige le gain d'apres un multimetre (courant stable)\n"
      "  gain <voie> <g>      gain explicite (1 = aucun)\n"
      "  avg <n>              moyennage materiel (1,4,16,64,128,256,512,1024)\n"
      "  soc <pct>            impose l'etat de charge batterie\n"
      "  reset                remet a zero Wh/Ah cumules\n"
      "  post                 envoi serveur immediat\n"
      "  ota                  verification OTA immediate\n"
      "  mailtest             mail de test (SMTP credentials.h)\n"
      "  factory              efface les reglages NVS (retour config.h) puis reboot\n"
      "  reboot               redemarrage\n");
}

void printConfig() {
  Serial.printf("\n[CFG] firmware %s  boot #%lu  uptime %lu s\n", FIRMWARE_VERSION,
                static_cast<unsigned long>(sensorsBootCount()), static_cast<unsigned long>(millis() / 1000));
  Serial.printf("[CFG] moyennage %u  mesure %lu ms  envoi %lu ms\n", sensorsAveraging(),
                static_cast<unsigned long>(ENERGIE_SAMPLE_MS), static_cast<unsigned long>(ENERGIE_POST_MS));
  for (uint8_t i = 0; i < ENERGIE_CH_COUNT; ++i) {
    const EnergieChannel& c = g_channels[i];
    const N3Ina226::Calibration& cal = c.dev.calibration();
    Serial.printf("[CFG] voie %u %-5s 0x%02X %s shunt=%.4f imax=%.3f gain=%.4f inv=%d CAL=%u LSB=%.2fuA max=%.3fA "
                  "reinit=%lu i2cErr=%lu\n",
                  i, c.name, c.address, c.configured ? "OK " : (c.present ? "ERR" : "ABS"), c.settings.shuntOhm,
                  c.settings.maxCurrentA, c.settings.gain, c.settings.invert ? 1 : 0, cal.cal,
                  cal.currentLsbA * 1e6f, cal.measurableMaxA, static_cast<unsigned long>(c.reinitCount),
                  static_cast<unsigned long>(c.dev.i2cErrors()));
  }
  const N3Power::LeadAcidConfig& b = g_batterySoc.config();
  Serial.printf("[CFG] batterie %.1f Ah, rendement %.2f, repos <%.2f A pendant %lu s ; SoC %s%.1f %% (recalages OCV %lu)\n",
                b.capacityAh, b.chargeEfficiency, b.restCurrentA, static_cast<unsigned long>(b.restSeconds),
                g_batterySoc.seededFromOcv() ? "~" : "", g_batterySoc.socPercent(),
                static_cast<unsigned long>(g_batterySoc.ocvResyncCount()));
  const N3Power::LowBatteryAlertConfig& a = g_lowBatteryAlert.config();
  Serial.printf("[CFG] alerte < %.2f V (%lu s), reamorcage %.2f V, cooldown %lu s, %s, %lu alerte(s)\n", a.thresholdV,
                static_cast<unsigned long>(a.holdSeconds), a.rearmV, static_cast<unsigned long>(a.cooldownSeconds),
                g_lowBatteryAlert.armed() ? "armee" : "desarmee", static_cast<unsigned long>(g_lowBatteryAlert.alertCount()));
  const EnergieNetStatus& n = networkStatus();
  Serial.printf("[CFG] WiFi %s RSSI %d  NTP %s  POST %s  ok=%lu ko=%lu dernier=%d\n",
                n.wifiConnected ? "OK" : "--", n.rssi, n.timeSynced ? "OK" : "--", ENERGIE_POST_URL,
                static_cast<unsigned long>(n.postOk), static_cast<unsigned long>(n.postFail), n.lastPostCode);
}

void dumpChannel(uint8_t ch) {
  EnergieChannel& c = g_channels[ch];
  struct Reg {
    uint8_t reg;
    const char* name;
  };
  static const Reg kRegs[] = {
      {N3Ina226::kRegConfig, "CONFIG"},     {N3Ina226::kRegShuntVoltage, "SHUNT"},
      {N3Ina226::kRegBusVoltage, "BUS"},    {N3Ina226::kRegPower, "POWER"},
      {N3Ina226::kRegCurrent, "CURRENT"},   {N3Ina226::kRegCalibration, "CAL"},
      {N3Ina226::kRegMaskEnable, "MASK"},   {N3Ina226::kRegAlertLimit, "ALERT"},
      {N3Ina226::kRegManufacturerId, "MFG"}, {N3Ina226::kRegDieId, "DIE"},
  };
  Serial.printf("[DUMP] voie %u %s @0x%02X\n", ch, c.name, c.address);
  const float lsb = c.dev.calibration().currentLsbA;
  for (const Reg& r : kRegs) {
    uint16_t v = 0;
    if (!c.dev.readRegister(r.reg, v)) {
      Serial.printf("[DUMP]   %-7s (0x%02X) : erreur I2C\n", r.name, r.reg);
      continue;
    }
    Serial.printf("[DUMP]   %-7s (0x%02X) = 0x%04X", r.name, r.reg, v);
    switch (r.reg) {
      case N3Ina226::kRegConfig: {
        const N3Ina226::ConfigFields f = N3Ina226::decodeConfig(v);
        Serial.printf("  AVG=%u VBUSCT=%luus VSHCT=%luus MODE=%u", N3Ina226::avgSamplesForCode(f.avgCode),
                      static_cast<unsigned long>(N3Ina226::conversionTimeUs(f.busCtCode)),
                      static_cast<unsigned long>(N3Ina226::conversionTimeUs(f.shuntCtCode)), f.mode);
        break;
      }
      case N3Ina226::kRegShuntVoltage: {
        const float mv = N3Ina226::shuntVoltageMv(static_cast<int16_t>(v));
        Serial.printf("  %.4f mV -> %.4f A via R=%.4f", mv, N3Ina226::currentFromShuntA(mv, c.settings.shuntOhm),
                      c.settings.shuntOhm);
        break;
      }
      case N3Ina226::kRegBusVoltage:
        Serial.printf("  %.4f V", N3Ina226::busVoltageV(v));
        break;
      case N3Ina226::kRegPower:
        Serial.printf("  %.4f W (non signe)", N3Ina226::powerFromRegisterW(v, lsb));
        break;
      case N3Ina226::kRegCurrent:
        Serial.printf("  %.5f A (avant gain/sens)", N3Ina226::currentFromRegisterA(static_cast<int16_t>(v), lsb));
        break;
      case N3Ina226::kRegCalibration:
        Serial.printf("  attendu %u%s", c.dev.calibration().cal, v == 0 ? "  <- 0 : config perdue !" : "");
        break;
      case N3Ina226::kRegMaskEnable:
        Serial.printf("%s%s", (v & N3Ina226::kMaskConversionReady) ? "  CVRF" : "",
                      (v & N3Ina226::kMaskMathOverflow) ? "  OVF" : "");
        break;
      case N3Ina226::kRegManufacturerId:
        Serial.print(v == N3Ina226::kManufacturerIdTi ? "  TI" : "  ?");
        break;
      case N3Ina226::kRegDieId:
        Serial.print((v & N3Ina226::kDieIdMask) == N3Ina226::kDieIdIna226 ? "  INA226" : "  ? (INA219 ?)");
        break;
      default:
        break;
    }
    Serial.println();
  }
}

bool parseChannel(const char* arg, uint8_t& ch) {
  const int c = sensorsChannelFromArg(arg);
  if (c < 0) {
    Serial.println("[CMD] voie inconnue (0|1|2 ou pan|bat|con)");
    return false;
  }
  ch = static_cast<uint8_t>(c);
  return true;
}

bool parseFloat(const char* arg, float& out) {
  if (arg == nullptr) return false;
  char* end = nullptr;
  out = strtof(arg, &end);
  return end != arg && isfinite(out);
}

void applyChannelChange(uint8_t ch) {
  sensorsSaveSettings(ch);
  sensorsSetupChannel(ch);
}

void execute(char* line) {
  char* cmd = strtok(line, " \t");
  if (cmd == nullptr) return;
  char* a1 = strtok(nullptr, " \t");
  char* a2 = strtok(nullptr, " \t");
  uint8_t ch = 0;
  float f = 0.0f;

  if (strcasecmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
    printHelp();
  } else if (strcasecmp(cmd, "scan") == 0) {
    boardI2cScan();
  } else if (strcasecmp(cmd, "cfg") == 0) {
    printConfig();
  } else if (strcasecmp(cmd, "dump") == 0) {
    if (parseChannel(a1, ch)) dumpChannel(ch);
  } else if (strcasecmp(cmd, "csv") == 0) {
    s_csv = !(a1 != nullptr && strcasecmp(a1, "off") == 0);
    if (s_csv) printCsvHeader();
  } else if (strcasecmp(cmd, "shunt") == 0) {
    if (parseChannel(a1, ch) && parseFloat(a2, f) && f > 0.0f) {
      g_channels[ch].settings.shuntOhm = f;
      applyChannelChange(ch);
    } else {
      Serial.println("[CMD] usage : shunt <voie> <ohm>");
    }
  } else if (strcasecmp(cmd, "imax") == 0) {
    if (parseChannel(a1, ch) && parseFloat(a2, f) && f > 0.0f) {
      g_channels[ch].settings.maxCurrentA = f;
      applyChannelChange(ch);
    } else {
      Serial.println("[CMD] usage : imax <voie> <A>");
    }
  } else if (strcasecmp(cmd, "inv") == 0) {
    if (parseChannel(a1, ch) && a2 != nullptr) {
      g_channels[ch].settings.invert = atoi(a2) != 0;
      applyChannelChange(ch);
    } else {
      Serial.println("[CMD] usage : inv <voie> 0|1");
    }
  } else if (strcasecmp(cmd, "gain") == 0) {
    if (parseChannel(a1, ch) && parseFloat(a2, f) && f >= 0.5f && f <= 2.0f) {
      g_channels[ch].settings.gain = f;
      applyChannelChange(ch);
    } else {
      Serial.println("[CMD] usage : gain <voie> <0.5..2>");
    }
  } else if (strcasecmp(cmd, "cal") == 0) {
    if (!parseChannel(a1, ch) || !parseFloat(a2, f)) {
      Serial.println("[CMD] usage : cal <voie> <courant_multimetre_A>");
      return;
    }
    EnergieChannel& c = g_channels[ch];
    if (!c.lastOk) {
      Serial.println("[CMD] pas de mesure valide sur cette voie");
      return;
    }
    bool ok = false;
    const float g = N3Ina226::computeGainCorrection(c.settings.gain, c.last.currentA, f, 0.01f, 0.5f, 2.0f, &ok);
    if (!ok) {
      Serial.printf("[CMD] correction refusee (mesure %.4f A, reference %.4f A : trop faible, sens oppose ou ecart > x2)\n",
                    c.last.currentA, f);
      return;
    }
    Serial.printf("[CMD] voie %s : gain %.4f -> %.4f\n", c.name, c.settings.gain, g);
    c.settings.gain = g;
    applyChannelChange(ch);
  } else if (strcasecmp(cmd, "avg") == 0) {
    if (a1 != nullptr && atoi(a1) > 0) {
      sensorsSetAveraging(static_cast<uint16_t>(atoi(a1)));
    } else {
      Serial.println("[CMD] usage : avg <1..1024>");
    }
  } else if (strcasecmp(cmd, "soc") == 0) {
    if (parseFloat(a1, f) && f >= 0.0f && f <= 100.0f) {
      sensorsSeedSoc(f);
      Serial.printf("[CMD] SoC impose a %.1f %%\n", f);
    } else {
      Serial.println("[CMD] usage : soc <0..100>");
    }
  } else if (strcasecmp(cmd, "reset") == 0) {
    sensorsResetCounters();
    Serial.println("[CMD] cumuls Wh/Ah remis a zero");
  } else if (strcasecmp(cmd, "post") == 0) {
    s_postRequested = true;
  } else if (strcasecmp(cmd, "ota") == 0) {
    networkOtaCheckNow();
  } else if (strcasecmp(cmd, "mailtest") == 0) {
    networkSendMail("[ENERGIE] Test mail banc", "Mail de test du banc d'essai energie (INA226).");
  } else if (strcasecmp(cmd, "factory") == 0) {
    sensorsFactoryReset();
    Serial.println("[CMD] reglages NVS effaces, redemarrage");
    delay(200);
    ESP.restart();
  } else if (strcasecmp(cmd, "reboot") == 0) {
    ESP.restart();
  } else {
    Serial.printf("[CMD] inconnue : %s (help)\n", cmd);
  }
}

}  // namespace

void consoleBegin() {
  printHelp();
  printCsvHeader();
}

void consolePoll() {
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (s_len > 0) {
        s_line[s_len] = '\0';
        Serial.printf("> %s\n", s_line);
        execute(s_line);
        s_len = 0;
      }
    } else if (s_len < kLineMax - 1) {
      s_line[s_len++] = static_cast<char>(c);
    }
  }
}

void consolePrintCsv(uint32_t nowMs) {
  if (!s_csv) return;
  Serial.printf("csv,%lu", static_cast<unsigned long>(nowMs));
  for (uint8_t i = 0; i < ENERGIE_CH_COUNT; ++i) {
    const EnergieChannel& c = g_channels[i];
    if (c.configured && c.lastOk) {
      Serial.printf(",%.3f,%.4f,%.3f", c.last.busV, c.last.currentA, c.last.powerW);
    } else {
      Serial.print(",,,");
    }
  }
  if (g_batterySoc.seeded()) {
    Serial.printf(",%.1f,%.4f\n", g_batterySoc.socPercent(), g_batterySoc.netAh());
  } else {
    Serial.println(",,");
  }
}

bool consoleConsumePostRequest() {
  const bool r = s_postRequested;
  s_postRequested = false;
  return r;
}
