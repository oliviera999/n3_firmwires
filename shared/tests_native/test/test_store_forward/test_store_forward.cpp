#include <unity.h>
#include <cstdint>

// Tests de n3SfDrain (n3_store_forward) — orchestrateur de drain d'une file
// offline durable, généralisé depuis uploadphotosserver/camera_sync.cpp.
// Ces invariants n'étaient couverts par AUCUN test auparavant :
//   peek->commit jamais burn, stratégie hybride, plafond absolu, pacing,
//   budget temps (report ≠ échec), retries 429, arrêt sur erreur réseau,
//   skip-commit sur HardFail.
// Temps et attente injectés (mockNow/mockSleep) -> déterministe en natif.

#include "n3_store_forward.cpp"  // impl (pur, aucune dépendance Arduino)

// ---------------------------------------------------------------------------
// Backend mock RAM : N items, enregistre les commits.
// ---------------------------------------------------------------------------
struct MockBackend : public N3SfBackend {
  uint32_t n = 0;                 // items en attente (handles 1..n)
  uint32_t commits[64] = {};      // handles committés (dans l'ordre)
  uint32_t commitCount = 0;

  uint32_t count() override { return n; }
  bool peek(uint32_t index, N3SfItem& out) override {
    if (index >= n) return false;
    out = {};
    out.handle = index + 1;  // handle stable 1..n
    return true;
  }
  void commit(const N3SfItem& item) override {
    if (commitCount < 64) commits[commitCount] = item.handle;
    commitCount++;
  }
};

// ---------------------------------------------------------------------------
// Backend mock à index GLISSANTS : commit() RETIRE réellement l'élément, comme
// le ferait une file NVS indexée (usage envisagé par l'en-tête pour ffp5cs).
// C'est le cas qui n'était couvert par aucun test — MockBackend ci-dessus
// n'enlève jamais rien (audit 2026-07, F6).
// ---------------------------------------------------------------------------
struct RemovingBackend : public N3SfBackend {
  uint32_t queue[64] = {};        // handles en attente, dans l'ordre
  uint32_t queueLen = 0;
  uint32_t commits[64] = {};      // handles committés (dans l'ordre)
  uint32_t commitCount = 0;

  void fill(uint32_t first, uint32_t howMany) {
    queueLen = 0;
    for (uint32_t k = 0; k < howMany && queueLen < 64; ++k) queue[queueLen++] = first + k;
  }

  uint32_t count() override { return queueLen; }
  bool peek(uint32_t index, N3SfItem& out) override {
    if (index >= queueLen) return false;
    out = {};
    out.handle = queue[index];
    return true;
  }
  void commit(const N3SfItem& item) override {
    if (commitCount < 64) commits[commitCount] = item.handle;
    commitCount++;
    for (uint32_t k = 0; k < queueLen; ++k) {
      if (queue[k] == item.handle) {
        for (uint32_t j = k; j + 1 < queueLen; ++j) queue[j] = queue[j + 1];
        queueLen--;
        return;
      }
    }
  }
};

// ---------------------------------------------------------------------------
// Envoi scripté : séquence de verdicts consommée appel par appel.
// ---------------------------------------------------------------------------
struct ScriptedSend {
  N3SfSend script[64];
  uint32_t scriptLen = 0;
  uint32_t calls = 0;
};
static N3SfSend scriptedSend(const N3SfItem& /*item*/, void* ctx) {
  ScriptedSend* s = static_cast<ScriptedSend*>(ctx);
  N3SfSend v = (s->calls < s->scriptLen) ? s->script[s->calls] : N3SfSend::Ok;
  s->calls++;
  return v;
}

// ---------------------------------------------------------------------------
// Horloge/attente mock : le temps avance de `g_tickPerSend` à chaque nowMs(),
// sleepMs enregistre les durées demandées.
// ---------------------------------------------------------------------------
static uint32_t g_now = 0;
static uint32_t g_nowStep = 0;      // incrément par appel à nowMs()
static uint32_t g_sleeps[64];
static uint32_t g_sleepCount = 0;
static uint32_t mockNow() { uint32_t v = g_now; g_now += g_nowStep; return v; }
static void mockSleep(uint32_t ms) {
  if (g_sleepCount < 64) g_sleeps[g_sleepCount] = ms;
  g_sleepCount++;
  g_now += ms;  // dormir fait avancer l'horloge
}

static N3SfDrainConfig baseCfg() {
  N3SfDrainConfig c = {};
  c.maxItemsPerWake = 3;
  c.fullDrainThreshold = 25;
  c.hardCapPerWake = 16;
  c.minIntervalMs = 0;   // pacing off par défaut (tests dédiés l'activent)
  c.maxDurationMs = 0;   // budget off par défaut
  c.rateLimitRetries = 2;
  c.nowMs = &mockNow;
  c.sleepMs = &mockSleep;
  return c;
}

