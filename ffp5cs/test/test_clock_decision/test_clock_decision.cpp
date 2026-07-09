#include <unity.h>

#include <cmath>
#include <cstdint>
#include <ctime>

// Tests de ClockDecision (clock_decision.h), extrait de power.cpp (chantier C4).
// Deux logiques critiques pour l'horloge et l'autonomie :
//   - plausibilité d'une heure NTP (année + saut d'epoch / proximité compile) ;
//   - mesure de dérive RTC en PPM avec bornage.
// Un bug ici fige une horloge fausse ou applique une correction excessive.
#include "n3_clock_decision.h"

using namespace ClockDecision;

// Repères temporels réalistes (UTC).
static const time_t COMPILE = 1780444800;  // 2026-06-03 00:00:00 (EPOCH_COMPILE_TIME secours)
static const time_t NOW2026 = 1781000000;  // ~2026-06-09, > 60 s du compile mais hors fenêtre 7j ? non : ~6.4j
static const time_t FAR2027 = 1800000000;  // 2027, très loin du compile

// Tolérance float pour comparaisons PPM (le shim local n'a pas TEST_ASSERT_FLOAT_WITHIN).
static bool floatNear(float a, float b, float tol) {
  return std::fabs(a - b) <= tol;
}

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// isPlausibleYear : reproduit `tm_year + 1900 < 2024` (rejet).
// ---------------------------------------------------------------------------
void test_year_boundary_2024_valid(void) {
  // tm_year est l'année - 1900. 2024 -> 124.
  TEST_ASSERT_TRUE(isPlausibleYear(124));   // 2024 : accepté (>= 2024)
  TEST_ASSERT_TRUE(isPlausibleYear(126));   // 2026
  TEST_ASSERT_TRUE(isPlausibleYear(200));   // futur lointain
}

void test_year_boundary_2023_invalid(void) {
  TEST_ASSERT_FALSE(isPlausibleYear(123));  // 2023 : rejeté
  TEST_ASSERT_FALSE(isPlausibleYear(70));   // 1970
  TEST_ASSERT_FALSE(isPlausibleYear(0));    // 1900
}

void test_year_custom_threshold(void) {
  // Seuil injectable : avec minYear=2027, 2026 doit être rejeté.
  TEST_ASSERT_FALSE(isPlausibleYear(126, 2027));
  TEST_ASSERT_TRUE(isPlausibleYear(127, 2027));
}

// ---------------------------------------------------------------------------
// isNtpEpochPlausible : (changé d'au moins 60 s) OU (proche compile < 7 j).
// ---------------------------------------------------------------------------
void test_ntp_changed_enough_accepts(void) {
  // ntp saute de +120 s vs localBefore -> epochChangedEnough -> plausible
  // (peu importe la proximité compile).
  TEST_ASSERT_TRUE(isNtpEpochPlausible(NOW2026 + 120, NOW2026, COMPILE));
}

void test_ntp_diff_below_60_and_far_rejects(void) {
  // Saut de +59 s (< 60) ET très loin du compile -> rejeté.
  TEST_ASSERT_FALSE(isNtpEpochPlausible(FAR2027 + 59, FAR2027, COMPILE));
}

void test_ntp_diff_exactly_60_accepts(void) {
  // Borne : >= 60 s accepté (parité avec NTP_MIN_EPOCH_DELTA_SEC inclusif).
  TEST_ASSERT_TRUE(isNtpEpochPlausible(FAR2027 + 60, FAR2027, COMPILE));
}

void test_ntp_small_diff_but_near_compile_accepts(void) {
  // Saut de seulement +10 s (< 60) MAIS ntp proche du compile (< 7 j) -> accepté
  // via nearCompile (cas premier boot : heure locale ~= compile).
  TEST_ASSERT_TRUE(isNtpEpochPlausible(COMPILE + 10, COMPILE, COMPILE));
}

