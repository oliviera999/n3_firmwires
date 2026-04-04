#pragma once

#include <cstddef>
#include <cstdio>

#if defined(USE_LOCAL_SERVER_ENDPOINTS) && !defined(LOCAL_SERVER_BASE_URL) && __has_include("local_server_overrides.h")
#include "local_server_overrides.h"
#endif

namespace ServerUrlConfig {
  #if defined(USE_LOCAL_SERVER_ENDPOINTS) && defined(LOCAL_SERVER_BASE_URL)
    inline constexpr const char* BASE_URL = LOCAL_SERVER_BASE_URL;
  #elif defined(USE_LOCAL_SERVER_ENDPOINTS) && defined(LOCAL_SERVER_BASE_URL_OVERRIDE)
    inline constexpr const char* BASE_URL = LOCAL_SERVER_BASE_URL_OVERRIDE;
  #elif defined(USE_LOCAL_SERVER_ENDPOINTS)
    inline constexpr const char* BASE_URL = "http://127.0.0.1:8082";
  #else
    inline constexpr const char* BASE_URL = "http://iot.olution.info";
  #endif

  #if defined(USE_LOCAL_SERVER_ENDPOINTS) && defined(LOCAL_SERVER_BASE_URL)
    inline constexpr const char* BASE_URL_SECURE = LOCAL_SERVER_BASE_URL;
  #elif defined(USE_LOCAL_SERVER_ENDPOINTS) && defined(LOCAL_SERVER_BASE_URL_OVERRIDE)
    inline constexpr const char* BASE_URL_SECURE = LOCAL_SERVER_BASE_URL_OVERRIDE;
  #elif defined(USE_LOCAL_SERVER_ENDPOINTS)
    inline constexpr const char* BASE_URL_SECURE = "http://127.0.0.1:8082";
  #else
    inline constexpr const char* BASE_URL_SECURE = "https://iot.olution.info";
  #endif

  #if defined(USE_TEST3_ENDPOINTS)
    inline constexpr const char* POST_DATA_ENDPOINT = "/ffp3/post-data3-test";
    inline constexpr const char* OUTPUT_ENDPOINT = "/ffp3/api/outputs3-test/state";
    inline constexpr const char* HEARTBEAT_ENDPOINT = "/ffp3/heartbeat3-test";
  #elif defined(BOARD_S3) && defined(PROFILE_PROD)
    inline constexpr const char* POST_DATA_ENDPOINT = "/ffp3/post-data3";
    inline constexpr const char* OUTPUT_ENDPOINT = "/ffp3/api/outputs3/state";
    inline constexpr const char* HEARTBEAT_ENDPOINT = "/ffp3/heartbeat3";
  #elif defined(PROFILE_TEST) || defined(PROFILE_DEV) || defined(USE_TEST_ENDPOINTS)
    inline constexpr const char* POST_DATA_ENDPOINT = "/ffp3/post-data-test";
    inline constexpr const char* OUTPUT_ENDPOINT = "/ffp3/api/outputs-test/state";
    inline constexpr const char* HEARTBEAT_ENDPOINT = "/ffp3/heartbeat-test";
  #else
    inline constexpr const char* POST_DATA_ENDPOINT = "/ffp3/post-data";
    inline constexpr const char* OUTPUT_ENDPOINT = "/ffp3/api/outputs/state";
    inline constexpr const char* HEARTBEAT_ENDPOINT = "/ffp3/heartbeat";
  #endif

  inline constexpr const char* OTA_BASE_PATH = "/ota/";

  inline void getPostDataUrl(char* buffer, size_t bufferSize) {
    snprintf(buffer, bufferSize, "%s%s", BASE_URL, POST_DATA_ENDPOINT);
  }

  inline void getOutputUrl(char* buffer, size_t bufferSize) {
    snprintf(buffer, bufferSize, "%s%s", BASE_URL, OUTPUT_ENDPOINT);
  }

  inline void getHeartbeatUrl(char* buffer, size_t bufferSize) {
    snprintf(buffer, bufferSize, "%s%s", BASE_URL, HEARTBEAT_ENDPOINT);
  }

  inline void getOtaBaseUrl(char* buffer, size_t bufferSize) {
    snprintf(buffer, bufferSize, "%s%s", BASE_URL_SECURE, OTA_BASE_PATH);
  }
}
