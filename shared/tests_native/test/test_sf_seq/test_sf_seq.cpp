#include <unity.h>
#include <cstdint>

// Tests natifs de shared/n3_store_forward/src/n3_sf_seq.h (logique pure de
// numérotation compteur + curseur de la file SD d'uploadphotosserver).
// Scénarios terrain 2026-10 : ESP32-CAM n3pp rechargée avec une carte SD déjà
// utilisée et/ou une NVS vierge -> aucune photo réellement envoyée.
#include "n3_sf_seq.h"

void setUp(void) {}
void tearDown(void) {}

// ---------- n3SfSeqNext / n3SfSeqPending / n3SfSeqIsPending ----------

void test_next_au_dessus_du_compteur_cas_nominal() {
  TEST_ASSERT_EQUAL_UINT32(1, n3SfSeqNext(0, 0));
  TEST_ASSERT_EQUAL_UINT32(101, n3SfSeqNext(100, 95));
}

void test_next_au_dessus_du_curseur_si_curseur_en_avance() {
  // Avant 2.77 : next = count+1 = 2 <= cursor 16 -> photo née « acquittée », jamais envoyée.
  const uint32_t next = n3SfSeqNext(1, 16);
  TEST_ASSERT_EQUAL_UINT32(17, next);
  // Une fois engagée (count = next), elle est bien en attente.
  TEST_ASSERT_EQUAL_UINT32(1, n3SfSeqPending(next, 16));
  TEST_ASSERT_TRUE(n3SfSeqIsPending(next, next, 16));
}

void test_pending_nul_si_curseur_au_dela() {
  TEST_ASSERT_EQUAL_UINT32(0, n3SfSeqPending(1, 16));
  TEST_ASSERT_EQUAL_UINT32(0, n3SfSeqPending(5, 5));
  TEST_ASSERT_EQUAL_UINT32(5, n3SfSeqPending(100, 95));
}

void test_is_pending_borne_par_le_compteur() {
  // ]cursor, count] = ]95, 100]
  TEST_ASSERT_FALSE(n3SfSeqIsPending(95, 100, 95));
  TEST_ASSERT_TRUE(n3SfSeqIsPending(96, 100, 95));
  TEST_ASSERT_TRUE(n3SfSeqIsPending(100, 100, 95));
  // Fichier d'une autre vie de la carte (n > count) : PAS en attente (avant : envoyé).
  TEST_ASSERT_FALSE(n3SfSeqIsPending(500, 100, 95));
}

// ---------- n3SfSeqReconcile ----------

void test_reconcile_support_vide_ou_coherent_ne_change_rien() {
  N3SfSeqReconcile r = n3SfSeqReconcile(0, 0, 0);
  TEST_ASSERT_EQUAL(static_cast<int>(N3SfSeqReconcileAction::None), static_cast<int>(r.action));
  r = n3SfSeqReconcile(100, 95, 100);
  TEST_ASSERT_EQUAL(static_cast<int>(N3SfSeqReconcileAction::None), static_cast<int>(r.action));
  TEST_ASSERT_EQUAL_UINT32(100, r.count);
  TEST_ASSERT_EQUAL_UINT32(95, r.cursor);
}

void test_reconcile_nvs_vierge_carte_reutilisee_adopte_sans_envoyer() {
  // NVS vierge, carte avec picture1..picture500 d'une ancienne caméra.
  const N3SfSeqReconcile r = n3SfSeqReconcile(0, 0, 500);
  TEST_ASSERT_EQUAL(static_cast<int>(N3SfSeqReconcileAction::AdoptForeign), static_cast<int>(r.action));
  TEST_ASSERT_EQUAL_UINT32(500, r.count);
  TEST_ASSERT_EQUAL_UINT32(500, r.cursor);
  TEST_ASSERT_EQUAL_UINT32(0, n3SfSeqPending(r.count, r.cursor));  // rien d'ancien n'est envoyé
  TEST_ASSERT_EQUAL_UINT32(501, n3SfSeqNext(r.count, r.cursor));   // aucune collision de numéro
}

void test_reconcile_orphelin_d_ecriture_interrompue_est_reintegre() {
  // Photo 101 écrite sur SD, coupure avant le commit NVS (count resté à 100).
  const N3SfSeqReconcile r = n3SfSeqReconcile(100, 95, 101);
  TEST_ASSERT_EQUAL(static_cast<int>(N3SfSeqReconcileAction::AdoptOrphan), static_cast<int>(r.action));
  TEST_ASSERT_EQUAL_UINT32(101, r.count);
  TEST_ASSERT_EQUAL_UINT32(95, r.cursor);  // le backlog légitime 96..101 reste à envoyer
  TEST_ASSERT_EQUAL_UINT32(6, n3SfSeqPending(r.count, r.cursor));
}

void test_reconcile_curseur_en_avance_realigne_le_compteur() {
  // État hérité < 2.77 : fichiers étrangers envoyés, curseur 16 > compteur 1.
  const N3SfSeqReconcile r = n3SfSeqReconcile(1, 16, 16);
  TEST_ASSERT_EQUAL(static_cast<int>(N3SfSeqReconcileAction::RealignCount), static_cast<int>(r.action));
  TEST_ASSERT_EQUAL_UINT32(16, r.count);
  TEST_ASSERT_EQUAL_UINT32(16, r.cursor);
}

