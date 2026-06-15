#include <unity.h>

// Tests de ThresholdUtil::subSat (automatism/threshold_util.h), soustraction saturée
// extraite (chantier C4) et dédupliquée de 4 sites de calcul de seuil d'hystérésis.
// Un underflow ici (seuil - marge sur non signé) = seuil d'hystérésis aberrant
// (ex. ~65530 mm) -> alerte jamais clôturée / pompe jamais déverrouillée.
#include "../../include/automatism/threshold_util.h"

using namespace ThresholdUtil;

void setUp(void) {}
void tearDown(void) {}

// Cas nominal : value > sub.
void test_normal_subtraction(void) {
  TEST_ASSERT_EQUAL_UINT16(25, subSat(30, 5));
  TEST_ASSERT_EQUAL_UINT16(20, subSat(30, 10));
  TEST_ASSERT_EQUAL_UINT16(0, subSat(5, 5));   // égalité -> 0 (parité : 5>5 faux)
}

// Saturation : sub >= value -> 0 (pas d'underflow non signé).
void test_saturates_to_zero(void) {
  TEST_ASSERT_EQUAL_UINT16(0, subSat(3, 5));
  TEST_ASSERT_EQUAL_UINT16(0, subSat(0, 10));
  TEST_ASSERT_EQUAL_UINT16(0, subSat(9, 10));
}

// Parité exhaustive avec l'ancien ternaire (value > sub) ? value - sub : 0
// pour les marges réellement utilisées (5 et 10), sur toute la plage uint16_t.
void test_parity_with_old_ternary(void) {
  for (uint32_t v = 0; v <= 65535U; ++v) {
    uint16_t value = static_cast<uint16_t>(v);
    uint16_t ref5 = (value > 5) ? static_cast<uint16_t>(value - 5) : 0;
    uint16_t ref10 = (value > 10) ? static_cast<uint16_t>(value - 10) : 0;
    TEST_ASSERT_EQUAL_UINT16(ref5, subSat(value, 5));
    TEST_ASSERT_EQUAL_UINT16(ref10, subSat(value, 10));
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_normal_subtraction);
  RUN_TEST(test_saturates_to_zero);
  RUN_TEST(test_parity_with_old_ternary);
  return UNITY_END();
}
