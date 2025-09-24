#ifndef PROJECT_CONFIG_H
#define PROJECT_CONFIG_H

// System Configuration
#define DEVICE_NAME "ESP32_FFP3CS4"
#define FIRMWARE_VERSION "10.20"
#define DEBUG_LEVEL 2

// WiFi Configuration
#define WIFI_RETRY_DELAY 5000
#define WIFI_MAX_RETRIES 10
#define WIFI_CONNECT_TIMEOUT 20000

// Web Server Configuration
#define WEB_SERVER_PORT 80
#define WS_MAX_CLIENTS 4
#define WEB_BUFFER_SIZE 8192

// Sensor Configuration
#define DHT_PIN 4
#define DHT_TYPE DHT22
#define DS18B20_PIN 5
#define SOIL_MOISTURE_PIN 34
#define LIGHT_SENSOR_PIN 35
#define WATER_LEVEL_PIN 36
#define SENSOR_READ_INTERVAL 5000
#define SENSOR_CACHE_SIZE 100

// Actuator Configuration
#define PUMP_PIN 25
#define FAN_PIN 26
#define LIGHT_PIN 27
#define VALVE_PIN 32
#define HEATER_PIN 33
#define ACTUATOR_PWM_FREQ 5000
#define ACTUATOR_PWM_RESOLUTION 8

// Display Configuration
#define TFT_CS   15
#define TFT_DC   2
#define TFT_RST  -1
#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_WIDTH 320
#define TFT_HEIGHT 240

// Memory Configuration
#define USE_PSRAM 1
#define PSRAM_CACHE_SIZE 32768
#define JSON_BUFFER_SIZE 4096
#define LOG_BUFFER_SIZE 2048

// Task Configuration
#define SENSOR_TASK_STACK 8192
#define WEB_TASK_STACK 16384
#define AUTO_TASK_STACK 8192
#define DISPLAY_TASK_STACK 8192
#define TASK_PRIORITY_HIGH 3
#define TASK_PRIORITY_NORMAL 2
#define TASK_PRIORITY_LOW 1

// Safety Thresholds
#define MAX_TEMPERATURE 40.0
#define MIN_TEMPERATURE 5.0
#define MAX_HUMIDITY 90.0
#define MIN_HUMIDITY 20.0
#define LOW_WATER_THRESHOLD 20
#define CRITICAL_HEAP_SIZE 10000
#define WARNING_HEAP_SIZE 20000

// Communication Configuration
#define MQTT_BROKER "mqtt.broker.com"
#define MQTT_PORT 1883
#define MQTT_KEEPALIVE 60
#define MQTT_QOS 1
#define HTTP_TIMEOUT 10000
#define API_ENDPOINT "https://api.server.com/data"

// OTA Configuration
#define OTA_HOSTNAME "ESP32-OTA"
#define OTA_PASSWORD "admin"
#define OTA_PORT 3232

// Time Configuration
#define NTP_SERVER "pool.ntp.org"
#define NTP_TIMEZONE "CET-1CEST,M3.5.0,M10.5.0/3"
#define TIME_SYNC_INTERVAL 3600000 // 1 hour

// Error Handling
#define MAX_ERROR_COUNT 10
#define ERROR_RESET_INTERVAL 3600000
#define WATCHDOG_TIMEOUT 30000

#endif // PROJECT_CONFIG_H