void test_ntp_near_compile_boundary_exclusive(void) {
  // nearCompile est STRICT (<). À exactement 7 j -> pas "near", et si diff<60 -> rejet.
  const time_t sevenDays = 7 * 86400;
  // ntp == localBefore (diff 0 < 60), ntp à exactement 7 j du compile -> rejeté.
  TEST_ASSERT_FALSE(isNtpEpochPlausible(COMPILE + sevenDays, COMPILE + sevenDays, COMPILE));
  // ntp à 7j - 1 s du compile, diff 0 < 60 -> accepté (near strict OK).
  TEST_ASSERT_TRUE(isNtpEpochPlausible(COMPILE + sevenDays - 1, COMPILE + sevenDays - 1, COMPILE));
}

void test_ntp_custom_thresholds(void) {
  // Seuils injectables : minDelta=10 rend un saut de 10 s acceptable même loin.
  TEST_ASSERT_TRUE(isNtpEpochPlausible(FAR2027 + 10, FAR2027, COMPILE, 10, 1));
  TEST_ASSERT_FALSE(isNtpEpochPlausible(FAR2027 + 9, FAR2027, COMPILE, 10, 1));
}

// ---------------------------------------------------------------------------
// computeDriftPpm : ppm = (réel - attendu)/attendu * 1e6, borné à [-max, max].
// ---------------------------------------------------------------------------
void test_drift_zero_when_perfect(void) {
  // 100 s écoulées (100000 ms) et 100 s réelles -> dérive nulle.
  TEST_ASSERT_TRUE(floatNear(0.0f, computeDriftPpm(100, 100000UL), 0.001f));
}

void test_drift_positive_formula(void) {
  // attendu = 100000/1000 = 100 s ; réel = 101 s ; drift = +1 s.
  // ppm = 1/100 * 1e6 = 10000 PPM, mais borné à 500 -> 500.
  TEST_ASSERT_TRUE(floatNear(500.0f, computeDriftPpm(101, 100000UL), 0.001f));
}

void test_drift_small_positive_unclamped(void) {
  // Dérive fractionnaire sous la borne (non écrêtée) : le drift fractionnaire
  // vient de millisDiff (timeDiff est entier). attendu = 999.9 s (millisDiff
  // 999900) ; réel = 1000 s -> drift 0.1 s -> ppm = 0.1/999.9*1e6 ≈ 100.01.
  float ppm = computeDriftPpm(1000, 999900UL);
  TEST_ASSERT_TRUE(floatNear(100.0f, ppm, 0.5f));
  // attendu = 999.5 s (millisDiff 999500) ; réel = 1000 s -> drift 0.5 s ->
  // ppm ≈ 0.5/999.5*1e6 ≈ 500.25, écrêté à la borne 500.
  TEST_ASSERT_TRUE(floatNear(DRIFT_PPM_MAX, computeDriftPpm(1000, 999500UL), 0.001f));
}

void test_drift_negative_clamped(void) {
  // réel beaucoup plus petit que attendu -> dérive négative énorme -> bornée à -500.
  // attendu = 100 s, réel = 50 s -> drift -50 -> ppm = -500000 -> -500.
  TEST_ASSERT_TRUE(floatNear(-500.0f, computeDriftPpm(50, 100000UL), 0.001f));
}

void test_drift_zero_elapsed_returns_zero(void) {
  // millisDiff == 0 -> 0 (garde-fou parité, pas de division).
  TEST_ASSERT_TRUE(floatNear(0.0f, computeDriftPpm(5, 0UL), 0.001f));
}

void test_drift_seconds_component(void) {
  // computeDriftSeconds renvoie le drift brut (non borné, non PPM).
  // attendu=100 s, réel=101 s -> +1.0 s.
  TEST_ASSERT_TRUE(floatNear(1.0f, computeDriftSeconds(101, 100000UL), 0.001f));
  // millisDiff 0 -> 0.
  TEST_ASSERT_TRUE(floatNear(0.0f, computeDriftSeconds(101, 0UL), 0.001f));
}

void test_drift_custom_max(void) {
  // Borne injectable : max=10000 -> le 10000 PPM brut n'est plus écrêté.
  TEST_ASSERT_TRUE(floatNear(10000.0f, computeDriftPpm(101, 100000UL, 10000.0f), 0.1f));
}

