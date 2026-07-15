#include <unity.h>

// Tests de la logique pure du harnais OTA périodique mutualisé (T4.2) :
// - OtaPeriodic (n3_common/n3_ota_periodic.h) : cadence 2 h persistée RTC,
//   cumul saturant — une dérive ferait soit marteler le serveur OTA, soit ne
//   plus jamais vérifier les mises à jour ;
// - n3_ota_ui_logic.h (n3_ota_ui) : détection « déjà à jour », pourcentage de
//   l'écran de fin, largeur de la barre de progression.
// Assertions à parité avec le comportement historique de n3pp/msp main.cpp.
#include "n3_ota_periodic.h"
#include "n3_ota_ui_logic.h"

static const uint32_t INTERVAL = OtaPeriodic::kDefaultIntervalSeconds;  // 7200 s

void setUp(void) {}
void tearDown(void) {}

// --- OtaPeriodic : cadence ---

void test_default_interval_is_two_hours(void) {
  TEST_ASSERT_EQUAL_UINT32(2UL * 60UL * 60UL, INTERVAL);
}

void test_due_at_or_above_interval(void) {
  TEST_ASSERT_FALSE(OtaPeriodic::due(0, INTERVAL));
  TEST_ASSERT_FALSE(OtaPeriodic::due(INTERVAL - 1, INTERVAL));
  TEST_ASSERT_TRUE(OtaPeriodic::due(INTERVAL, INTERVAL));
  TEST_ASSERT_TRUE(OtaPeriodic::due(INTERVAL + 1, INTERVAL));
}

void test_remaining_counts_down_to_zero(void) {
  TEST_ASSERT_EQUAL_UINT32(INTERVAL, OtaPeriodic::remainingSeconds(0, INTERVAL));
  TEST_ASSERT_EQUAL_UINT32(1, OtaPeriodic::remainingSeconds(INTERVAL - 1, INTERVAL));
  TEST_ASSERT_EQUAL_UINT32(0, OtaPeriodic::remainingSeconds(INTERVAL, INTERVAL));
  TEST_ASSERT_EQUAL_UINT32(0, OtaPeriodic::remainingSeconds(INTERVAL + 5, INTERVAL));
}

void test_accumulate_adds_below_interval(void) {
  // Cas nominal : un réveil de 300 s s'ajoute au cumul.
  TEST_ASSERT_EQUAL_UINT32(300, OtaPeriodic::accumulate(0, INTERVAL, 300));
  TEST_ASSERT_EQUAL_UINT32(600, OtaPeriodic::accumulate(300, INTERVAL, 300));
}

void test_accumulate_saturates_at_interval(void) {
  // Le cumul plafonne à l'intervalle : un long passage hors ligne ne déborde pas.
  TEST_ASSERT_EQUAL_UINT32(INTERVAL, OtaPeriodic::accumulate(INTERVAL - 10, INTERVAL, 10));
  TEST_ASSERT_EQUAL_UINT32(INTERVAL, OtaPeriodic::accumulate(INTERVAL - 10, INTERVAL, 10000));
  TEST_ASSERT_EQUAL_UINT32(INTERVAL, OtaPeriodic::accumulate(0, INTERVAL, 0xFFFFFFFFu));
}

void test_accumulate_noop_when_already_due(void) {
  // Comportement historique : compteur déjà au plafond -> inchangé.
  TEST_ASSERT_EQUAL_UINT32(INTERVAL, OtaPeriodic::accumulate(INTERVAL, INTERVAL, 500));
}

void test_accumulate_zero_delta_is_noop(void) {
  TEST_ASSERT_EQUAL_UINT32(42, OtaPeriodic::accumulate(42, INTERVAL, 0));
}

void test_accumulate_no_uint32_overflow(void) {
  // elapsed proche du plafond + gros delta : pas de wrap-around 32 bits.
  const uint32_t bigInterval = 0xFFFFFFF0u;
  TEST_ASSERT_EQUAL_UINT32(bigInterval,
      OtaPeriodic::accumulate(bigInterval - 1, bigInterval, 0xFFFFFFFFu));
}

void test_boot_scenario_first_check_immediate(void) {
  // Au premier boot, le compteur RTC est initialisé À l'intervalle -> check
  // immédiat, puis remise à zéro et cumul réveil par réveil.
  uint32_t elapsed = INTERVAL;
  TEST_ASSERT_TRUE(OtaPeriodic::due(elapsed, INTERVAL));
  elapsed = 0;  // le harnais remet à zéro après le check
  for (int i = 0; i < 23; ++i) {
    elapsed = OtaPeriodic::accumulate(elapsed, INTERVAL, 300);
    TEST_ASSERT_FALSE(OtaPeriodic::due(elapsed, INTERVAL));
  }
  elapsed = OtaPeriodic::accumulate(elapsed, INTERVAL, 300);  // 24 x 300 s = 7200 s
  TEST_ASSERT_TRUE(OtaPeriodic::due(elapsed, INTERVAL));
}

// --- n3_ota_ui_logic : écran ---

void test_end_up_to_date_detection(void) {
  TEST_ASSERT_TRUE(n3OtaUiEndIsUpToDate("firmware deja a jour"));
  TEST_ASSERT_TRUE(n3OtaUiEndIsUpToDate("pas de mise a jour disponible"));
  TEST_ASSERT_FALSE(n3OtaUiEndIsUpToDate("erreur HTTP 500"));
  TEST_ASSERT_FALSE(n3OtaUiEndIsUpToDate(""));
  TEST_ASSERT_FALSE(n3OtaUiEndIsUpToDate(nullptr));
}

void test_end_percent(void) {
  // À jour -> 100 ; échec sans rendu préalable (255) -> 0 ; sinon dernier rendu.
  TEST_ASSERT_EQUAL_UINT8(100, n3OtaUiEndPercent(true, 255));
  TEST_ASSERT_EQUAL_UINT8(100, n3OtaUiEndPercent(true, 42));
  TEST_ASSERT_EQUAL_UINT8(0, n3OtaUiEndPercent(false, 255));
  TEST_ASSERT_EQUAL_UINT8(42, n3OtaUiEndPercent(false, 42));
}

void test_progress_fill_width(void) {
  // Écran 128 px, barre utile 126 px (bordure 1 px de chaque côté).
  TEST_ASSERT_EQUAL_INT(0, n3OtaUiProgressFillWidth(128, 0));
  TEST_ASSERT_EQUAL_INT(63, n3OtaUiProgressFillWidth(128, 50));
  TEST_ASSERT_EQUAL_INT(126, n3OtaUiProgressFillWidth(128, 100));
  TEST_ASSERT_EQUAL_INT(126, n3OtaUiProgressFillWidth(128, 255));  // clamp >100
  TEST_ASSERT_EQUAL_INT(1, n3OtaUiProgressFillWidth(128, 1));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_default_interval_is_two_hours);
  RUN_TEST(test_due_at_or_above_interval);
  RUN_TEST(test_remaining_counts_down_to_zero);
  RUN_TEST(test_accumulate_adds_below_interval);
  RUN_TEST(test_accumulate_saturates_at_interval);
  RUN_TEST(test_accumulate_noop_when_already_due);
  RUN_TEST(test_accumulate_zero_delta_is_noop);
  RUN_TEST(test_accumulate_no_uint32_overflow);
  RUN_TEST(test_boot_scenario_first_check_immediate);
  RUN_TEST(test_end_up_to_date_detection);
  RUN_TEST(test_end_percent);
  RUN_TEST(test_progress_fill_width);
  return UNITY_END();
}
