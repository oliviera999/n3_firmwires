#include <unity.h>

#define PROFILE_BETA
#define USE_TEST_ENDPOINTS
#define USE_LOCAL_SERVER_ENDPOINTS
#define LOCAL_SERVER_BASE_URL "http://127.0.0.1:8082"

#include "../../include/server_url_config.h"

void setUp(void) {}
void tearDown(void) {}

void test_local_base_url_is_used_for_post_data(void) {
  char buffer[160] = {0};
  ServerUrlConfig::getPostDataUrl(buffer, sizeof(buffer));
  TEST_ASSERT_EQUAL_STRING("http://127.0.0.1:8082/ffp3/post-data-test", buffer);
}

void test_local_base_url_is_used_for_heartbeat(void) {
  char buffer[160] = {0};
  ServerUrlConfig::getHeartbeatUrl(buffer, sizeof(buffer));
  TEST_ASSERT_EQUAL_STRING("http://127.0.0.1:8082/ffp3/heartbeat-test", buffer);
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
