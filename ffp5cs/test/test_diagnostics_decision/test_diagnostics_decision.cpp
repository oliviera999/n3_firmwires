#include <cstdint>  // uint8_t/uint16_t/uint32_t AVANT tout en-tête (pitfall)
#include <unity.h>

// Tests de DiagnosticsDecision (diagnostics_decision.h), extrait de
// Diagnostics::update (diagnostics.cpp, audit §3.8). Deux logiques pures,
// 100 % string/integer, DIAGNOSTIC uniquement (aucun impact sécurité/autonomie) :
//   1. parseIdlePercent  : parse la sortie vTaskGetRunTimeStats() -> idle% moyen.
//   2. decideMinHeapSave : décide s'il faut persister minFreeHeap en NVS.
// On vérifie aussi la PARITÉ ligne-à-ligne via une transcription de la cascade
// inline d'origine (brute-force sur de nombreux jeux de valeurs).
#include "../../include/diagnostics_decision.h"

#include <cstring>
#include <cstdlib>  // atoi() de la libc, pour le contrôle de parité du parse
#include <cstdio>   // std::snprintf (génération de cas de parité)

using namespace DiagnosticsDecision;

void setUp(void) {}
void tearDown(void) {}

// =============================================================================
// Transcriptions de référence du code inline d'origine (diagnostics.cpp avant
// extraction) — sert d'oracle pour les boucles de parité.
// =============================================================================

// Réplique EXACTE du parse idle% d'origine (avec atoi() libc et isdigit ASCII).
static uint8_t refParseIdlePercent(const char* taskStats) {
  uint8_t idlePct = 255;
  const size_t taskStatsLen = strlen(taskStats);
  if (taskStatsLen > 0) {
    const char* taskStatsPtr = taskStats;
    uint16_t sumIdle = 0;
    uint8_t cnt = 0;
    const char* searchPtr = taskStatsPtr;
    while ((searchPtr = strstr(searchPtr, "IDLE")) != nullptr) {
      int idx = (int)(searchPtr - taskStatsPtr);
      const char* lineEndPtr = strchr(searchPtr, '\n');
      int lineEnd = (lineEndPtr != nullptr) ? (int)(lineEndPtr - taskStatsPtr) : (int)taskStatsLen;
      int lineLen = lineEnd - idx;
      if (lineLen > 0 && lineLen < 120) {
        char line[120];
        strncpy(line, taskStatsPtr + idx, (size_t)lineLen);
        line[lineLen] = '\0';
        int pctPos = -1;
        for (int i = lineLen - 1; i >= 0; i--) {
          if (line[i] == '%') { pctPos = i; break; }
        }
        if (pctPos > 0) {
          int start = pctPos - 1;
          while (start >= 0 && (line[start] >= '0' && line[start] <= '9')) start--;
          start++;
          int len = pctPos - start;
          if (len > 0 && len <= 3) {
            char numStr[4];
            strncpy(numStr, line + start, (size_t)len);
            numStr[len] = '\0';
            uint16_t val = (uint16_t)atoi(numStr);  // atoi libc d'origine
            sumIdle = (uint16_t)(sumIdle + val);
            cnt++;
          }
        }
      }
      searchPtr = (lineEndPtr != nullptr) ? lineEndPtr + 1 : taskStatsPtr + taskStatsLen;
    }
    if (cnt > 0) {
      uint16_t avg = (uint16_t)(sumIdle / cnt);
      idlePct = (avg > 100) ? 100 : (uint8_t)avg;
    }
  }
  return idlePct;
}

// Réplique EXACTE de la cascade de décision minHeap d'origine -> renvoie shouldSave.
static bool refShouldSaveMinHeap(uint32_t minFreeHeap, uint32_t lastSavedMinHeap,
                                 unsigned long now, unsigned long lastHeapSave,
                                 unsigned long interval, uint32_t diffForSave,
                                 uint32_t criticalLoss) {
  bool shouldSave = false;
  if (lastHeapSave == 0) {
    shouldSave = true;
  } else if (lastSavedMinHeap != UINT32_MAX &&
             (now - lastHeapSave > interval) &&
             (lastSavedMinHeap - minFreeHeap > diffForSave)) {
    shouldSave = true;
  } else if (lastSavedMinHeap != UINT32_MAX &&
             lastSavedMinHeap - minFreeHeap > criticalLoss) {
    shouldSave = true;
  }
  return shouldSave;
}

