#include <unity.h>

#define PROFILE_BETA
#define USE_TEST_ENDPOINTS
#define USE_LOCAL_SERVER_ENDPOINTS
#define LOCAL_SERVER_BASE_URL "http://127.0.0.1:8082"

#include "../../include/server_url_config.h"

void setUp(void) {}
void tearDown(void) {}

// Note v13.93 (audit) : depuis v13.87 les endpoints POST/heartbeat sont canoniques
// SANS préfixe /ffp3/ (les URLs /ffp3/* provoquent un 301 Apache que le HTTPClient
// ESP32 ne suit pas — cf. server_url_config.h). Test réaligné sur ce comportement
// (il était resté sur l'ancien /ffp3/ et n'était pas détecté faute de CI de tests).
void test_local_base_url_is_used_for_post_data(void) {
  char buffer[160] = {0};
  ServerUrlConfig::getPostDataUrl(buffer, sizeof(buffer));
  TEST_ASSERT_EQUAL_STRING("http://127.0.0.1:8082/post-data-test", buffer);
}

void test_local_base_url_is_used_for_heartbeat(void) {
  char buffer[160] = {0};
  ServerUrlConfig::getHeartbeatUrl(buffer, sizeof(buffer));
  TEST_ASSERT_EQUAL_STRING("http://127.0.0.1:8082/heartbeat-test", buffer);
}

void test_local_base_url_is_used_for_ota_base(void) {
  char buffer[160] = {0};
  ServerUrlConfig::getOtaBaseUrl(buffer, sizeof(buffer));
  TEST_ASSERT_EQUAL_STRING("http://127.0.0.1:8082/ota/", buffer);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_local_base_url_is_used_for_post_data);
  RUN_TEST(test_local_base_url_is_used_for_heartbeat);
  RUN_TEST(test_local_base_url_is_used_for_ota_base);
  return UNITY_END();
}
