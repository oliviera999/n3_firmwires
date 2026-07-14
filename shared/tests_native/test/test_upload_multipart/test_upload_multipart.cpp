#include <unity.h>
#include <cstring>
#include <cstdint>

// Tests de N3MultipartReader + N3UploadBufferSource (n3_upload, briques pures) —
// transposition de MultipartCameraStream/MultipartFileStream::readBytes
// (uploadphotosserver/camera_upload.cpp). On verrouille : totalSize, séquence
// head|corps|tail octet-exacte, chunking aux frontières de segments, retours
// courts d'une source tarie, rewind pour retry.

#include "n3_multipart_reader.h"

void setUp(void) {}
void tearDown(void) {}

static const char* HEAD = "--B\r\nCD\r\n\r\n";  // 11 octets
static const uint8_t BODY[] = {0x01, 0x02, 0x03, 0x04, 0x05};
static const char* TAIL = "\r\n--B--\r\n";      // 9 octets

// Lit tout via des chunks de taille chunk et concatène dans out (retourne total).
static size_t drainAll(N3MultipartReader& r, char* out, size_t outCap, size_t chunk) {
  size_t total = 0;
  for (;;) {
    if (total >= outCap) break;
    size_t want = chunk;
    if (want > outCap - total) want = outCap - total;
    const size_t n = r.readBytes(out + total, want);
    if (n == 0) break;
    total += n;
  }
  return total;
}

void test_total_size(void) {
  N3UploadBufferSource src(BODY, sizeof(BODY));
  N3MultipartReader r(HEAD, src, TAIL);
  TEST_ASSERT_EQUAL_size_t(strlen(HEAD) + sizeof(BODY) + strlen(TAIL), r.totalSize());
}

void test_sequence_complete_en_une_lecture(void) {
  N3UploadBufferSource src(BODY, sizeof(BODY));
  N3MultipartReader r(HEAD, src, TAIL);
  char out[64] = {};
  const size_t n = r.readBytes(out, sizeof(out));
  TEST_ASSERT_EQUAL_size_t(r.totalSize(), n);
  TEST_ASSERT_EQUAL_MEMORY(HEAD, out, strlen(HEAD));
  TEST_ASSERT_EQUAL_MEMORY(BODY, out + strlen(HEAD), sizeof(BODY));
  TEST_ASSERT_EQUAL_MEMORY(TAIL, out + strlen(HEAD) + sizeof(BODY), strlen(TAIL));
}

void test_chunking_traverse_les_frontieres(void) {
  // Chunks de 4 octets : coupent head/corps et corps/tail. Le résultat
  // concaténé doit être octet-identique à la lecture d'un bloc.
  N3UploadBufferSource src(BODY, sizeof(BODY));
  N3MultipartReader r(HEAD, src, TAIL);
  char out[64] = {};
  const size_t n = drainAll(r, out, sizeof(out), 4);
  TEST_ASSERT_EQUAL_size_t(r.totalSize(), n);
  TEST_ASSERT_EQUAL_MEMORY(HEAD, out, strlen(HEAD));
  TEST_ASSERT_EQUAL_MEMORY(BODY, out + strlen(HEAD), sizeof(BODY));
  TEST_ASSERT_EQUAL_MEMORY(TAIL, out + strlen(HEAD) + sizeof(BODY), strlen(TAIL));
}

void test_chunking_un_par_un(void) {
  N3UploadBufferSource src(BODY, sizeof(BODY));
  N3MultipartReader r(HEAD, src, TAIL);
  char out[64] = {};
  const size_t n = drainAll(r, out, sizeof(out), 1);
  TEST_ASSERT_EQUAL_size_t(r.totalSize(), n);
  TEST_ASSERT_EQUAL_MEMORY(BODY, out + strlen(HEAD), sizeof(BODY));
}

void test_head_tail_vides(void) {
  N3UploadBufferSource src(BODY, sizeof(BODY));
  N3MultipartReader r("", src, "");
  char out[16] = {};
  const size_t n = r.readBytes(out, sizeof(out));
  TEST_ASSERT_EQUAL_size_t(sizeof(BODY), n);
  TEST_ASSERT_EQUAL_MEMORY(BODY, out, sizeof(BODY));
}

void test_corps_vide(void) {
  N3UploadBufferSource src(nullptr, 0);
  N3MultipartReader r(HEAD, src, TAIL);
  char out[32] = {};
  const size_t n = r.readBytes(out, sizeof(out));
  TEST_ASSERT_EQUAL_size_t(strlen(HEAD) + strlen(TAIL), n);
  TEST_ASSERT_EQUAL_MEMORY(TAIL, out + strlen(HEAD), strlen(TAIL));
}

