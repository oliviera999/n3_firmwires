// Même profil que test_server_url (résout BASE_URL = http://127.0.0.1:8082).
#define PROFILE_BETA
#define USE_TEST_ENDPOINTS
#define USE_LOCAL_SERVER_ENDPOINTS
#define LOCAL_SERVER_BASE_URL "http://127.0.0.1:8082"

#include <unity.h>
#include <cstring>

// Complète test_server_url : couvre getServerHostname (extraction d'hôte avec
// parsing — strip scheme http(s):// puis arrêt à ':' ou '/' + bornage) et
// getOutputUrl, jusqu'ici non testés.
#include "../../include/server_url_config.h"

void setUp(void) {}
void tearDown(void) {}

void test_hostname_strips_scheme_and_port(void) {
  char buf[64] = {0};
  ServerUrlConfig::getServerHostname(buf, sizeof(buf));
  // http://127.0.0.1:8082 -> "127.0.0.1" (scheme + port retirés)
  TEST_ASSERT_EQUAL_STRING("127.0.0.1", buf);
}

void test_hostname_zero_size_is_noop(void) {
  char buf[8] = "KEEP";
  ServerUrlConfig::getServerHostname(buf, 0);
  TEST_ASSERT_EQUAL_STRING("KEEP", buf);  // size 0 ne touche pas le buffer
}

void test_hostname_null_buffer_no_crash(void) {
  ServerUrlConfig::getServerHostname(nullptr, 16);
  TEST_ASSERT_TRUE(true);
}

void test_hostname_truncation_no_overflow(void) {
  char buf[4];
  memset(buf, '#', sizeof(buf));
  ServerUrlConfig::getServerHostname(buf, sizeof(buf));  // "127.0.0.1" -> "127"
  TEST_ASSERT_EQUAL_STRING("127", buf);
  TEST_ASSERT_TRUE(strlen(buf) < sizeof(buf));
}

void test_output_url_well_formed(void) {
  char buf[160] = {0};
  ServerUrlConfig::getOutputUrl(buf, sizeof(buf));
  TEST_ASSERT_TRUE(strlen(buf) > 0);
  // Doit reposer sur la base locale configurée.
  TEST_ASSERT_EQUAL_INT(0, strncmp(buf, "http://127.0.0.1:8082", 21));
  // Pas de double slash après le schéma.
  const char* rest = strstr(buf, "://") + 3;
  TEST_ASSERT_NULL(strstr(rest, "//"));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_hostname_strips_scheme_and_port);
  RUN_TEST(test_hostname_zero_size_is_noop);
  RUN_TEST(test_hostname_null_buffer_no_crash);
  RUN_TEST(test_hostname_truncation_no_overflow);
  RUN_TEST(test_output_url_well_formed);
  return UNITY_END();
}