void test_reconcile_orphelin_au_dessus_du_curseur_en_avance() {
  const N3SfSeqReconcile r = n3SfSeqReconcile(1, 16, 17);
  TEST_ASSERT_EQUAL(static_cast<int>(N3SfSeqReconcileAction::AdoptOrphan), static_cast<int>(r.action));
  TEST_ASSERT_EQUAL_UINT32(17, r.count);
  TEST_ASSERT_EQUAL_UINT32(16, r.cursor);
  TEST_ASSERT_EQUAL_UINT32(1, n3SfSeqPending(r.count, r.cursor));
}

// ---------- n3SfClassifyUploadCode ----------

void test_classification_des_codes_http() {
  TEST_ASSERT_EQUAL(static_cast<int>(N3SfUploadClass::Ok), static_cast<int>(n3SfClassifyUploadCode(200)));
  TEST_ASSERT_EQUAL(static_cast<int>(N3SfUploadClass::Ok), static_cast<int>(n3SfClassifyUploadCode(202)));
  TEST_ASSERT_EQUAL(static_cast<int>(N3SfUploadClass::RateLimited), static_cast<int>(n3SfClassifyUploadCode(429)));
  TEST_ASSERT_EQUAL(static_cast<int>(N3SfUploadClass::ItemRejected), static_cast<int>(n3SfClassifyUploadCode(400)));
  TEST_ASSERT_EQUAL(static_cast<int>(N3SfUploadClass::ItemRejected), static_cast<int>(n3SfClassifyUploadCode(413)));
  TEST_ASSERT_EQUAL(static_cast<int>(N3SfUploadClass::ItemRejected), static_cast<int>(n3SfClassifyUploadCode(415)));
  // Auth / route / serveur / réseau : jamais un motif pour sauter une photo.
  TEST_ASSERT_EQUAL(static_cast<int>(N3SfUploadClass::Transient), static_cast<int>(n3SfClassifyUploadCode(401)));
  TEST_ASSERT_EQUAL(static_cast<int>(N3SfUploadClass::Transient), static_cast<int>(n3SfClassifyUploadCode(404)));
  TEST_ASSERT_EQUAL(static_cast<int>(N3SfUploadClass::Transient), static_cast<int>(n3SfClassifyUploadCode(500)));
  TEST_ASSERT_EQUAL(static_cast<int>(N3SfUploadClass::Transient), static_cast<int>(n3SfClassifyUploadCode(-1)));
  TEST_ASSERT_EQUAL(static_cast<int>(N3SfUploadClass::Transient), static_cast<int>(n3SfClassifyUploadCode(-11)));
}

// ---------- N3SfRejectTracker ----------

void test_tracker_saute_au_seuil_sur_le_meme_element() {
  N3SfRejectTracker t = {0, 0};
  TEST_ASSERT_FALSE(n3SfRejectShouldSkip(t, 42, 3));
  TEST_ASSERT_FALSE(n3SfRejectShouldSkip(t, 42, 3));
  TEST_ASSERT_TRUE(n3SfRejectShouldSkip(t, 42, 3));
  n3SfRejectReset(t);
  TEST_ASSERT_EQUAL_UINT8(0, t.count);
}

void test_tracker_autre_element_repart_a_un() {
  N3SfRejectTracker t = {0, 0};
  TEST_ASSERT_FALSE(n3SfRejectShouldSkip(t, 42, 3));
  TEST_ASSERT_FALSE(n3SfRejectShouldSkip(t, 42, 3));
  TEST_ASSERT_FALSE(n3SfRejectShouldSkip(t, 43, 3));  // nouvel élément en tête
  TEST_ASSERT_EQUAL_UINT32(43, t.handle);
  TEST_ASSERT_EQUAL_UINT8(1, t.count);
}

void test_tracker_handle_zero_compte_correctement() {
  N3SfRejectTracker t = {0, 0};
  TEST_ASSERT_FALSE(n3SfRejectShouldSkip(t, 0, 2));
  TEST_ASSERT_TRUE(n3SfRejectShouldSkip(t, 0, 2));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_next_au_dessus_du_compteur_cas_nominal);
  RUN_TEST(test_next_au_dessus_du_curseur_si_curseur_en_avance);
  RUN_TEST(test_pending_nul_si_curseur_au_dela);
  RUN_TEST(test_is_pending_borne_par_le_compteur);
  RUN_TEST(test_reconcile_support_vide_ou_coherent_ne_change_rien);
  RUN_TEST(test_reconcile_nvs_vierge_carte_reutilisee_adopte_sans_envoyer);
  RUN_TEST(test_reconcile_orphelin_d_ecriture_interrompue_est_reintegre);
  RUN_TEST(test_reconcile_curseur_en_avance_realigne_le_compteur);
  RUN_TEST(test_reconcile_orphelin_au_dessus_du_curseur_en_avance);
  RUN_TEST(test_classification_des_codes_http);
  RUN_TEST(test_tracker_saute_au_seuil_sur_le_meme_element);
  RUN_TEST(test_tracker_autre_element_repart_a_un);
  RUN_TEST(test_tracker_handle_zero_compte_correctement);
  return UNITY_END();
}