// =============================================================================
// 1. parseIdlePercent
// =============================================================================

void test_idle_null_and_empty_return_unknown(void) {
  TEST_ASSERT_EQUAL(255, parseIdlePercent(nullptr));   // garde nullptr
  TEST_ASSERT_EQUAL(255, parseIdlePercent(""));        // vide -> inconnu
}

void test_idle_no_idle_line_returns_unknown(void) {
  // Lignes sans "IDLE" -> aucune valeur -> 255.
  const char* s = "Task            Abs Time       % Time\nloopTask        12345          42%\n";
  TEST_ASSERT_EQUAL(255, parseIdlePercent(s));
}

void test_idle_single_row(void) {
  const char* s = "IDLE            999999         85%\n";
  TEST_ASSERT_EQUAL(85, parseIdlePercent(s));
}

void test_idle_single_row_no_trailing_newline(void) {
  // Dernière ligne sans '\n' : lineEnd = fin de chaîne (parité).
  const char* s = "IDLE            999999         77%";
  TEST_ASSERT_EQUAL(77, parseIdlePercent(s));
}

void test_idle_two_rows_averaged(void) {
  // IDLE0=80%, IDLE1=90% -> moyenne entière (80+90)/2 = 85.
  const char* s = "IDLE0           500000         80%\nIDLE1           600000         90%\n";
  TEST_ASSERT_EQUAL(85, parseIdlePercent(s));
}

void test_idle_average_integer_truncation(void) {
  // 80% et 91% -> (80+91)/2 = 171/2 = 85 (troncature entière, pas 85.5).
  const char* s = "IDLE0           1              80%\nIDLE1           2              91%\n";
  TEST_ASSERT_EQUAL(85, parseIdlePercent(s));
}

void test_idle_zero_percent(void) {
  const char* s = "IDLE            123            0%\n";
  TEST_ASSERT_EQUAL(0, parseIdlePercent(s));
}

void test_idle_hundred_percent(void) {
  const char* s = "IDLE            123            100%\n";
  TEST_ASSERT_EQUAL(100, parseIdlePercent(s));
}

void test_idle_capped_at_100(void) {
  // Une seule ligne "IDLE" à 150% : len=3 (<=3) -> val=150, avg=150 -> plafonné 100.
  const char* s = "IDLE            1              150%\n";
  TEST_ASSERT_EQUAL(100, parseIdlePercent(s));
}

void test_idle_line_with_no_percent_sign_skipped(void) {
  // Ligne "IDLE" sans '%' -> ignorée -> aucune valeur -> 255.
  const char* s = "IDLE            999999         85\n";
  TEST_ASSERT_EQUAL(255, parseIdlePercent(s));
}

void test_idle_percent_with_no_preceding_digit_skipped(void) {
  // '%' précédé d'un non-chiffre (espace) -> len==0 -> ligne ignorée -> 255.
  const char* s = "IDLE             %\n";
  TEST_ASSERT_EQUAL(255, parseIdlePercent(s));
}

void test_idle_four_digit_number_skipped(void) {
  // Nombre de 4 chiffres avant '%' -> len=4 (>3) -> ignoré -> 255 (parité).
  const char* s = "IDLE            1              1234%\n";
  TEST_ASSERT_EQUAL(255, parseIdlePercent(s));
}

void test_idle_percent_at_position_zero_skipped(void) {
  // '%' en tout début de ligne IDLE impossible (commence par "IDLE"), mais on
  // valide le garde pctPos>0 via une ligne où le seul '%' est juste après IDLE.
  // "IDLE%..." -> pctPos pointe sur le '%' à l'index 4 (>0), start remonte sur
  // les chiffres (aucun) -> len==0 -> ignoré.
  const char* s = "IDLE%           1              \n";
  TEST_ASSERT_EQUAL(255, parseIdlePercent(s));
}

