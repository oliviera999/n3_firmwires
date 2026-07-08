// Tests Unity natifs pour n3_tracker (logique pure du tracker solaire msp :
// suivi de pics, fusion ponderee, fenetre fine, asservissement differentiel).
//
//   pio test -c platformio-native.ini -e native -f test_tracker
//
// Cette logique pilote physiquement les servos du panneau : chaque regle
// (validation de pic, zone morte, anti-oscillation, butee, divergence) est
// verifiee ici avant tout deploiement terrain.

#include <unity.h>
#include "n3_tracker.cpp"  // implémentation incluse (unité de traduction isolée)

using namespace N3Tracker;

void setUp() {}
void tearDown() {}

// --- clampAngle ---

void test_clampAngle() {
  TEST_ASSERT_EQUAL_INT(40, clampAngle(10, 40, 145));
  TEST_ASSERT_EQUAL_INT(145, clampAngle(200, 40, 145));
  TEST_ASSERT_EQUAL_INT(90, clampAngle(90, 40, 145));
}

// --- PeakTracker ---

void test_peak_vide_invalide() {
  PeakTracker p;
  TEST_ASSERT_FALSE(p.valid(0));
}

void test_peak_garde_premiere_position_a_egalite() {
  PeakTracker p;
  p.feed(10, 500);
  p.feed(20, 500);  // meme amplitude -> position 10 conservee
  p.feed(30, 499);
  TEST_ASSERT_EQUAL_INT(10, p.bestPos);
  TEST_ASSERT_EQUAL_INT(500, p.bestVal);
  TEST_ASSERT_TRUE(p.valid(500));
  TEST_ASSERT_FALSE(p.valid(501));
}

void test_peak_reset() {
  PeakTracker p;
  p.feed(10, 500);
  p.reset();
  TEST_ASSERT_FALSE(p.valid(0));
  TEST_ASSERT_EQUAL_INT(-1, p.bestPos);
}

// --- fusePeaks ---

void test_fuse_deux_pics_pondere_par_amplitude() {
  PeakTracker a, b;
  a.feed(100, 3000);
  b.feed(140, 1000);
  // (100*3000 + 140*1000) / 4000 = 110
  TEST_ASSERT_EQUAL_INT(110, fusePeaks(a, b, 200, 90, 1, 179));
}

void test_fuse_amplitudes_egales_donne_le_milieu() {
  PeakTracker a, b;
  a.feed(100, 2000);
  b.feed(120, 2000);
  TEST_ASSERT_EQUAL_INT(110, fusePeaks(a, b, 200, 90, 1, 179));
}

void test_fuse_un_seul_pic_valide_pas_de_division_par_deux() {
  // Cas C2 de l'audit : LDR B ombragee (pic nul), soleil a 120.
  // L'ancien (pos1+pos2)/2 donnait 60 ; on doit suivre la seule LDR valide.
  PeakTracker a, b;
  a.feed(120, 3000);
  b.feed(5, 40);  // sous minPeakValue
  TEST_ASSERT_EQUAL_INT(120, fusePeaks(a, b, 200, 90, 1, 179));
  TEST_ASSERT_EQUAL_INT(120, fusePeaks(b, a, 200, 90, 1, 179));  // symetrique
}

void test_fuse_aucun_pic_retourne_fallback() {
  PeakTracker a, b;
  a.feed(50, 10);
  b.feed(60, 20);
  TEST_ASSERT_EQUAL_INT(92, fusePeaks(a, b, 200, 92, 40, 145));
}

void test_fuse_resultat_borne() {
  PeakTracker a, b;
  a.feed(10, 3000);
  b.feed(20, 3000);
  // fusion = 15, sous minAngle 40 -> clamp
  TEST_ASSERT_EQUAL_INT(40, fusePeaks(a, b, 200, 92, 40, 145));
}

// --- applyGainPermille / computeEqualizationGains (calibration LDR) ---

void test_gain_neutre_passthrough() {
  TEST_ASSERT_EQUAL_INT(1234, applyGainPermille(1234, kGainNeutralPermille, 4095));
}

void test_gain_amplifie_et_borne() {
  TEST_ASSERT_EQUAL_INT(1500, applyGainPermille(1000, 1500, 4095));
  TEST_ASSERT_EQUAL_INT(4095, applyGainPermille(4000, 1500, 4095));  // clamp haut
  TEST_ASSERT_EQUAL_INT(0, applyGainPermille(-50, 1500, 4095));      // brut negatif
}

