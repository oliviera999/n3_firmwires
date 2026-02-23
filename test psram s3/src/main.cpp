/*
 * ESP32-S3-WROOM-1-N16R8 - Boot, test PSRAM OPI et boucle de stabilité.
 * Utilise heap_caps_* pour détection et test en PSRAM.
 */

#include <Arduino.h>
#include <esp_heap_caps.h>

static const uint32_t TEST_BUFFER_SIZE = 4 * 1024;   // 4 Ko (évite WDT en setup)
static const uint32_t STABILITY_INTERVAL_MS = 5000;  // 5 s entre chaque cycle
static const uint8_t PATTERN_A = 0x55;
static const uint8_t PATTERN_B = 0xAA;

static bool g_psram_ok = false;
static uint32_t g_cycle_count = 0;

/** Teste un bloc PSRAM : alloc, écriture motif, lecture, vérification, libération. */
static bool test_psram_block(size_t size) {
    delay(1);  /* laisser le WDT se nourrir avant accès PSRAM */
    uint8_t* buf = (uint8_t*)heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (!buf) {
        Serial.printf("  [FAIL] heap_caps_malloc(SPIRAM, %u) = NULL\n", (unsigned)size);
        return false;
    }

    for (size_t i = 0; i < size; i++) {
        buf[i] = (i % 2 == 0) ? PATTERN_A : PATTERN_B;
        if ((i & 127) == 0 && i) delay(1);
    }

    bool ok = true;
    for (size_t i = 0; i < size; i++) {
        if ((i & 127) == 0 && i) delay(1);
        uint8_t expected = (i % 2 == 0) ? PATTERN_A : PATTERN_B;
        if (buf[i] != expected) {
            Serial.printf("  [FAIL] mismatch at %u: got 0x%02X expected 0x%02X\n",
                         (unsigned)i, buf[i], expected);
            ok = false;
            break;
        }
    }

    heap_caps_free(buf);
    return ok;
}

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\n=== ESP32-S3 N16R8 - Boot PSRAM OPI ===\n");

    if (!psramFound()) {
        Serial.println("[ERREUR] PSRAM non detectee. Verifiez board_build.psram_type=opi et BOARD_HAS_PSRAM.");
        g_psram_ok = false;
        return;
    }

    g_psram_ok = true;
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    Serial.printf("PSRAM detectee: libre=%u octets, plus grand bloc=%u octets\n\n",
                  (unsigned)free_psram, (unsigned)largest);

    Serial.println("Test fonctionnel (alloc / write / read / free)...");
    if (test_psram_block(TEST_BUFFER_SIZE)) {
        Serial.println("  [OK] Premier test PSRAM reussi.\n");
    } else {
        Serial.println("  [FAIL] Premier test PSRAM echoue.\n");
        g_psram_ok = false;
    }
}

void loop() {
    if (!g_psram_ok) {
        delay(2000);
        return;
    }

    static uint32_t last = 0;
    uint32_t now = millis();
    if (now - last < STABILITY_INTERVAL_MS)
        return;
    last = now;

    g_cycle_count++;
    bool ok = test_psram_block(TEST_BUFFER_SIZE);
    Serial.printf("Cycle %u: %s\n", g_cycle_count, ok ? "OK" : "FAIL");
}