void test_idle_mixed_idle_and_other_tasks(void) {
  // Tâches mélangées : seules les lignes "IDLE" comptent. IDLE0=49, IDLE1=51 -> 50.
  const char* s =
      "Task            Abs             %\n"
      "loopTask        100000          30%\n"
      "IDLE0           400000          49%\n"
      "wifiTask        50000           5%\n"
      "IDLE1           420000          51%\n";
  TEST_ASSERT_EQUAL(50, parseIdlePercent(s));
}

void test_idle_takes_last_percent_on_line(void) {
  // Plusieurs '%' sur la ligne IDLE : on prend le DERNIER (recherche depuis la fin).
  // "... 12% ... 88%" -> doit lire 88, pas 12.
  const char* s = "IDLE   12%   foo   88%\n";
  TEST_ASSERT_EQUAL(88, parseIdlePercent(s));
}

// Parité brute-force du parse : compare parseIdlePercent à la transcription
// d'origine (atoi libc) sur un éventail de cas générés.
void test_idle_parity_bruteforce(void) {
  static const char* cases[] = {
    "",
    "IDLE",
    "IDLE\n",
    "IDLE 0%\n",
    "IDLE 100%\n",
    "IDLE 255%\n",
    "IDLE0 80%\nIDLE1 90%\n",
    "IDLE no percent here\n",
    "IDLE %\n",
    "IDLE 1234%\n",
    "loopTask 42%\nIDLE0 33%\nIDLE1 67%\n",
    "IDLE   12%   foo   88%\n",
    "IDLE%   \n",
    "noidlehere\n",
    "IDLE 5%\nIDLE 6%\nIDLE 7%\n",
  };
  const int n = (int)(sizeof(cases) / sizeof(cases[0]));
  for (int i = 0; i < n; ++i) {
    TEST_ASSERT_EQUAL(refParseIdlePercent(cases[i]), parseIdlePercent(cases[i]));
  }
  // Génère aussi des lignes "IDLE <v>%" pour v = 0..300 et compare.
  for (int v = 0; v <= 300; ++v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "IDLE   123   %d%%\n", v);
    TEST_ASSERT_EQUAL(refParseIdlePercent(buf), parseIdlePercent(buf));
  }
}

// =============================================================================
// 2. decideMinHeapSave
// =============================================================================

static const unsigned long INTERVAL = DEFAULT_MIN_HEAP_SAVE_INTERVAL; // 600000
static const uint32_t DIFF = DEFAULT_MIN_HEAP_DIFF_FOR_SAVE;          // 1024
static const uint32_t CRIT = DEFAULT_MIN_HEAP_CRITICAL_LOSS;          // 10240

static MinHeapSaveReason decide(uint32_t minFree, uint32_t lastSaved,
                                unsigned long now, unsigned long lastSave) {
  return decideMinHeapSave(minFree, lastSaved, now, lastSave, INTERVAL, DIFF, CRIT);
}

void test_save_first_when_lasthaepsave_zero(void) {
  // lastHeapSave == 0 -> première sauvegarde, quel que soit le reste.
  TEST_ASSERT_EQUAL((int)MinHeapSaveReason::FIRST_SAVE,
                    (int)decide(50000, UINT32_MAX, 1000, 0));
  TEST_ASSERT_EQUAL((int)MinHeapSaveReason::FIRST_SAVE,
                    (int)decide(50000, 60000, 9999999, 0));
}

void test_uint32max_guard_blocks_interval_and_critical(void) {
  // Premier boot : lastSavedMinHeap == UINT32_MAX neutralise branches 2 & 3.
  // Intervalle écoulé, perte énorme, mais lastHeapSave != 0 -> NO_SAVE.
  TEST_ASSERT_EQUAL((int)MinHeapSaveReason::NO_SAVE,
                    (int)decide(1000, UINT32_MAX, 10000000, 1));
}

void test_interval_delta_save(void) {
  // Intervalle dépassé ET diff > 1024 -> INTERVAL_DELTA.
  // lastSaved=50000, minFree=48000 -> diff=2000 (>1024) ; now-lastSave=600001 (>600000).
  TEST_ASSERT_EQUAL((int)MinHeapSaveReason::INTERVAL_DELTA,
                    (int)decide(48000, 50000, 700001, 100000 /*delta=600001*/));
}

