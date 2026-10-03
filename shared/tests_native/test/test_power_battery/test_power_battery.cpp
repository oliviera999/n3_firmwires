// Tests Unity natifs pour n3_power / n3_power_battery (SoC plomb/AGM 12 V :
// table OCV, comptage coulometrique, recalage au repos ; alerte batterie basse :
// seuil, maintien, hysteresis, cooldown, charge, debordement de millis()).
//
//   pio test -c platformio-native.ini -e native -f test_power_battery

#include <unity.h>
#include "n3_power_battery.cpp"  // implementation incluse (unite de traduction isolee)

using namespace N3Power;

// Unity est compile sans UNITY_INCLUDE_DOUBLE (config partagee) : on compare
// les doubles en float, largement suffisant aux ordres de grandeur testes.
#define ASSERT_DBL_WITHIN(tol, expected, actual) \
  TEST_ASSERT_FLOAT_WITHIN(static_cast<float>(tol), static_cast<float>(expected), static_cast<float>(actual))

void setUp() {}
void tearDown() {}

// --- Table OCV ---

void test_ocv_bornes_et_interpolation() {
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, ocvToSoc(11.0f, kAgm12vOcvTable, kAgm12vOcvTableSize));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 100.0f, ocvToSoc(13.5f, kAgm12vOcvTable, kAgm12vOcvTableSize));
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 50.0f, ocvToSoc(12.35f, kAgm12vOcvTable, kAgm12vOcvTableSize));
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 45.0f, ocvToSoc(12.30f, kAgm12vOcvTable, kAgm12vOcvTableSize));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -1.0f, ocvToSoc(12.0f, nullptr, 0));
}

void test_ocv_table_croissante() {
  for (size_t i = 1; i < kAgm12vOcvTableSize; ++i) {
    TEST_ASSERT_TRUE(kAgm12vOcvTable[i].volts > kAgm12vOcvTable[i - 1].volts);
    TEST_ASSERT_TRUE(kAgm12vOcvTable[i].socPercent > kAgm12vOcvTable[i - 1].socPercent);
  }
}

// --- SoC ---

void test_soc_graine_ocv_au_premier_update() {
  LeadAcidSoc soc;
  TEST_ASSERT_FALSE(soc.seeded());
  soc.update(12.35f, -0.5f, 0.0, 0.0f);
  TEST_ASSERT_TRUE(soc.seeded());
  TEST_ASSERT_TRUE(soc.seededFromOcv());
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 50.0f, soc.socPercent());
}

void test_soc_comptage_charge_avec_rendement() {
  LeadAcidConfig cfg;  // 12 Ah, rendement 0,9
  LeadAcidSoc soc(cfg);
  soc.seed(50.0f);
  soc.update(13.8f, 2.0f, 1.2, 1.0f);  // +1,2 Ah * 0,9 / 12 Ah = +9 %
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 59.0f, soc.socPercent());
  ASSERT_DBL_WITHIN(1e-7, 1.2, soc.netAh());
}

void test_soc_comptage_decharge_sans_rendement() {
  LeadAcidSoc soc;
  soc.seed(50.0f);
  soc.update(12.1f, -2.0f, -1.2, 1.0f);  // -1,2 Ah / 12 Ah = -10 %
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 40.0f, soc.socPercent());
  soc.resetAh();
  ASSERT_DBL_WITHIN(1e-9, 0.0, soc.netAh());
}

void test_soc_borne_0_100() {
  LeadAcidSoc soc;
  soc.seed(99.0f);
  soc.update(14.4f, 5.0f, 5.0, 1.0f);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 100.0f, soc.socPercent());
  soc.seed(1.0f);
  soc.update(11.0f, -5.0f, -5.0, 1.0f);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, soc.socPercent());
  soc.seed(150.0f);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 100.0f, soc.socPercent());
}

void test_soc_recalage_ocv_apres_repos_une_fois_par_repos() {
  LeadAcidConfig cfg;
  cfg.restSeconds = 10;
  LeadAcidSoc soc(cfg);
  soc.seed(80.0f);
  for (int i = 0; i < 9; ++i) soc.update(12.35f, 0.01f, 0.0, 1.0f);
  TEST_ASSERT_FALSE(soc.atRest());
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 80.0f, soc.socPercent());
  soc.update(12.35f, 0.01f, 0.0, 1.0f);  // 10 s de repos
  TEST_ASSERT_TRUE(soc.atRest());
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 50.0f, soc.socPercent());
  TEST_ASSERT_EQUAL_UINT32(1, soc.ocvResyncCount());
  // Le repos continue : pas de recalage repete
  soc.seed(60.0f);
  soc.update(12.35f, 0.01f, 0.0, 1.0f);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 60.0f, soc.socPercent());
  TEST_ASSERT_EQUAL_UINT32(1, soc.ocvResyncCount());
  // Un courant franc rompt le repos ; un nouveau repos recale a nouveau
  soc.update(12.2f, -1.0f, 0.0, 1.0f);
  TEST_ASSERT_FALSE(soc.atRest());
  for (int i = 0; i < 10; ++i) soc.update(12.45f, -0.05f, 0.0, 1.0f);
  TEST_ASSERT_EQUAL_UINT32(2, soc.ocvResyncCount());
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 60.0f, soc.socPercent());
}