void setUp(void) {
  g_now = 0;
  g_nowStep = 0;
  g_sleepCount = 0;
}
void tearDown(void) {}

// ---- File vide / planned hybride ----

void test_file_vide_complete_sans_envoi(void) {
  MockBackend b;  // n=0
  ScriptedSend s;
  N3SfDrainResult r = n3SfDrain(b, baseCfg(), &scriptedSend, &s);
  TEST_ASSERT_EQUAL_UINT32(0, r.pending);
  TEST_ASSERT_TRUE(r.complete);
  TEST_ASSERT_EQUAL_UINT32(0, s.calls);
  TEST_ASSERT_EQUAL_UINT32(0, b.commitCount);
}

void test_drain_incremental_borne_a_maxItems(void) {
  MockBackend b; b.n = 5;                    // 5 <= seuil 25 -> incrémental
  ScriptedSend s;                            // tout Ok
  N3SfDrainResult r = n3SfDrain(b, baseCfg(), &scriptedSend, &s);
  TEST_ASSERT_EQUAL_UINT32(5, r.pending);
  TEST_ASSERT_EQUAL_UINT32(3, r.planned);    // maxItemsPerWake=3
  TEST_ASSERT_EQUAL_UINT32(3, r.sent);
  TEST_ASSERT_EQUAL_UINT32(0, r.failed);
  TEST_ASSERT_TRUE(r.complete);              // report ≠ échec
  TEST_ASSERT_EQUAL_UINT32(3, b.commitCount);
  TEST_ASSERT_EQUAL_UINT32(1, b.commits[0]); // ordre FIFO
  TEST_ASSERT_EQUAL_UINT32(3, b.commits[2]);
}

void test_vidage_complet_au_dela_du_seuil_cape_par_hardcap(void) {
  MockBackend b; b.n = 30;                   // 30 > seuil 25 -> vidage complet…
  ScriptedSend s;
  N3SfDrainResult r = n3SfDrain(b, baseCfg(), &scriptedSend, &s);
  TEST_ASSERT_EQUAL_UINT32(16, r.planned);   // …capé au plafond absolu 16
  TEST_ASSERT_EQUAL_UINT32(16, r.sent);
}

// ---- peek->commit jamais burn ----

void test_echec_reseau_arrete_sans_bruler_l_element(void) {
  MockBackend b; b.n = 3;
  ScriptedSend s;
  s.script[0] = N3SfSend::Ok;
  s.script[1] = N3SfSend::NetworkError;      // échec au 2e
  s.scriptLen = 2;
  N3SfDrainResult r = n3SfDrain(b, baseCfg(), &scriptedSend, &s);
  TEST_ASSERT_EQUAL_UINT32(1, r.sent);
  TEST_ASSERT_EQUAL_UINT32(1, r.failed);
  TEST_ASSERT_FALSE(r.complete);
  TEST_ASSERT_EQUAL_UINT32(1, b.commitCount);   // SEUL le 1er est committé
  TEST_ASSERT_EQUAL_UINT32(1, b.commits[0]);    // l'item 2 reste en file
  TEST_ASSERT_EQUAL_UINT32(2, s.calls);          // le 3e n'est jamais tenté
}

// ---- Rate-limit 429 ----

void test_429_puis_ok_au_retry_avec_pause(void) {
  MockBackend b; b.n = 1;
  N3SfDrainConfig c = baseCfg();
  c.minIntervalMs = 11000;
  ScriptedSend s;
  s.script[0] = N3SfSend::RateLimited;
  s.script[1] = N3SfSend::Ok;                // retry réussit
  s.scriptLen = 2;
  N3SfDrainResult r = n3SfDrain(b, c, &scriptedSend, &s);
  TEST_ASSERT_EQUAL_UINT32(1, r.sent);
  TEST_ASSERT_TRUE(r.complete);
  TEST_ASSERT_EQUAL_UINT32(1, g_sleepCount);      // une pause pleine entre les essais
  TEST_ASSERT_EQUAL_UINT32(11000, g_sleeps[0]);
  TEST_ASSERT_EQUAL_UINT32(1, b.commitCount);
}

void test_429_epuise_arrete_le_drain(void) {
  MockBackend b; b.n = 2;
  N3SfDrainConfig c = baseCfg();             // rateLimitRetries=2
  c.minIntervalMs = 100;
  ScriptedSend s;
  s.script[0] = N3SfSend::RateLimited;
  s.script[1] = N3SfSend::RateLimited;
  s.script[2] = N3SfSend::RateLimited;       // 1 essai + 2 retries, tous 429
  s.scriptLen = 3;
  N3SfDrainResult r = n3SfDrain(b, c, &scriptedSend, &s);
  TEST_ASSERT_EQUAL_UINT32(0, r.sent);
  TEST_ASSERT_EQUAL_UINT32(1, r.failed);
  TEST_ASSERT_FALSE(r.complete);
  TEST_ASSERT_EQUAL_UINT32(0, b.commitCount);   // rien de brûlé
  TEST_ASSERT_EQUAL_UINT32(3, s.calls);          // 1+2 retries, puis stop
}

