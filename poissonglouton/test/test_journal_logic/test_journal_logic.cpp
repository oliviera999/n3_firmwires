// -----------------------------------------------------------------------------
// Tests natifs (hote) de PglEventJournal — logique pure (CRC + (de)codage record).
//
// On compile directement pgl_event_journal.cpp contre les stubs hote
// (Arduino/Preferences/SD/pgl_sd_storage). On accede aux internes prives
// (crc16, decodeRecord, JournalRecord) via la struct amie PglJournalTestAccess
// declaree friend sous UNIT_TEST — aucune modification du comportement firmware.
//
// Cibles :
//   - crc16 : vecteurs connus + stabilite/determinisme + sensibilite a 1 bit.
//   - decodeRecord : round-trip encode->decode (struct + CRC big-endian) et
//     detection de CRC corrompu.
// -----------------------------------------------------------------------------

#include <unity.h>

#include <cstdint>
#include <cstring>

// Stub minimal du backend SD storage attendu par le .cpp du journal.
bool pglSdStorageBegin() { return true; }
bool pglSdStorageIsMounted() { return true; }

#include "../../src/pgl_event_journal.cpp"

// Accesseur de test : expose les membres prives statiques/struct du journal.
struct PglJournalTestAccess {
  using Record = PglEventJournal::JournalRecord;
  static constexpr size_t RECORD_SIZE = PglEventJournal::RECORD_SIZE;

  static uint16_t crc16(const uint8_t* d, size_t n) {
    return PglEventJournal::crc16(d, n);
  }
  static bool decodeRecord(const uint8_t* buf, Record& out) {
    return PglEventJournal::decodeRecord(buf, out);
  }

  // Encode un record dans un buffer RECORD_SIZE (struct 18o + CRC16 big-endian),
  // exactement comme writeRecord() du firmware.
  static void encode(const Record& rec, uint8_t* buf) {
    memset(buf, 0, RECORD_SIZE);
    memcpy(buf, &rec, sizeof(Record));
    const uint16_t crc = PglEventJournal::crc16(buf, sizeof(Record));
    buf[sizeof(Record)] = static_cast<uint8_t>(crc >> 8);
    buf[sizeof(Record) + 1] = static_cast<uint8_t>(crc & 0xFF);
  }
};

void setUp(void) {}
void tearDown(void) {}

// --- CRC16 ------------------------------------------------------------------

// Vecteur connu pour CRC-16/CCITT-FALSE (init 0xFFFF, poly 0x1021, sans
// reflexion, sans xorout) : "123456789" -> 0x29B1. C'est la variante utilisee
// par crc16() (cf. boucle XOR << 8 / poly 0x1021).
void test_crc16_known_vector_check_string() {
  const uint8_t data[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  TEST_ASSERT_EQUAL_HEX16(0x29B1, PglJournalTestAccess::crc16(data, sizeof(data)));
}

void test_crc16_empty_is_init() {
  // Sur une entree vide, le CRC reste a la valeur d'initialisation 0xFFFF.
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, PglJournalTestAccess::crc16(nullptr, 0));
}

void test_crc16_single_byte_zero() {
  const uint8_t data[] = {0x00};
  // 0xFFFF traverse par un octet nul -> 0xE1F0 (CRC-16/CCITT-FALSE).
  TEST_ASSERT_EQUAL_HEX16(0xE1F0, PglJournalTestAccess::crc16(data, sizeof(data)));
}

void test_crc16_deterministic() {
  const uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02};
  const uint16_t a = PglJournalTestAccess::crc16(data, sizeof(data));
  const uint16_t b = PglJournalTestAccess::crc16(data, sizeof(data));
  TEST_ASSERT_EQUAL_HEX16(a, b);
}