// ---------------------------------------------------------------------------
// Parité brute-force : transcription INLINE de l'original power.cpp:307-323
// comparée à computeDriftPpm() sur de nombreuses entrées (comme la boucle de
// parité du chantier sleep blocking).
// ---------------------------------------------------------------------------
static float originalInlinePpm(time_t timeDiff, unsigned long millisDiff, float maxPpm) {
  // Transcription FIDÈLE du bloc inline de syncTimeFromNTP (avant extraction).
  float result;
  if (millisDiff > 0) {
    float expectedSeconds = millisDiff / 1000.0f;
    float actualSeconds = static_cast<float>(timeDiff);
    float driftSeconds = actualSeconds - expectedSeconds;
    if (expectedSeconds > 0.0f) {
      result = (driftSeconds / expectedSeconds) * 1000000.0f;
      result = std::fmax(-maxPpm, std::fmin(maxPpm, result));
    } else {
      result = 0.0f;
    }
  } else {
    result = 0.0f;
  }
  return result;
}

void test_parity_drift_ppm_bruteforce(void) {
  const float MAXP = DRIFT_PPM_MAX;
  // Balayage : timeDiff de -5 à +200 s, millisDiff de 0 à ~300 s par pas variés.
  for (int td = -5; td <= 200; ++td) {
    for (unsigned long md = 0; md <= 300000UL; md += 250UL) {
      float ref = originalInlinePpm(static_cast<time_t>(td), md, MAXP);
      float got = computeDriftPpm(static_cast<time_t>(td), md, MAXP);
      // Identique au bit près attendu (mêmes opérations float, même ordre).
      TEST_ASSERT_TRUE(floatNear(ref, got, 0.0001f));
    }
  }
}

// Parité brute-force de la décision NTP vs transcription inline (power.cpp:273-277).
static bool originalInlineNtpPlausible(time_t ntp, time_t localBefore, time_t compile,
                                       time_t minDelta, time_t compileTol) {
  const bool epochChangedEnough = EpochUtil::epochAbsDiff(ntp, localBefore) >= minDelta;
  const bool nearCompile = EpochUtil::epochAbsDiff(ntp, compile) < compileTol;
  return epochChangedEnough || nearCompile;
}

void test_parity_ntp_plausible_bruteforce(void) {
  const time_t minDelta = NTP_MIN_EPOCH_DELTA_SEC;
  const time_t compileTol = NTP_COMPILE_TOLERANCE_SEC;
  const time_t base = COMPILE;
  for (long dn = -200; dn <= 200; dn += 7) {
    for (long dl = -200; dl <= 200; dl += 11) {
      // ntp balaie aussi de larges écarts au compile pour toucher nearCompile.
      for (long extra = -8 * 86400; extra <= 8 * 86400; extra += 86400) {
        time_t ntp = base + extra + dn;
        time_t localBefore = base + extra + dl;
        bool ref = originalInlineNtpPlausible(ntp, localBefore, base, minDelta, compileTol);
        bool got = isNtpEpochPlausible(ntp, localBefore, base, minDelta, compileTol);
        TEST_ASSERT_TRUE(ref == got);
      }
    }
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_year_boundary_2024_valid);
  RUN_TEST(test_year_boundary_2023_invalid);
  RUN_TEST(test_year_custom_threshold);
  RUN_TEST(test_ntp_changed_enough_accepts);
  RUN_TEST(test_ntp_diff_below_60_and_far_rejects);
  RUN_TEST(test_ntp_diff_exactly_60_accepts);
  RUN_TEST(test_ntp_small_diff_but_near_compile_accepts);
  RUN_TEST(test_ntp_near_compile_boundary_exclusive);
  RUN_TEST(test_ntp_custom_thresholds);
  RUN_TEST(test_drift_zero_when_perfect);
  RUN_TEST(test_drift_positive_formula);
  RUN_TEST(test_drift_small_positive_unclamped);
  RUN_TEST(test_drift_negative_clamped);
  RUN_TEST(test_drift_zero_elapsed_returns_zero);
  RUN_TEST(test_drift_seconds_component);
  RUN_TEST(test_drift_custom_max);
  RUN_TEST(test_parity_drift_ppm_bruteforce);
  RUN_TEST(test_parity_ntp_plausible_bruteforce);
  return UNITY_END();
}
