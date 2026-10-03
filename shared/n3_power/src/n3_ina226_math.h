/**
 * n3_ina226_math — Logique pure du moniteur de puissance INA226 (TI).
 *
 * Registres, encodage CONFIG, calcul de calibration et conversions brutes ->
 * grandeurs physiques. Aucune dependance Arduino : le driver I2C
 * (n3_ina226.h) fait les acces bus et delegue ici ; tout est verifiable en
 * tests natifs Unity (test_ina226_math).
 *
 * References :
 *  - TI INA226 datasheet SBOS547A (§7.5 Programming, §7.6 Register Maps,
 *    equations 1-4 : CAL = 0.00512 / (Current_LSB * R_shunt), Power_LSB =
 *    25 * Current_LSB) — https://www.ti.com/lit/ds/symlink/ina226.pdf
 *  - Approche de calibration (LSB courant derive du courant max, borne du
 *    registre CAL, recalcul du LSB effectif) inspiree de robtillaart/INA226
 *    (licence MIT) — https://github.com/RobTillaart/INA226 — reecrite, pas copiee.
 *
 * Points a retenir (banc d'essai energie) :
 *  - Pleine echelle shunt = +/-81,92 mV (LSB 2,5 uV) : un module "R100"
 *    (0,1 ohm) sature a ~0,82 A ; un shunt 10 mohm monte a ~8,2 A.
 *  - Tension bus 0..36 V (LSB 1,25 mV), mesuree entre VBUS et GND.
 *  - Au power-on reset le registre CAL vaut 0 : courant et puissance lisent 0
 *    tant que le firmware ne l'a pas reecrit (cf. rail +3V3_SW commute).
 */
#pragma once

#include <stdint.h>