void test_crc16_sensitive_to_one_bit_flip() {
  uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02};
  const uint16_t base = PglJournalTestAccess::crc16(data, sizeof(data));
  data[3] ^= 0x01;  // flip d'un seul bit
  const uint16_t flipped = PglJournalTestAccess::crc16(data, sizeof(data));
  TEST_ASSERT_NOT_EQUAL(base, flipped);
}

// --- decodeRecord -----------------------------------------------------------

static PglJournalTestAccess::Record makeRecord() {
  PglJournalTestAccess::Record rec{};
  rec.eventId = 0x11223344u;
  rec.epoch = 1700001234u;
  rec.countDelta = 7;
  rec.sensorMode = 3;
  rec.tandemValidated = 1;
  rec.batteryMilliVolt = 3700;
  rec.rssi = -65;
  return rec;
}

void test_decode_record_roundtrip() {
  const auto rec = makeRecord();
  uint8_t buf[PglJournalTestAccess::RECORD_SIZE];
  PglJournalTestAccess::encode(rec, buf);

  PglJournalTestAccess::Record out{};
  TEST_ASSERT_TRUE(PglJournalTestAccess::decodeRecord(buf, out));
  TEST_ASSERT_EQUAL_UINT32(rec.eventId, out.eventId);
  TEST_ASSERT_EQUAL_UINT32(rec.epoch, out.epoch);
  TEST_ASSERT_EQUAL_UINT16(rec.countDelta, out.countDelta);
  TEST_ASSERT_EQUAL_UINT8(rec.sensorMode, out.sensorMode);
  TEST_ASSERT_EQUAL_UINT8(rec.tandemValidated, out.tandemValidated);
  TEST_ASSERT_EQUAL_INT16(rec.batteryMilliVolt, out.batteryMilliVolt);
  TEST_ASSERT_EQUAL_INT16(rec.rssi, out.rssi);
}

void test_decode_record_detects_payload_corruption() {
  const auto rec = makeRecord();
  uint8_t buf[PglJournalTestAccess::RECORD_SIZE];
  PglJournalTestAccess::encode(rec, buf);

  buf[2] ^= 0xFF;  // corrompt la charge utile sans toucher au CRC stocke

  PglJournalTestAccess::Record out{};
  TEST_ASSERT_FALSE(PglJournalTestAccess::decodeRecord(buf, out));
}

void test_decode_record_detects_crc_corruption() {
  const auto rec = makeRecord();
  uint8_t buf[PglJournalTestAccess::RECORD_SIZE];
  PglJournalTestAccess::encode(rec, buf);

  buf[PglJournalTestAccess::RECORD_SIZE - 1] ^= 0xFF;  // corrompt l'octet CRC bas

  PglJournalTestAccess::Record out{};
  TEST_ASSERT_FALSE(PglJournalTestAccess::decodeRecord(buf, out));
}

void test_decode_record_crc_is_big_endian() {
  // Verifie que l'encodage CRC est bien big-endian (haut puis bas), conforme
  // a writeRecord()/decodeRecord(). On reconstruit le CRC attendu a la main.
  const auto rec = makeRecord();
  uint8_t buf[PglJournalTestAccess::RECORD_SIZE];
  PglJournalTestAccess::encode(rec, buf);

  const size_t s = sizeof(PglJournalTestAccess::Record);
  const uint16_t crc = PglJournalTestAccess::crc16(buf, s);
  TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(crc >> 8), buf[s]);
  TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(crc & 0xFF), buf[s + 1]);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_crc16_known_vector_check_string);
  RUN_TEST(test_crc16_empty_is_init);
  RUN_TEST(test_crc16_single_byte_zero);
  RUN_TEST(test_crc16_deterministic);
  RUN_TEST(test_crc16_sensitive_to_one_bit_flip);
  RUN_TEST(test_decode_record_roundtrip);
  RUN_TEST(test_decode_record_detects_payload_corruption);
  RUN_TEST(test_decode_record_detects_crc_corruption);
  RUN_TEST(test_decode_record_crc_is_big_endian);
  return UNITY_END();
}