// --- Alerte batterie basse ---

void test_alerte_apres_maintien() {
  LowBatteryAlert a;  // 11,8 V / 60 s / rearm 12,4 V / cooldown 6 h
  TEST_ASSERT_FALSE(a.update(0, 11.5f, -0.5f));
  TEST_ASSERT_FALSE(a.update(59000, 11.5f, -0.5f));
  TEST_ASSERT_TRUE(a.update(60000, 11.5f, -0.5f));
  TEST_ASSERT_EQUAL_UINT32(1, a.alertCount());
  TEST_ASSERT_FALSE(a.armed());
  // Pas de repetition tant que non reamorcee
  TEST_ASSERT_FALSE(a.update(200000, 11.4f, -0.5f));
}

void test_remontee_breve_remet_le_maintien_a_zero() {
  LowBatteryAlert a;
  a.update(0, 11.5f, -0.5f);
  a.update(30000, 11.9f, -0.5f);  // repasse au-dessus du seuil
  TEST_ASSERT_FALSE(a.update(70000, 11.5f, -0.5f));
  TEST_ASSERT_TRUE(a.update(130000, 11.5f, -0.5f));
}

void test_pas_d_alerte_en_charge() {
  LowBatteryAlert a;
  a.update(0, 11.5f, 1.0f);
  TEST_ASSERT_FALSE(a.update(120000, 11.5f, 1.0f));
  TEST_ASSERT_EQUAL_UINT32(0, a.alertCount());
}

void test_hysteresis_et_cooldown() {
  LowBatteryAlert a;
  a.update(0, 11.5f, -0.5f);
  TEST_ASSERT_TRUE(a.update(60000, 11.5f, -0.5f));
  // Entre seuil et reamorcage : toujours desarmee
  TEST_ASSERT_FALSE(a.update(100000, 12.0f, -0.5f));
  TEST_ASSERT_FALSE(a.armed());
  // Reamorcage au-dessus de 12,4 V
  TEST_ASSERT_FALSE(a.update(110000, 12.5f, 0.0f));
  TEST_ASSERT_TRUE(a.armed());
  // Nouvelle chute : maintien atteint mais cooldown de 6 h non ecoule
  a.update(120000, 11.5f, -0.5f);
  TEST_ASSERT_FALSE(a.update(180000, 11.5f, -0.5f));
  // Cooldown ecoule (6 h apres la 1re alerte) : alerte
  TEST_ASSERT_TRUE(a.update(60000u + 21600000u, 11.5f, -0.5f));
  TEST_ASSERT_EQUAL_UINT32(2, a.alertCount());
}

void test_alerte_debordement_millis() {
  LowBatteryAlert a;
  a.update(0xFFFFC000u, 11.5f, -0.5f);                     // 16 384 ms avant le debordement
  TEST_ASSERT_FALSE(a.update(0xFFFFFFFFu, 11.5f, -0.5f));  // 16 383 ms ecoulees
  TEST_ASSERT_TRUE(a.update(0x0000B000u, 11.5f, -0.5f));   // 16 384 + 45 056 = 61 440 ms
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_ocv_bornes_et_interpolation);
  RUN_TEST(test_ocv_table_croissante);
  RUN_TEST(test_soc_graine_ocv_au_premier_update);
  RUN_TEST(test_soc_comptage_charge_avec_rendement);
  RUN_TEST(test_soc_comptage_decharge_sans_rendement);
  RUN_TEST(test_soc_borne_0_100);
  RUN_TEST(test_soc_recalage_ocv_apres_repos_une_fois_par_repos);
  RUN_TEST(test_alerte_apres_maintien);
  RUN_TEST(test_remontee_breve_remet_le_maintien_a_zero);
  RUN_TEST(test_pas_d_alerte_en_charge);
  RUN_TEST(test_hysteresis_et_cooldown);
  RUN_TEST(test_alerte_debordement_millis);
  return UNITY_END();
}