namespace N3Ina226 {

// --- Carte des registres (SBOS547 §7.6) ---
constexpr uint8_t kRegConfig = 0x00;
constexpr uint8_t kRegShuntVoltage = 0x01;
constexpr uint8_t kRegBusVoltage = 0x02;
constexpr uint8_t kRegPower = 0x03;
constexpr uint8_t kRegCurrent = 0x04;
constexpr uint8_t kRegCalibration = 0x05;
constexpr uint8_t kRegMaskEnable = 0x06;
constexpr uint8_t kRegAlertLimit = 0x07;
constexpr uint8_t kRegManufacturerId = 0xFE;
constexpr uint8_t kRegDieId = 0xFF;

constexpr uint16_t kManufacturerIdTi = 0x5449;  // "TI"
constexpr uint16_t kDieIdIna226 = 0x2260;       // DID 0x226, revision 0
constexpr uint16_t kDieIdMask = 0xFFF0;         // bits 15-4 = device id

constexpr uint16_t kConfigResetBit = 0x8000;
constexpr uint16_t kConfigReservedBits = 0x4000;  // D14 lit 1 (defaut 0x4127)
constexpr uint16_t kConfigDefault = 0x4127;       // AVG=1, 1,1 ms, continu

// MASK/ENABLE (SBOS547 §7.6.7)
constexpr uint16_t kMaskBusUnderVoltage = 0x1000;  // BUL
constexpr uint16_t kMaskConversionReady = 0x0008;  // CVRF
constexpr uint16_t kMaskMathOverflow = 0x0004;     // OVF

// --- Constantes physiques ---
constexpr float kShuntLsbV = 2.5e-6f;           // 2,5 uV
constexpr float kBusLsbV = 1.25e-3f;            // 1,25 mV
constexpr float kShuntFullScaleV = 0.08192f;    // 32767 * 2,5 uV
constexpr float kBusMaxV = 36.0f;               // tension bus max mesurable (recommandee)
constexpr float kCalibrationConstant = 0.00512f;
constexpr uint16_t kCalibrationMax = 0x7FFF;    // bit 15 reserve
constexpr float kPowerLsbFactor = 25.0f;
/** |shunt brut| >= ce seuil (~97,7 % de la pleine echelle) : voie saturee. */
constexpr int16_t kShuntSaturationRaw = 32000;

// --- Modes de fonctionnement (bits 2-0) ---
constexpr uint8_t kModePowerDown = 0;
constexpr uint8_t kModeShuntBusTriggered = 3;
constexpr uint8_t kModeShuntBusContinuous = 7;

/** Nombre de moyennes pour un code AVG (0..7) : 1,4,16,64,128,256,512,1024. */
uint16_t avgSamplesForCode(uint8_t code);
/** Plus grand code AVG dont le nombre de moyennes est <= samples (min code 0). */
uint8_t avgCodeForSamples(uint16_t samples);

/** Temps de conversion (us) d'un code CT (0..7) : 140,204,332,588,1100,2116,4156,8244. */
uint32_t conversionTimeUs(uint8_t code);
/** Plus grand code CT dont la duree est <= us (min code 0). */
uint8_t conversionCodeForUs(uint32_t us);

/** Mot CONFIG (D14 force a 1, RST a 0). Les champs sont bornes a 3 bits. */
uint16_t encodeConfig(uint8_t avgCode, uint8_t busCtCode, uint8_t shuntCtCode, uint8_t mode);

struct ConfigFields {
  uint8_t avgCode;
  uint8_t busCtCode;
  uint8_t shuntCtCode;
  uint8_t mode;
};
ConfigFields decodeConfig(uint16_t config);

/** Duree (us) d'un cycle complet de mesure bus + shunt moyenne. */
uint32_t cycleTimeUs(uint16_t config);

/** Resultat de calibration d'une voie. */
struct Calibration {
  bool ok;                  // parametres exploitables, cal != 0
  uint16_t cal;             // valeur a ecrire dans le registre 0x05
  float currentLsbA;        // LSB courant EFFECTIF (recalcule depuis cal tronque)
  float powerLsbW;          // 25 * currentLsbA
  float shuntLimitA;        // courant max avant saturation du shunt (81,92 mV / R)
  float measurableMaxA;     // min(shuntLimitA, 32767 * currentLsbA)
  bool calClamped;          // cal borne a 0x7FFF -> LSB elargi (resolution moindre)
  bool imaxBeyondShunt;     // courant max demande > pleine echelle du shunt
};

/**
 * Calibration (SBOS547 eq. 1-2) : Current_LSB = Imax / 2^15, CAL =
 * trunc(0.00512 / (Current_LSB * Rshunt)). Le LSB effectif est recalcule a
 * partir du CAL tronque pour ne pas introduire de biais. Si CAL depasse
 * 0x7FFF (shunt tres faible + Imax faible), le LSB est elargi pour tenir.
 * ok=false si shuntOhm <= 0, maxCurrentA <= 0 ou CAL nul.
 */
Calibration computeCalibration(float shuntOhm, float maxCurrentA);

/** Tension bus (V) depuis le registre 0x02 (bit 15 toujours 0). */
float busVoltageV(uint16_t raw);
/** Tension shunt (mV) depuis le registre 0x01 (complement a 2). */
float shuntVoltageMv(int16_t raw);
/** Courant (A) depuis le registre 0x04 (complement a 2). */
float currentFromRegisterA(int16_t raw, float currentLsbA);
/** Courant (A) recalcule depuis la tension shunt et R (diagnostic). */
float currentFromShuntA(float shuntMv, float shuntOhm);
/** Puissance (W) depuis le registre 0x03 (non signe = valeur absolue). */
float powerFromRegisterW(uint16_t raw, float currentLsbA);

/** Voie saturee : |shunt brut| proche de la pleine echelle (+/-81,92 mV). */
bool isShuntSaturated(int16_t shuntRaw);

/** Applique gain (correction multimetre) et inversion de sens a un courant. */
float applyCurrentCorrection(float currentA, float gain, bool invert);

/**
 * Nouveau gain apres comparaison a un appareil de reference :
 * gain' = gain * reference / mesure (mesure = courant DEJA corrige par gain).
 * Retourne currentGain (inchange) si |mesure| < minMeasuredA, si les signes
 * different ou si le resultat sort de [minGain, maxGain] ; *ok indique si la
 * correction a ete appliquee (peut etre nullptr).
 */
float computeGainCorrection(float currentGain, float measuredA, float referenceA,
                            float minMeasuredA, float minGain, float maxGain, bool* ok);

/** Adresse I2C 7 bits plausible pour un INA226 (0x40..0x4F, broches A0/A1). */
bool isValidAddress(uint8_t addr);

}  // namespace N3Ina226