void test_interval_not_elapsed_no_save(void) {
  // lastHeapSave != 0 (sinon branche 1). Diff suffisante mais intervalle PAS
  // dépassé (delta == interval, pas '>') -> branche 2 KO ; perte 2000 < 10240
  // -> branche 3 KO -> NO_SAVE. lastSave=1, now=600001 -> delta=600000.
  TEST_ASSERT_EQUAL((int)MinHeapSaveReason::NO_SAVE,
                    (int)decide(48000, 50000, 600001, 1 /*delta=600000, pas >*/));
}

void test_interval_elapsed_but_diff_too_small_no_save(void) {
  // lastHeapSave != 0. Intervalle dépassé mais diff == 1024 (pas '>') -> branche 2
  // KO ; 1024 < 10240 -> branche 3 KO -> NO_SAVE.
  // delta = 700002-1 = 700001 (>600000) ; diff = 50000-48976 = 1024 exact.
  TEST_ASSERT_EQUAL((int)MinHeapSaveReason::NO_SAVE,
                    (int)decide(48976, 50000, 700002, 1)); // diff = 1024 exact
}

void test_diff_just_above_threshold_with_interval(void) {
  // lastHeapSave != 0. diff = 1025 (>1024) ET intervalle dépassé -> INTERVAL_DELTA.
  // delta = 700002-1 = 700001 (>600000) ; diff = 50000-48975 = 1025.
  TEST_ASSERT_EQUAL((int)MinHeapSaveReason::INTERVAL_DELTA,
                    (int)decide(48975, 50000, 700002, 1)); // diff = 1025
}

void test_critical_loss_save_even_if_interval_not_elapsed(void) {
  // Intervalle PAS dépassé (branche 2 KO) mais perte > 10240 -> CRITICAL_LOSS.
  // lastSaved=50000, minFree=39000 -> perte=11000 (>10240) ; now-lastSave=10 (<interval).
  TEST_ASSERT_EQUAL((int)MinHeapSaveReason::CRITICAL_LOSS,
                    (int)decide(39000, 50000, 1010, 1000));
}

void test_critical_loss_boundary(void) {
  // Perte == 10240 exact (pas '>') -> pas critique ; intervalle non dépassé -> NO_SAVE.
  TEST_ASSERT_EQUAL((int)MinHeapSaveReason::NO_SAVE,
                    (int)decide(40000 - 240, 50000, 1010, 1000)); // perte = 10240
  // Perte == 10241 (>10240) -> CRITICAL_LOSS.
  TEST_ASSERT_EQUAL((int)MinHeapSaveReason::CRITICAL_LOSS,
                    (int)decide(50000 - 10241, 50000, 1010, 1000));
}

void test_interval_branch_takes_precedence_over_critical(void) {
  // Si branche 2 (interval+delta) ET branche 3 (perte) seraient vraies, la
  // cascade choisit la branche 2 d'abord -> INTERVAL_DELTA (parité de l'ordre).
  // lastHeapSave != 0. perte=20000 (>10240) ET intervalle dépassé
  // (delta=700001>600000) ET diff>1024 -> branche 2 d'abord.
  TEST_ASSERT_EQUAL((int)MinHeapSaveReason::INTERVAL_DELTA,
                    (int)decide(30000, 50000, 700002, 1));
}

// Parité brute-force de la décision : compare le booléen "shouldSave" dérivé de
// decideMinHeapSave à la transcription de la cascade d'origine, sur une grille.
void test_save_parity_bruteforce(void) {
  const uint32_t lastSavedVals[] = {UINT32_MAX, 0, 1024, 50000, 60000, 100000};
  const uint32_t minFreeVals[]   = {0, 1000, 39000, 48000, 48975, 48976, 49999, 50000, 60000};
  const unsigned long lastSaveVals[] = {0, 1, 1000, 100000};
  const unsigned long nowVals[]      = {0, 1010, 600000, 700000, 10000000};
  for (uint32_t ls : lastSavedVals)
    for (uint32_t mf : minFreeVals)
      for (unsigned long lhs : lastSaveVals)
        for (unsigned long nw : nowVals) {
          MinHeapSaveReason r = decideMinHeapSave(mf, ls, nw, lhs, INTERVAL, DIFF, CRIT);
          bool got = (r != MinHeapSaveReason::NO_SAVE);
          bool ref = refShouldSaveMinHeap(mf, ls, nw, lhs, INTERVAL, DIFF, CRIT);
          TEST_ASSERT_EQUAL(ref, got);
        }
}