void test_equalization_nominal() {
  // Pics d'un balayage plein soleil : B et C moins sensibles que A/D.
  const int refs[4] = {4000, 3200, 3600, 4000};
  int gains[4];
  TEST_ASSERT_TRUE(computeEqualizationGains(refs, 4, 100, 500, 2000, gains));
  TEST_ASSERT_EQUAL_INT(1000, gains[0]);
  TEST_ASSERT_EQUAL_INT(1250, gains[1]);  // 1000*4000/3200
  TEST_ASSERT_EQUAL_INT(1111, gains[2]);  // 1000*4000/3600
  TEST_ASSERT_EQUAL_INT(1000, gains[3]);
}

void test_equalization_ref_invalide_gain_neutre() {
  // LDR C ombragee/HS pendant la mesure : gain neutre pour elle, calibration
  // signalee incomplete, les autres restent calibrees.
  const int refs[4] = {4000, 3200, 40, 4000};
  int gains[4];
  TEST_ASSERT_FALSE(computeEqualizationGains(refs, 4, 100, 500, 2000, gains));
  TEST_ASSERT_EQUAL_INT(1250, gains[1]);
  TEST_ASSERT_EQUAL_INT(kGainNeutralPermille, gains[2]);
}

void test_equalization_gain_borne() {
  // Ecart de sensibilite extreme : le gain est borne (capteur suspect).
  const int refs[2] = {4000, 1200};
  int gains[2];
  TEST_ASSERT_TRUE(computeEqualizationGains(refs, 2, 100, 500, 2000, gains));
  TEST_ASSERT_EQUAL_INT(2000, gains[1]);  // 3333 -> clamp 2000
}

void test_equalization_tout_invalide() {
  const int refs[4] = {0, 10, 20, 30};
  int gains[4];
  TEST_ASSERT_FALSE(computeEqualizationGains(refs, 4, 100, 500, 2000, gains));
  for (int i = 0; i < 4; ++i) {
    TEST_ASSERT_EQUAL_INT(kGainNeutralPermille, gains[i]);
  }
}

// --- fineWindow ---

void test_fineWindow_centre() {
  ScanWindow w = fineWindow(90, 6, 1, 179);
  TEST_ASSERT_EQUAL_INT(84, w.from);
  TEST_ASSERT_EQUAL_INT(96, w.to);
}

void test_fineWindow_bornee_aux_limites() {
  ScanWindow w = fineWindow(3, 6, 1, 179);
  TEST_ASSERT_EQUAL_INT(1, w.from);
  TEST_ASSERT_EQUAL_INT(9, w.to);
  w = fineWindow(178, 6, 1, 179);
  TEST_ASSERT_EQUAL_INT(172, w.from);
  TEST_ASSERT_EQUAL_INT(179, w.to);
}

// --- DiffAxis ---

static DiffAxisConfig gdCfg(bool invert = false) {
  DiffAxisConfig c;
  c.minAngle = 1;
  c.maxAngle = 179;
  c.deadband = 80;
  c.stepDeg = 2;
  c.invert = invert;
  return c;
}

void test_diff_zone_morte_immobile() {
  DiffAxis ctl(gdCfg());
  DiffStep s = ctl.step(90, 1000, 1050);  // |ecart|=50 <= 80
  TEST_ASSERT_TRUE(s.balanced);
  TEST_ASSERT_TRUE(s.finished);
  TEST_ASSERT_EQUAL_INT(90, s.nextAngle);
}

void test_diff_pas_vers_ldr_la_plus_eclairee() {
  DiffAxis ctl(gdCfg());
  DiffStep s = ctl.step(90, 2000, 1000);  // A > B -> +angle
  TEST_ASSERT_FALSE(s.finished);
  TEST_ASSERT_EQUAL_INT(92, s.nextAngle);

  DiffAxis ctl2(gdCfg());
  s = ctl2.step(90, 1000, 2000);  // B > A -> -angle
  TEST_ASSERT_EQUAL_INT(88, s.nextAngle);
}

void test_diff_invert_inverse_le_sens() {
  DiffAxis ctl(gdCfg(true));
  DiffStep s = ctl.step(90, 2000, 1000);
  TEST_ASSERT_EQUAL_INT(88, s.nextAngle);
}

