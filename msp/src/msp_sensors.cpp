/* MeteoStationPrototype (msp1) — Capteurs
 * LectureCapteurs, batterie, Light_val (tracker solaire)
 */

#include "msp_sensors.h"
#include "msp_config.h"
#include "msp_globals.h"
#include <Arduino.h>
#include "n3_battery.h"
#include "n3_analog_sensors.h"
#include "n3_tracker.h"
#include "sensor_failure_manager.h"  // machine d'état de détection de panne (T2)

static const uint16_t BATTERY_OLED_DELAY_MS = 500;
static const int LIGHT_SCAN_MIN_THRESHOLD = 50;

// --- Tracker solaire (refonte v2.54, audit tracker 2026-07) ---
// Deux algorithmes, selectionnes par la cle serveur 113 (page de controle) :
//   113=0 (defaut) : asservissement DIFFERENTIEL — equilibrer les deux LDR de
//                    chaque axe par petits pas (zone morte, anti-oscillation).
//                    Quelques pas par reveil au lieu d'un balayage complet.
//   113=1          : BALAYAGE classique optimise — passe grossiere (3°) puis
//                    fine (1°) autour du pic, filtrage par position, fusion
//                    des pics ponderee par l'amplitude (une LDR ombragee ne
//                    divise plus l'angle par deux).
// La logique pure est dans shared/n3_tracker (tests natifs test_tracker).
static const int SCAN_SERVO_SETTLE_MS = 15;
static const int SCAN_REPOSITION_MS = 200;      // grand deplacement (debut de passe)
static const int SCAN_COARSE_STEP_DEG = 3;
static const int SCAN_FINE_HALFWIDTH_DEG = 6;
static const int SCAN_PEAK_MIN_VALUE = 100;     // pic exploitable (raw ADC)
// L'OLED (clearDisplay+display ≈ 25-30 ms I2C) n'est rafraîchie qu'une
// position sur N pendant le balayage.
static const int SCAN_DISPLAY_EVERY = 8;
static const int DIFF_DEADBAND = 80;            // |A-B| tolere (raw ADC, ~2 %)
static const int DIFF_STEP_DEG = 2;
static const int DIFF_MAX_STEPS = 120;          // borne le pire cas (traversee complete)
static const int DIFF_SETTLE_MS = 20;
// Sens de montage : lumA > lumB doit pousser vers +angle. A valider sur cible :
// si le panneau fuit le soleil (logs [SERVO][DIFF][WARN] divergence), inverser.
static const bool DIFF_INVERT_GD = false;
static const bool DIFF_INVERT_HB = false;
static bool s_lastServoModeAutoLogKnown = false;
static bool s_lastServoModeAutoLogged = true;