// ---- HardFail : skip sans bloquer la file ----

void test_hardfail_committe_et_continue(void) {
  MockBackend b; b.n = 3;
  ScriptedSend s;
  s.script[0] = N3SfSend::Ok;
  s.script[1] = N3SfSend::HardFail;          // rejet définitif du 2e
  s.script[2] = N3SfSend::Ok;
  s.scriptLen = 3;
  N3SfDrainResult r = n3SfDrain(b, baseCfg(), &scriptedSend, &s);
  TEST_ASSERT_EQUAL_UINT32(2, r.sent);
  TEST_ASSERT_EQUAL_UINT32(1, r.failed);
  TEST_ASSERT_FALSE(r.complete);
  TEST_ASSERT_EQUAL_UINT32(3, b.commitCount);   // 2 envoyés + 1 sauté = 3 commits
  TEST_ASSERT_EQUAL_UINT32(2, b.commits[1]);    // le HardFail est bien consommé
}

// ---- Pacing : attente du COMPLÉMENT uniquement ----

void test_pacing_attend_le_complement(void) {
  MockBackend b; b.n = 2;
  N3SfDrainConfig c = baseCfg();
  c.minIntervalMs = 1000;
  g_nowStep = 300;  // chaque lecture d'horloge avance de 300 ms (simule l'envoi)
  ScriptedSend s;   // tout Ok
  N3SfDrainResult r = n3SfDrain(b, c, &scriptedSend, &s);
  TEST_ASSERT_EQUAL_UINT32(2, r.sent);
  // Entre l'horodatage post-envoi n°1 et le pacing pré-envoi n°2, l'horloge
  // n'a avancé que de g_nowStep par lecture : il reste un complément < 1000 ms
  // -> exactement UNE attente, strictement inférieure à l'intervalle plein.
  TEST_ASSERT_EQUAL_UINT32(1, g_sleepCount);
  TEST_ASSERT_TRUE(g_sleeps[0] > 0);
  TEST_ASSERT_TRUE(g_sleeps[0] < 1000);
}

// ---- Budget temps : report propre, pas un échec ----

void test_budget_temps_reporte_sans_echec(void) {
  MockBackend b; b.n = 10;
  N3SfDrainConfig c = baseCfg();
  c.maxItemsPerWake = 10;
  c.maxDurationMs = 500;
  g_nowStep = 300;  // chaque lecture avance de 300 ms -> budget dépassé vite
  ScriptedSend s;
  N3SfDrainResult r = n3SfDrain(b, c, &scriptedSend, &s);
  TEST_ASSERT_TRUE(r.sent >= 1);              // au moins le 1er passe (i>0 requis)
  TEST_ASSERT_TRUE(r.sent < 10);              // le budget a arrêté le drain
  TEST_ASSERT_EQUAL_UINT32(0, r.failed);
  TEST_ASSERT_TRUE(r.complete);               // report ≠ échec (A1)
  TEST_ASSERT_EQUAL_UINT32(r.sent, b.commitCount);
}

// ---- Sans horloge injectée : pacing/budget désactivés, drain fonctionnel ----

void test_sans_horloge_draine_quand_meme(void) {
  MockBackend b; b.n = 2;
  N3SfDrainConfig c = baseCfg();
  c.nowMs = nullptr;
  c.minIntervalMs = 1000;
  c.maxDurationMs = 1;
  ScriptedSend s;
  N3SfDrainResult r = n3SfDrain(b, c, &scriptedSend, &s);
  TEST_ASSERT_EQUAL_UINT32(2, r.sent);        // ni pacing ni budget appliqués
  TEST_ASSERT_EQUAL_UINT32(0, g_sleepCount);
}

// ---- Sémantique d'index : backend qui RETIRE l'élément au commit (F6) ----

/**
 * Avant correctif, l'orchestrateur lisait `peek(i)` avec `i` = compteur
 * d'itérations. Sur un backend qui retire l'élément, chaque commit décale les
 * index d'un cran : il drainait un élément sur DEUX (10, 12, 14) et laissait
 * 11, 13, 15 en file. Le curseur est désormais observé après chaque commit.
 */