void test_lecture_apres_fin_rend_zero(void) {
  N3UploadBufferSource src(BODY, sizeof(BODY));
  N3MultipartReader r(HEAD, src, TAIL);
  char out[64] = {};
  (void)r.readBytes(out, sizeof(out));       // draine tout
  TEST_ASSERT_EQUAL_size_t(0, r.readBytes(out, sizeof(out)));
}

void test_gardes_buffer(void) {
  N3UploadBufferSource src(BODY, sizeof(BODY));
  N3MultipartReader r(HEAD, src, TAIL);
  TEST_ASSERT_EQUAL_size_t(0, r.readBytes(nullptr, 16));
  char out[4];
  TEST_ASSERT_EQUAL_size_t(0, r.readBytes(out, 0));
}

// Source qui rend au plus 2 octets par appel (simule file_.read court /
// UPLOAD_CHUNK_SIZE) : le lecteur doit quand même tout servir.
struct DribbleSource : public N3UploadSource {
  const uint8_t* d; size_t len; size_t pos = 0;
  DribbleSource(const uint8_t* dd, size_t l) : d(dd), len(l) {}
  size_t size() const override { return len; }
  void rewind() override { pos = 0; }
  size_t readChunk(uint8_t* dst, size_t maxLen) override {
    if (pos >= len || maxLen == 0) return 0;
    size_t n = len - pos; if (n > 2) n = 2; if (n > maxLen) n = maxLen;
    memcpy(dst, d + pos, n); pos += n; return n;
  }
};

void test_source_au_compte_goutte(void) {
  DribbleSource src(BODY, sizeof(BODY));
  N3MultipartReader r(HEAD, src, TAIL);
  char out[64] = {};
  const size_t n = drainAll(r, out, sizeof(out), 64);
  TEST_ASSERT_EQUAL_size_t(r.totalSize(), n);
  TEST_ASSERT_EQUAL_MEMORY(BODY, out + strlen(HEAD), sizeof(BODY));
}

// Source tronquée : annonce 5 octets mais n'en fournit que 3 -> retour court
// (parité MultipartFileStream : break si file_.read rend 0).
struct TruncatedSource : public N3UploadSource {
  size_t served = 0;
  size_t size() const override { return 5; }
  void rewind() override { served = 0; }
  size_t readChunk(uint8_t* dst, size_t maxLen) override {
    if (served >= 3 || maxLen == 0) return 0;
    size_t n = 3 - served; if (n > maxLen) n = maxLen;
    memset(dst, 0xAA, n); served += n; return n;
  }
};

void test_source_tronquee_retour_court(void) {
  TruncatedSource src;
  N3MultipartReader r(HEAD, src, TAIL);
  char out[64] = {};
  const size_t n = r.readBytes(out, sizeof(out));
  // head complet + 3 octets servis, puis arrêt (pas de tail : incohérence
  // détectable par l'appelant via n != totalSize()).
  TEST_ASSERT_EQUAL_size_t(strlen(HEAD) + 3, n);
  TEST_ASSERT_TRUE(n != r.totalSize());
}

void test_rewind_pour_retry(void) {
  N3UploadBufferSource src(BODY, sizeof(BODY));
  N3MultipartReader r(HEAD, src, TAIL);
  char out1[64] = {};
  const size_t n1 = r.readBytes(out1, sizeof(out1));
  r.rewind();  // retry d'envoi : tout doit être relisible à l'identique
  char out2[64] = {};
  const size_t n2 = r.readBytes(out2, sizeof(out2));
  TEST_ASSERT_EQUAL_size_t(n1, n2);
  TEST_ASSERT_EQUAL_MEMORY(out1, out2, n1);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_total_size);
  RUN_TEST(test_sequence_complete_en_une_lecture);
  RUN_TEST(test_chunking_traverse_les_frontieres);
  RUN_TEST(test_chunking_un_par_un);
  RUN_TEST(test_head_tail_vides);
  RUN_TEST(test_corps_vide);
  RUN_TEST(test_lecture_apres_fin_rend_zero);
  RUN_TEST(test_gardes_buffer);
  RUN_TEST(test_source_au_compte_goutte);
  RUN_TEST(test_source_tronquee_retour_court);
  RUN_TEST(test_rewind_pour_retry);
  return UNITY_END();
}