static void applyManualServoTargets() {
  const int requestedGd = AngleServoGD;
  const int requestedHb = AngleServoHB;
  const int clampedGd = N3Tracker::clampAngle(requestedGd, minAngleServoGD, maxAngleServoGD);
  const int clampedHb = N3Tracker::clampAngle(requestedHb, minAngleServoHB, maxAngleServoHB);

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

// --- Calibration LDR (egalisation des sensibilites, cle serveur 114) ---
// Les LDR et leurs ponts diviseurs ont des tolerances : a eclairement egal,
// deux capteurs d'un meme axe ne lisent pas pareil, ce qui biaise l'equilibre
// du differentiel et la fusion des pics. La calibration mesure les pics des 4
// LDR sur un balayage complet plein soleil (chaque capteur voit alors le meme
// eclairement maximal) et calcule des gains d'egalisation (n3_tracker),
// persistes en NVS et appliques a toutes les lectures.
static const int LDR_GAIN_MIN_PERMILLE = 500;    // borne de securite (x0.5)
static const int LDR_GAIN_MAX_PERMILLE = 2000;   // x2 : au-dela, capteur suspect
static const char* TRACKER_PREFS_NAMESPACE = "n3tracker";
static const char* TRACKER_PREFS_KEYS[4] = {"gainA", "gainB", "gainC", "gainD"};
static int s_ldrGainPermille[4] = {
  N3Tracker::kGainNeutralPermille, N3Tracker::kGainNeutralPermille,
  N3Tracker::kGainNeutralPermille, N3Tracker::kGainNeutralPermille
};

static int ldrIndexForPin(uint8_t pin) {
  switch (pin) {
    case LUMINOSITEa: return 0;
    case LUMINOSITEb: return 1;
    case LUMINOSITEc: return 2;
    case LUMINOSITEd: return 3;
    default: return -1;
  }
}

static void logLdrGains(const char* context) {
  Serial.printf("[SERVO][CALIB] gains(%s) A=%d B=%d C=%d D=%d (pour-mille)\n", context,
                s_ldrGainPermille[0], s_ldrGainPermille[1],
                s_ldrGainPermille[2], s_ldrGainPermille[3]);
}

static void saveLdrGains() {
  Preferences prefs;
  if (!prefs.begin(TRACKER_PREFS_NAMESPACE, false)) {
    Serial.println("[SERVO][CALIB][WARN] NVS inaccessible, gains non persistes");
    return;
  }
  for (int i = 0; i < 4; ++i) {
    prefs.putUShort(TRACKER_PREFS_KEYS[i], (uint16_t)s_ldrGainPermille[i]);
  }
  prefs.end();
}

void mspTrackerLoadCalibration() {
  Preferences prefs;
  // begin() en lecture seule echoue si le namespace n'existe pas encore
  // (jamais calibre) : on reste en gains neutres.
  if (!prefs.begin(TRACKER_PREFS_NAMESPACE, true)) {
    logLdrGains("neutres, jamais calibre");
    return;
  }
  for (int i = 0; i < 4; ++i) {
    const int gain = prefs.getUShort(TRACKER_PREFS_KEYS[i],
                                     (uint16_t)N3Tracker::kGainNeutralPermille);
    s_ldrGainPermille[i] = (gain >= LDR_GAIN_MIN_PERMILLE && gain <= LDR_GAIN_MAX_PERMILLE)
                               ? gain
                               : N3Tracker::kGainNeutralPermille;
  }
  prefs.end();
  logLdrGains("nvs");
}

void mspTrackerResetCalibration() {
  for (int i = 0; i < 4; ++i) {
    s_ldrGainPermille[i] = N3Tracker::kGainNeutralPermille;
  }
  saveLdrGains();
  logLdrGains("reset");
}

// Lecture LDR filtree par position (mediane puis moyenne, 5 echantillons) —
// remplace la moyenne glissante inter-positions du balayage historique qui
// decalait le pic de ~+5° et diluait les premieres positions (constat C3).
// Le gain d'egalisation du capteur est applique a la valeur filtree.
static int readLdrFiltered(uint8_t pin) {
  N3Analog::AnalogConfig cfg = {};
  cfg.pin = pin;
  cfg.numSamples = 5;
  cfg.delayMs = 1;
  cfg.filterMode = N3Analog::MEDIANE_PUIS_MOYENNE;
  cfg.outlierMax = 200;
  cfg.minValid = 0;
  cfg.maxValid = 4095;
  cfg.fallback = 0;
  cfg.emaAlpha = 0.0f;
  N3Analog::AnalogResult r = N3Analog::readFilteredAnalog(cfg);
  const int raw = r.valid ? r.value : 0;
  const int idx = ldrIndexForPin(pin);
  if (idx < 0) return raw;
  return N3Tracker::applyGainPermille(raw, s_ldrGainPermille[idx], 4095);
}

// Telemetrie : valeurs instantanees des 4 LDR + moyenne. Appelee avant ET
// apres le positionnement pour que la BDD recoive toujours la meme grandeur
// (lecture instantanee), plus jamais les pics de balayage (constat C4).
static void readLdrTelemetry() {
  photocellReadingA = readLdrFiltered(LUMINOSITEa);
  photocellReadingB = readLdrFiltered(LUMINOSITEb);
  photocellReadingC = readLdrFiltered(LUMINOSITEc);
  photocellReadingD = readLdrFiltered(LUMINOSITEd);
  photocellReadingMoy =
      (photocellReadingA + photocellReadingB + photocellReadingC + photocellReadingD) / 4;
}

// Balayage optimise d'un axe : passe grossiere sur toute la plage puis passe
// fine autour du pic fusionne (les pics des deux passes se cumulent). Retourne
// l'angle final (fallbackAngle si aucun pic exploitable : on ne bouge pas).
static int sweepAxis(Servo& servo, uint8_t pin1, uint8_t pin2,
                     int minAngle, int maxAngle, int fallbackAngle,
                     N3Tracker::PeakTracker& peak1, N3Tracker::PeakTracker& peak2,
                     const char* axisName) {
  peak1.reset();
  peak2.reset();

  servo.write(minAngle);
  delay(SCAN_REPOSITION_MS);
  int iteration = 0;
  for (int pos = minAngle; pos <= maxAngle; pos += SCAN_COARSE_STEP_DEG) {
    servo.write(pos);
    delay(SCAN_SERVO_SETTLE_MS);
    const int v1 = readLdrFiltered(pin1);
    const int v2 = readLdrFiltered(pin2);
    peak1.feed(pos, v1);
    peak2.feed(pos, v2);
    if (displayOk && (++iteration % SCAN_DISPLAY_EVERY) == 0) {
      display.clearDisplay();
      display.setTextSize(2);
      display.setCursor(0, 0);
      display.print(axisName);
      display.print(" ");
      display.println(pos);
      display.println(v1);
      display.println(v2);
      display.display();
    }
  }

  const int coarseAngle = N3Tracker::fusePeaks(peak1, peak2, SCAN_PEAK_MIN_VALUE,
                                               fallbackAngle, minAngle, maxAngle);
  const N3Tracker::ScanWindow w =
      N3Tracker::fineWindow(coarseAngle, SCAN_FINE_HALFWIDTH_DEG, minAngle, maxAngle);
  servo.write(w.from);
  delay(SCAN_REPOSITION_MS);
  for (int pos = w.from; pos <= w.to; ++pos) {
    servo.write(pos);
    delay(SCAN_SERVO_SETTLE_MS);
    peak1.feed(pos, readLdrFiltered(pin1));
    peak2.feed(pos, readLdrFiltered(pin2));
  }

  const int finalAngle = N3Tracker::fusePeaks(peak1, peak2, SCAN_PEAK_MIN_VALUE,
                                              fallbackAngle, minAngle, maxAngle);
  servo.write(finalAngle);
  delay(SCAN_SERVO_SETTLE_MS);

  const bool valid1 = peak1.valid(SCAN_PEAK_MIN_VALUE);
  const bool valid2 = peak2.valid(SCAN_PEAK_MIN_VALUE);
  Serial.printf("[SERVO][SCAN] axe=%s pic1=%d@%d pic2=%d@%d angle=%d\n", axisName,
                peak1.bestVal, peak1.bestPos, peak2.bestVal, peak2.bestPos, finalAngle);
  if (!valid1 || !valid2) {
    Serial.printf("[SERVO][SCAN][WARN] axe=%s pic(s) invalide(s) (min=%d) : LDR ombragee/HS ?%s\n",
                  axisName, SCAN_PEAK_MIN_VALUE,
                  (!valid1 && !valid2) ? " aucun pic -> position conservee" : "");
  }
  return finalAngle;
}

// Asservissement differentiel d'un axe : equilibre les deux LDR par petits pas
// depuis la position courante. La decision par pas (zone morte, renversement,
// butee, divergence) est dans shared/n3_tracker.
static int diffTrackAxis(Servo& servo, uint8_t pinA, uint8_t pinB,
                         const N3Tracker::DiffAxisConfig& cfg, int startAngle,
                         const char* axisName) {
  N3Tracker::DiffAxis controller(cfg);
  int angle = startAngle;
  int stepsDone = 0;
  for (; stepsDone < DIFF_MAX_STEPS; ++stepsDone) {
    const int lumA = readLdrFiltered(pinA);
    const int lumB = readLdrFiltered(pinB);
    const N3Tracker::DiffStep s = controller.step(angle, lumA, lumB);
    if (s.nextAngle != angle) {
      angle = s.nextAngle;
      servo.write(angle);
      delay(DIFF_SETTLE_MS);
    }
    if (s.finished) {
      if (s.diverged) {
        Serial.printf("[SERVO][DIFF][WARN] axe=%s divergence (sens inverse ? cf. DIFF_INVERT_%s) "
                      "angle=%d A=%d B=%d\n", axisName, axisName, angle, lumA, lumB);
      } else {
        Serial.printf("[SERVO][DIFF] axe=%s %s angle=%d pas=%d A=%d B=%d\n", axisName,
                      s.balanced ? "equilibre" : "stop", angle, stepsDone, lumA, lumB);
      }
      return angle;
    }
  }
  Serial.printf("[SERVO][DIFF][WARN] axe=%s budget de pas epuise (%d), angle=%d\n",
                axisName, DIFF_MAX_STEPS, angle);
  return angle;
}

// Calibration : balayage complet des deux axes en gains NEUTRES pour mesurer
// le pic brut de chaque LDR (meme eclairement maximal pour tous en plein
// soleil), puis egalisation via n3_tracker et persistance NVS.
// Retourne false si la lumiere est insuffisante (demande conservee, retentee
// au prochain reveil). Des references inexploitables (LDR ombragee/HS)
// consomment la demande : gains precedents conserves, [WARN] au log.
bool mspTrackerCalibrate() {
  readLdrTelemetry();
  if (photocellReadingMoy <= LIGHT_SCAN_MIN_THRESHOLD) {
    Serial.printf("[SERVO][CALIB] report: lum=%d<=seuil=%d (retente au prochain reveil)\n",
                  photocellReadingMoy, LIGHT_SCAN_MIN_THRESHOLD);
    return false;
  }

  Serial.println("[SERVO][CALIB] debut: balayage de reference en gains neutres");
  int previousGains[4];
  for (int i = 0; i < 4; ++i) {
    previousGains[i] = s_ldrGainPermille[i];
    s_ldrGainPermille[i] = N3Tracker::kGainNeutralPermille;
  }

  N3Tracker::PeakTracker peakA, peakB, peakC, peakD;
  AngleServoGD = sweepAxis(servogd, LUMINOSITEa, LUMINOSITEb,
                           minAngleServoGD, maxAngleServoGD, AngleServoGD,
                           peakA, peakB, "GD");
  AngleServoHB = sweepAxis(servohb, LUMINOSITEc, LUMINOSITEd,
                           minAngleServoHB, maxAngleServoHB, AngleServoHB,
                           peakC, peakD, "HB");
  rtcAngleServoGD = AngleServoGD;
  rtcAngleServoHB = AngleServoHB;

  const int refs[4] = {peakA.bestVal, peakB.bestVal, peakC.bestVal, peakD.bestVal};
  int gains[4];
  const bool complete = N3Tracker::computeEqualizationGains(
      refs, 4, SCAN_PEAK_MIN_VALUE, LDR_GAIN_MIN_PERMILLE, LDR_GAIN_MAX_PERMILLE, gains);
  Serial.printf("[SERVO][CALIB] references A=%d B=%d C=%d D=%d\n",
                refs[0], refs[1], refs[2], refs[3]);

  if (!complete) {
    for (int i = 0; i < 4; ++i) {
      s_ldrGainPermille[i] = previousGains[i];
    }
    Serial.println("[SERVO][CALIB][WARN] reference(s) inexploitable(s) (LDR ombragee/HS ?), "
                   "gains precedents conserves — relancer par temps clair (cle 114: 0 puis 1)");
    return true;  // demande consommee : pas de re-essai en boucle sur un capteur mort
  }

  for (int i = 0; i < 4; ++i) {
    s_ldrGainPermille[i] = gains[i];
  }
  saveLdrGains();
  logLdrGains("calibration OK");
  return true;
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

// Pluie : projection de la sortie NUMERIQUE (DO) du module sur l'echelle
// historique 0..4095 (« plus bas = plus mouille ») pour ne rien changer au
// contrat serveur (MSP_RAIN_WET_THRESHOLD, graphes, historique).
static const int MSP_PLUIE_DRY_RAW = 4095;  // DO haut = sec (valeur historique)
static const int MSP_PLUIE_WET_RAW = 100;   // DO bas = mouille (sous tout seuil)

// --- Détection de panne DURABLE du DS18B20 sol (adoption T2, chantier shared) --
// AMELIORATION (changement de comportement assume, decision utilisateur) : au lieu
// d'un repli constant a chaque lecture invalide (25C historique -> 20C v2.42), on
// adopte la machine d'état SensorFailureManager (shared/n3_analog_sensors) qui
// DESACTIVE le capteur apres N echecs consecutifs (= debranchement durable) et le
// re-teste periodiquement. Le repli VALEUR reste la logique float existante
// (isnan + bornes + derniere valeur valide) : N3SensorFallback (uint16, 0=invalide)
// ne convient PAS a une temperature (negatifs/0 legitimes).
//
// PIEGE DEEP SLEEP : msp deep-sleepe entre chaque cycle -> setup() recree le
// manager et les compteurs RAM repartent a 0 ; sans persistance la desactivation
// ne se declencherait JAMAIS. On persiste donc l'état du manager en RTC_DATA_ATTR
// (POD SensorFailureState) — restauré au reveil, sauvegardé en fin de lecture — et
// on cadence la reactivation PAR CYCLES DE REVEIL (shouldTestReactivationCyclic),
// pas par millis() qui repart a 0 a chaque reveil. Un test de reactivation est donc
// tente a chaque reveil tant que le capteur est desactive (les reveils sont deja
// espaces par FreqWakeUp), et 3 succes consecutifs le reactivent.
//
// Le firmware POSSEDE le stockage RTC (la lib reste sans état statique). Le magic
// protege contre un cold boot (RTC non initialise) et un changement de layout du
// POD apres une OTA : en cas d'incoherence, on repart d'un état neutre.
static const uint8_t  MSP_DS18B20_MAX_FAILURES = 10;  // ~10 reveils d'echec = debranche
static const uint8_t  MSP_DS18B20_REACT_SUCCESSES = 3;
static const uint16_t MSP_FAIL_PERSIST_MAGIC = 0x5301;  // 'S3' + version layout

struct MspFailurePersist {
  uint16_t magic;
  SensorFailureState ds18b20Sol;
  float lastValidTempSol;  // dernier bon relevé (repli VALEUR quand désactivé)
};
RTC_DATA_ATTR static MspFailurePersist s_failurePersist;

// Cadence par cycles : 0 => intervalle millis() inutilise (voie cyclique).
static SensorFailureManager s_ds18b20SolFailure("DS18B20sol", MSP_DS18B20_MAX_FAILURES, 0,
                                                MSP_DS18B20_REACT_SUCCESSES);

// Restaure l'état des managers depuis le RTC au reveil (une fois par cycle),
// avance le compteur de cycles de reveil, et gere le cold boot / OTA via le magic.
static void mspRestoreFailureState() {
  if (s_failurePersist.magic != MSP_FAIL_PERSIST_MAGIC) {
    s_failurePersist.magic = MSP_FAIL_PERSIST_MAGIC;
    s_failurePersist.ds18b20Sol = SensorFailureState{};
    s_failurePersist.lastValidTempSol = NAN;
  }
  s_ds18b20SolFailure.restoreState(s_failurePersist.ds18b20Sol);
  // Un reveil de plus ecoule depuis une eventuelle desactivation (no-op si actif).
  s_ds18b20SolFailure.noteWakeCycle();
}

// Sauvegarde l'état des managers en RTC avant le sommeil. Appelee en fin de
// lecture : robuste aux multiples points de sortie de sommeil() (urgence/normal).
static void mspSaveFailureState() {
  s_failurePersist.ds18b20Sol = s_ds18b20SolFailure.serializeState();
}

// Repli VALEUR pour le DS18B20 sol : derniere valeur valide (RTC) sinon defaut.
static float mspTempSolFallback() {
  const float last = s_failurePersist.lastValidTempSol;
  if (!isnan(last) && last >= MSP_TEMP_MIN && last <= MSP_TEMP_MAX) {
    return last;
  }
  return MSP_TEMP_FALLBACK;
}

void LectureCapteurs() {
  // Restaure la machine d'état de détection de panne depuis le RTC (deep sleep).
  mspRestoreFailureState();

  // Humidite du sol (ADC filtre)
  N3Analog::AnalogResult rHum = N3Analog::readFilteredAnalog(cfgHumidSol);
  HumidSol = rHum.valid ? rHum.value : 1;
  if (HumidSol == 0) HumidSol = 1;
  Serial.printf("[SENSOR] HumidSol=%d\n", HumidSol);

  // Detection pluie via la sortie NUMERIQUE (DO) du module (v2.72).
  // PLUIE (GPIO27) est sur l'ADC2, que la radio requisitionne des que le WiFi
  // est actif : l'ancien analogRead renvoyait 0 -> sentinelle systematique,
  // la mesure analogique n'a jamais fonctionne en conditions reelles.
  // On lit desormais DO (comparateur du module, seuil regle par son
  // potentiometre — cablage J13 de la carte commune n3pp-msp), projete sur
  // l'echelle historique : DO haut = sec -> 4095, DO bas = mouille -> 100.
  // INPUT_PULLUP (main.cpp) : module debranche = ligne haute = « sec » — la
  // distinction « sonde deconnectee » de l'ere analogique n'existe plus.
  Pluie = (digitalRead(PLUIE) == HIGH) ? MSP_PLUIE_DRY_RAW : MSP_PLUIE_WET_RAW;
  delay(100);
  Serial.printf("[SENSOR] Pluie=%d (DO %s)\n", Pluie,
                Pluie == MSP_PLUIE_DRY_RAW ? "sec" : "mouille");

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

  // Temperature sol (DS18B20) — machine d'état de détection de panne (T2).
  // Avant v2.42 : magique 25.00 traite comme erreur. v2.42 : test explicite
  // DEVICE_DISCONNECTED_C, retry une fois, fallback constant. v2.64 : la
  // detection de panne DURABLE est deleguee au SensorFailureManager persiste RTC.
  if (s_ds18b20SolFailure.isDisabled()) {
    // Capteur juge debranche : on saute la lecture normale et on teste la
    // reactivation a chaque reveil (cadence par cycles, pas millis).
    bool reactivated = false;
    if (s_ds18b20SolFailure.shouldTestReactivationCyclic()) {
      sensors.requestTemperatures();
      float testTemp = sensors.getTempCByIndex(0);
      if (testTemp != DEVICE_DISCONNECTED_C && !isnan(testTemp) &&
          testTemp >= MSP_TEMP_MIN && testTemp <= MSP_TEMP_MAX) {
        if (s_ds18b20SolFailure.recordReactivationTestSuccess()) {
          temperatureSol = testTemp;
          s_failurePersist.lastValidTempSol = testTemp;
          Serial.printf("[DS18B20] TempSol=%.2fC (capteur reactive)\n", temperatureSol);
          reactivated = true;
        }
      } else {
        s_ds18b20SolFailure.recordReactivationTestFailure();
      }
    }
    if (!reactivated) {
      temperatureSol = mspTempSolFallback();
      Serial.printf("[DS18B20][WARN] capteur desactive (debranche?), fallback %.1fC\n",
                    temperatureSol);
    }
  } else {
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
      // Echec de ce reveil : le manager comptabilise (desactive apres N reveils).
      s_ds18b20SolFailure.recordFailure();
      temperatureSol = mspTempSolFallback();
      Serial.printf("[DS18B20][WARN] Temperature sol invalide, fallback %.1fC\n",
                    temperatureSol);
    } else {
      s_ds18b20SolFailure.recordSuccess();
      s_failurePersist.lastValidTempSol = temperatureSol;
      Serial.printf("[DS18B20] TempSol=%.2fC\n", temperatureSol);
    }
  }

  // Persiste l'état de détection de panne en RTC avant le sommeil (robuste aux
  // multiples points de sortie de sommeil() : urgence batterie / normal).
  mspSaveFailureState();
}

void batterie() {
  N3BatteryResult res = n3BatteryRead(batteryConfig, (void*)samples, (void*)&sampleIndex, (void*)&sampleTotal);
  avgPontDiv = res.rawAvg;
  measuredVoltage = res.measuredVoltage;
  batteryVoltage = res.batteryVoltage;

  // Harmonisation A7 (lot 0 T6, chantier shared) : PontDiv porte desormais la
  // valeur FILTREE (moyenne n3BatteryRead) et non plus un analogRead brut
  // une-passe — aligne sur n3pp (audit 4.38) : PontDiv pilote la protection
  // batterie (sommeil() + alerte serveur via POST), un pic ADC isole ne doit
  // pas declencher la veille d'urgence ni une fausse alerte.
  PontDiv = avgPontDiv;
  Serial.printf("[BATT] PontDiv=%d\n", PontDiv);

  Serial.printf("[BATT] ADC=%d tension_brute=%.2f V\n", avgPontDiv, measuredVoltage);

  // Affichage OLED uniquement. Bornage 0..100 % et tension en float (l'ancien
  // int batteryVoltage2 tronquait la tension a un entier 0-4 V, valeur inutile).
  int battPercent = (int)(100 - ((2100 - avgPontDiv) * 0.2));
  if (battPercent < 0) battPercent = 0;
  if (battPercent > 100) battPercent = 100;
  float batteryVoltage2 = avgPontDiv * 4.2f / 2100.0f;

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

  // Télémétrie : toujours remplir LuminositeA–D et LuminositeMoy pour le POST
  // serveur (v2.41), en lecture filtrée par position depuis v2.54.
  readLdrTelemetry();

  if (!servoModeAuto) {
    Serial.println("[SERVO][AUTO] scan=OFF raison=mode_manuel");
    applyManualServoTargets();
    rtcAngleServoGD = AngleServoGD;
    rtcAngleServoHB = AngleServoHB;
    return;
  }

  if (photocellReadingMoy <= LIGHT_SCAN_MIN_THRESHOLD) {
    Serial.printf("[SERVO][AUTO] scan=SKIP lum=%d<=seuil=%d\n",
                  photocellReadingMoy, LIGHT_SCAN_MIN_THRESHOLD);
    // Pas de mouvement : AngleServoGD/HB restent la position physique courante
    // (restauree du RTC au boot) — c'est elle qui part en telemetrie, plus les
    // consignes serveur jamais appliquees (constat C1).
    if (displayOk) {
      display.clearDisplay();
      display.setTextSize(2);
      display.setCursor(0, 0);
      display.println("Pas de scan");
      display.println("LumMoy = ");
      display.println(photocellReadingMoy);
      display.display();
      delay(750);
    }
    return;
  }

  if (trackerModeSweep) {
    // === Balayage classique optimise (cle serveur 113=1) ===
    Serial.printf("[SERVO][AUTO] scan=ON algo=BALAYAGE lum=%d seuil=%d\n",
                  photocellReadingMoy, LIGHT_SCAN_MIN_THRESHOLD);
    if (displayOk) {
      display.clearDisplay();
      display.setTextSize(2);
      display.setCursor(0, 0);
      display.println("Scan");
      display.print("LumMoy = ");
      display.println(photocellReadingMoy);
      display.display();
      delay(750);
    }

    N3Tracker::PeakTracker peakA, peakB, peakC, peakD;
    AngleServoGD = sweepAxis(servogd, LUMINOSITEa, LUMINOSITEb,
                             minAngleServoGD, maxAngleServoGD, AngleServoGD,
                             peakA, peakB, "GD");
    posLumMax1 = peakA.bestPos;
    posLumMax2 = peakB.bestPos;
    AngleServoHB = sweepAxis(servohb, LUMINOSITEc, LUMINOSITEd,
                             minAngleServoHB, maxAngleServoHB, AngleServoHB,
                             peakC, peakD, "HB");
    posLumMax3 = peakC.bestPos;
    posLumMax4 = peakD.bestPos;
  } else {
    // === Asservissement differentiel (defaut, cle serveur 113=0/absente) ===
    Serial.printf("[SERVO][AUTO] scan=ON algo=DIFFERENTIEL lum=%d seuil=%d\n",
                  photocellReadingMoy, LIGHT_SCAN_MIN_THRESHOLD);
    const N3Tracker::DiffAxisConfig cfgGd = {
      minAngleServoGD, maxAngleServoGD, DIFF_DEADBAND, DIFF_STEP_DEG, DIFF_INVERT_GD
    };
    AngleServoGD = diffTrackAxis(servogd, LUMINOSITEa, LUMINOSITEb, cfgGd, AngleServoGD, "GD");
    const N3Tracker::DiffAxisConfig cfgHb = {
      minAngleServoHB, maxAngleServoHB, DIFF_DEADBAND, DIFF_STEP_DEG, DIFF_INVERT_HB
    };
    AngleServoHB = diffTrackAxis(servohb, LUMINOSITEc, LUMINOSITEd, cfgHb, AngleServoHB, "HB");
  }

  Serial.printf("[SERVO] angleGD=%d angleHB=%d\n", AngleServoGD, AngleServoHB);
  rtcAngleServoGD = AngleServoGD;
  rtcAngleServoHB = AngleServoHB;

  // Télémétrie post-positionnement : lecture instantanee a la position finale
  // (avant v2.54 le POST envoyait les pics du balayage — constat C4).
  readLdrTelemetry();
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
    delay(750);
  }
}