void test_diff_convergence_vers_equilibre() {
  // Modele : soleil a 100. ecart = (100 - angle) * 40 -> equilibre en ~100.
  DiffAxis ctl(gdCfg());
  int angle = 90;
  int iterations = 0;
  for (; iterations < 50; ++iterations) {
    const int error = (100 - angle) * 40;
    DiffStep s = ctl.step(angle, 1000 + error, 1000);
    angle = s.nextAngle;
    if (s.finished) {
      TEST_ASSERT_TRUE(s.balanced);
      break;
    }
  }
  // deadband 80 -> equilibre atteint a moins de 2 degres du soleil.
  TEST_ASSERT_INT_WITHIN(2, 100, angle);
  TEST_ASSERT_TRUE(iterations < 50);
}

void test_diff_renversement_stoppe_sans_osciller() {
  DiffAxis ctl(gdCfg());
  DiffStep s = ctl.step(90, 2000, 1000);  // -> 92 (dir +1)
  TEST_ASSERT_EQUAL_INT(92, s.nextAngle);
  s = ctl.step(92, 1000, 2000);  // equilibre depasse (dir -1) -> stop net
  TEST_ASSERT_TRUE(s.finished);
  TEST_ASSERT_FALSE(s.balanced);
  TEST_ASSERT_EQUAL_INT(92, s.nextAngle);
}

void test_diff_butee_stoppe() {
  DiffAxis ctl(gdCfg());
  DiffStep s = ctl.step(179, 2000, 1000);  // deja en butee haute
  TEST_ASSERT_TRUE(s.finished);
  TEST_ASSERT_FALSE(s.balanced);
  TEST_ASSERT_EQUAL_INT(179, s.nextAngle);
}

void test_diff_divergence_stoppe_apres_3_pas() {
  // L'ecart grossit a chaque pas dans le meme sens (sens de montage inverse).
  DiffAxis ctl(gdCfg());
  int angle = 90;
  DiffStep s = ctl.step(angle, 1200, 1000);  // mag=200, 1er pas
  angle = s.nextAngle;
  s = ctl.step(angle, 1400, 1000);  // mag=400 > 200, diverge #1
  TEST_ASSERT_FALSE(s.finished);
  angle = s.nextAngle;
  s = ctl.step(angle, 1600, 1000);  // diverge #2
  TEST_ASSERT_FALSE(s.finished);
  angle = s.nextAngle;
  s = ctl.step(angle, 1800, 1000);  // diverge #3 -> stop
  TEST_ASSERT_TRUE(s.finished);
  TEST_ASSERT_TRUE(s.diverged);
  TEST_ASSERT_EQUAL_INT(angle, s.nextAngle);  // pas de mouvement sur le stop
}

void test_diff_reset_oublie_la_direction() {
  DiffAxis ctl(gdCfg());
  (void)ctl.step(90, 2000, 1000);  // dir +1 memorisee
  ctl.reset();
  DiffStep s = ctl.step(92, 1000, 2000);  // dir -1 : pas un renversement
  TEST_ASSERT_FALSE(s.finished);
  TEST_ASSERT_EQUAL_INT(90, s.nextAngle);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_clampAngle);
  RUN_TEST(test_peak_vide_invalide);
  RUN_TEST(test_peak_garde_premiere_position_a_egalite);
  RUN_TEST(test_peak_reset);
  RUN_TEST(test_fuse_deux_pics_pondere_par_amplitude);
  RUN_TEST(test_fuse_amplitudes_egales_donne_le_milieu);
  RUN_TEST(test_fuse_un_seul_pic_valide_pas_de_division_par_deux);
  RUN_TEST(test_fuse_aucun_pic_retourne_fallback);
  RUN_TEST(test_fuse_resultat_borne);
  RUN_TEST(test_gain_neutre_passthrough);
  RUN_TEST(test_gain_amplifie_et_borne);
  RUN_TEST(test_equalization_nominal);
  RUN_TEST(test_equalization_ref_invalide_gain_neutre);
  RUN_TEST(test_equalization_gain_borne);
  RUN_TEST(test_equalization_tout_invalide);
  RUN_TEST(test_fineWindow_centre);
  RUN_TEST(test_fineWindow_bornee_aux_limites);
  RUN_TEST(test_diff_zone_morte_immobile);
  RUN_TEST(test_diff_pas_vers_ldr_la_plus_eclairee);
  RUN_TEST(test_diff_invert_inverse_le_sens);
  RUN_TEST(test_diff_convergence_vers_equilibre);
  RUN_TEST(test_diff_renversement_stoppe_sans_osciller);
  RUN_TEST(test_diff_butee_stoppe);
  RUN_TEST(test_diff_divergence_stoppe_apres_3_pas);
  RUN_TEST(test_diff_reset_oublie_la_direction);
  return UNITY_END();
}