// Parité avec des seuils NON-défaut (vérifie que les seuils sont bien des params).
void test_save_parity_custom_thresholds(void) {
  const unsigned long itv = 1000;
  const uint32_t dff = 100;
  const uint32_t crt = 500;
  const uint32_t lastSavedVals[] = {UINT32_MAX, 2000, 5000};
  const uint32_t minFreeVals[]   = {1000, 1900, 4000, 4900, 5000};
  const unsigned long lastSaveVals[] = {0, 1, 500};
  const unsigned long nowVals[]      = {0, 600, 1500, 100000};
  for (uint32_t ls : lastSavedVals)
    for (uint32_t mf : minFreeVals)
      for (unsigned long lhs : lastSaveVals)
        for (unsigned long nw : nowVals) {
          MinHeapSaveReason r = decideMinHeapSave(mf, ls, nw, lhs, itv, dff, crt);
          bool got = (r != MinHeapSaveReason::NO_SAVE);
          bool ref = refShouldSaveMinHeap(mf, ls, nw, lhs, itv, dff, crt);
          TEST_ASSERT_EQUAL(ref, got);
        }
}

// Sanity : les constantes par défaut du header valent bien celles d'origine.
void test_default_constants(void) {
  TEST_ASSERT_EQUAL(255, IDLE_UNKNOWN);
  TEST_ASSERT_EQUAL(600000UL, DEFAULT_MIN_HEAP_SAVE_INTERVAL);
  TEST_ASSERT_EQUAL(1024, DEFAULT_MIN_HEAP_DIFF_FOR_SAVE);
  TEST_ASSERT_EQUAL(10240, DEFAULT_MIN_HEAP_CRITICAL_LOSS);
}

int main(void) {
  UNITY_BEGIN();
  // parseIdlePercent
  RUN_TEST(test_idle_null_and_empty_return_unknown);
  RUN_TEST(test_idle_no_idle_line_returns_unknown);
  RUN_TEST(test_idle_single_row);
  RUN_TEST(test_idle_single_row_no_trailing_newline);
  RUN_TEST(test_idle_two_rows_averaged);
  RUN_TEST(test_idle_average_integer_truncation);
  RUN_TEST(test_idle_zero_percent);
  RUN_TEST(test_idle_hundred_percent);
  RUN_TEST(test_idle_capped_at_100);
  RUN_TEST(test_idle_line_with_no_percent_sign_skipped);
  RUN_TEST(test_idle_percent_with_no_preceding_digit_skipped);
  RUN_TEST(test_idle_four_digit_number_skipped);
  RUN_TEST(test_idle_percent_at_position_zero_skipped);
  RUN_TEST(test_idle_mixed_idle_and_other_tasks);
  RUN_TEST(test_idle_takes_last_percent_on_line);
  RUN_TEST(test_idle_parity_bruteforce);
  // decideMinHeapSave
  RUN_TEST(test_save_first_when_lasthaepsave_zero);
  RUN_TEST(test_uint32max_guard_blocks_interval_and_critical);
  RUN_TEST(test_interval_delta_save);
  RUN_TEST(test_interval_not_elapsed_no_save);
  RUN_TEST(test_interval_elapsed_but_diff_too_small_no_save);
  RUN_TEST(test_diff_just_above_threshold_with_interval);
  RUN_TEST(test_critical_loss_save_even_if_interval_not_elapsed);
  RUN_TEST(test_critical_loss_boundary);
  RUN_TEST(test_interval_branch_takes_precedence_over_critical);
  RUN_TEST(test_save_parity_bruteforce);
  RUN_TEST(test_save_parity_custom_thresholds);
  RUN_TEST(test_default_constants);
  return UNITY_END();
}