void test_backend_glissant_draine_tous_les_elements_dans_l_ordre(void) {
  RemovingBackend b;
  b.fill(10, 6);  // 10..15
  N3SfDrainConfig c = baseCfg();
  c.maxItemsPerWake = 6;
  ScriptedSend s;  // tout Ok
  N3SfDrainResult r = n3SfDrain(b, c, &scriptedSend, &s);

  TEST_ASSERT_EQUAL_UINT32(6, r.sent);
  TEST_ASSERT_EQUAL_UINT32(0, r.failed);
  TEST_ASSERT_TRUE(r.complete);
  TEST_ASSERT_EQUAL_UINT32(6, b.commitCount);
  for (uint32_t k = 0; k < 6; ++k) {
    TEST_ASSERT_EQUAL_UINT32(10 + k, b.commits[k]);  // ordre strict, aucun saut
  }
  TEST_ASSERT_EQUAL_UINT32(0, b.queueLen);  // file entierement vidée
}

/** Le commit de skip (HardFail) décale aussi les index : même exigence. */
void test_backend_glissant_hardfail_ne_desynchronise_pas_le_curseur(void) {
  RemovingBackend b;
  b.fill(10, 4);  // 10..13
  N3SfDrainConfig c = baseCfg();
  c.maxItemsPerWake = 4;
  ScriptedSend s;
  s.script[0] = N3SfSend::Ok;
  s.script[1] = N3SfSend::HardFail;
  s.script[2] = N3SfSend::Ok;
  s.script[3] = N3SfSend::Ok;
  s.scriptLen = 4;
  N3SfDrainResult r = n3SfDrain(b, c, &scriptedSend, &s);

  TEST_ASSERT_EQUAL_UINT32(3, r.sent);
  TEST_ASSERT_EQUAL_UINT32(1, r.failed);
  TEST_ASSERT_EQUAL_UINT32(4, b.commitCount);
  for (uint32_t k = 0; k < 4; ++k) {
    TEST_ASSERT_EQUAL_UINT32(10 + k, b.commits[k]);
  }
  TEST_ASSERT_EQUAL_UINT32(0, b.queueLen);
}

/** Invariant peek->commit-jamais-burn : l'élément non acquitté reste en tête. */
void test_backend_glissant_echec_reseau_laisse_l_element_en_tete(void) {
  RemovingBackend b;
  b.fill(10, 4);  // 10..13
  N3SfDrainConfig c = baseCfg();
  c.maxItemsPerWake = 4;
  ScriptedSend s;
  s.script[0] = N3SfSend::Ok;
  s.script[1] = N3SfSend::NetworkError;
  s.scriptLen = 2;
  N3SfDrainResult r = n3SfDrain(b, c, &scriptedSend, &s);

  TEST_ASSERT_EQUAL_UINT32(1, r.sent);
  TEST_ASSERT_EQUAL_UINT32(1, r.failed);
  TEST_ASSERT_EQUAL_UINT32(1, b.commitCount);
  TEST_ASSERT_EQUAL_UINT32(10, b.commits[0]);
  TEST_ASSERT_EQUAL_UINT32(3, b.queueLen);   // 11, 12, 13 conservés
  TEST_ASSERT_EQUAL_UINT32(11, b.queue[0]);  // le refusé n'est pas brûlé
}

/** Les index stables (SdCursorBackend) restent traités comme avant : +1. */
void test_backend_stable_avance_toujours_le_curseur(void) {
  MockBackend b;
  b.n = 4;
  N3SfDrainConfig c = baseCfg();
  c.maxItemsPerWake = 4;
  ScriptedSend s;
  N3SfDrainResult r = n3SfDrain(b, c, &scriptedSend, &s);

  TEST_ASSERT_EQUAL_UINT32(4, r.sent);
  TEST_ASSERT_EQUAL_UINT32(4, b.commitCount);
  for (uint32_t k = 0; k < 4; ++k) {
    TEST_ASSERT_EQUAL_UINT32(k + 1, b.commits[k]);  // 1,2,3,4 — aucun doublon
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_file_vide_complete_sans_envoi);
  RUN_TEST(test_drain_incremental_borne_a_maxItems);
  RUN_TEST(test_vidage_complet_au_dela_du_seuil_cape_par_hardcap);
  RUN_TEST(test_echec_reseau_arrete_sans_bruler_l_element);
  RUN_TEST(test_429_puis_ok_au_retry_avec_pause);
  RUN_TEST(test_429_epuise_arrete_le_drain);
  RUN_TEST(test_hardfail_committe_et_continue);
  RUN_TEST(test_pacing_attend_le_complement);
  RUN_TEST(test_budget_temps_reporte_sans_echec);
  RUN_TEST(test_sans_horloge_draine_quand_meme);
  RUN_TEST(test_backend_glissant_draine_tous_les_elements_dans_l_ordre);
  RUN_TEST(test_backend_glissant_hardfail_ne_desynchronise_pas_le_curseur);
  RUN_TEST(test_backend_glissant_echec_reseau_laisse_l_element_en_tete);
  RUN_TEST(test_backend_stable_avance_toujours_le_curseur);
  return UNITY_END();
}
