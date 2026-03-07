/* Firmware camera unifie — n3 IoT v2.0
 * 3 cibles : msp1, n3pp, ffp3
 * Build : pio run -e msp1 / pio run -e n3pp / pio run -e ffp3
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include "esp_camera.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "time.h"
#include "config.h"
#include "credentials.h"
#include "n3_ota.h"

#if USE_SD
#include "FS.h"
#include "SD_MMC.h"
#include <EEPROM.h>
#define EEPROM_SIZE 1
#endif

#include <ArduinoJson.h>

// ── WiFi ────────────────────────────────────────────────────────────
const char* ssid     = WIFI_SSID1;
const char* password = WIFI_PASS1;
const char* ssid2    = WIFI_SSID2;
const char* password2 = WIFI_PASS2;
const char* ssid3    = WIFI_SSID3;
const char* password3 = WIFI_PASS3;

// ── Variables globales ──────────────────────────────────────────────
#if USE_DEEP_SLEEP
RTC_DATA_ATTR int bootCount = 0;
#ifdef OTA_CHECK_EVERY_N
RTC_DATA_ATTR int otaBootCount = 0;
#endif
#else
unsigned long previousMillis = 0;
#endif

#if USE_SD
RTC_DATA_ATTR int pictureNumber = 0;
#endif

// ── LED ─────────────────────────────────────────────────────────────
void ledBlink(int onMs, int offMs, int count) {
    for (int i = 0; i < count; i++) {
        digitalWrite(LED_STATUS_PIN, LOW);  // active LOW sur GPIO 33
        delay(onMs);
        digitalWrite(LED_STATUS_PIN, HIGH);
        delay(offMs);
    }
}

// ── WiFi multi-reseau ───────────────────────────────────────────────
bool Wificonnect() {
    int attempt = 0;
    const char* networks[][2] = {
        {ssid, password}, {ssid2, password2}, {ssid3, password3}
    };

    for (int n = 0; n < 3; n++) {
        Serial.printf("[WiFi] Tentative %s...\n", networks[n][0]);
        WiFi.begin(networks[n][0], networks[n][1]);

        attempt = 0;
        while (WiFi.status() != WL_CONNECTED && attempt < 20) {
            delay(500);
            Serial.print(".");
            ledBlink(500, 500, 1);
            attempt++;
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("\n[WiFi] Connecte a %s — IP: %s\n",
                          networks[n][0], WiFi.localIP().toString().c_str());
            return true;
        }
        Serial.println("\n[WiFi] Echec");
    }

    Serial.println("[WiFi] Aucun reseau disponible");
    return false;
}

// ── Camera init ─────────────────────────────────────────────────────
bool initCamera() {
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;
    config.pin_d0       = Y2_GPIO_NUM;
    config.pin_d1       = Y3_GPIO_NUM;
    config.pin_d2       = Y4_GPIO_NUM;
    config.pin_d3       = Y5_GPIO_NUM;
    config.pin_d4       = Y6_GPIO_NUM;
    config.pin_d5       = Y7_GPIO_NUM;
    config.pin_d6       = Y8_GPIO_NUM;
    config.pin_d7       = Y9_GPIO_NUM;
    config.pin_xclk     = XCLK_GPIO_NUM;
    config.pin_pclk     = PCLK_GPIO_NUM;
    config.pin_vsync    = VSYNC_GPIO_NUM;
    config.pin_href     = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn     = PWDN_GPIO_NUM;
    config.pin_reset    = RESET_GPIO_NUM;
    config.xclk_freq_hz = XCLK_FREQ;
    config.pixel_format = PIXFORMAT_JPEG;
    config.grab_mode    = CAMERA_GRAB_LATEST;

    if (psramFound()) {
        config.frame_size   = FRAMESIZE_DEFAULT;
        config.jpeg_quality = JPEG_QUALITY;
        config.fb_count     = 2;
    } else {
        config.frame_size   = FRAMESIZE_CIF;
        config.jpeg_quality = 12;
        config.fb_count     = 1;
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("[CAM] Init echouee: 0x%x\n", err);
        return false;
    }
    Serial.println("[CAM] Initialisee OK");
    return true;
}

// ── Warmup camera (stabilisation exposition/AWB apres deep sleep) ───
void warmupCamera() {
    sensor_t* s = esp_camera_sensor_get();
    if (!s) return;

    s->set_aec2(s, 1);
    s->set_ae_level(s, 0);
    s->set_aec_value(s, 300);
    s->set_agc_gain(s, 0);
    s->set_awb_gain(s, 1);
    s->set_wb_mode(s, 0);

    for (int i = 0; i < 3; i++) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (fb) esp_camera_fb_return(fb);
        delay(100);
    }
    Serial.println("[CAM] Warmup termine");
}

// ── Ajustement exposition ───────────────────────────────────────────
void adjustExposure(camera_fb_t* fb) {
    if (!fb || fb->len == 0) return;

    long sum = 0;
    int sampleSize = min((size_t)1000, fb->len);
    int step = fb->len / sampleSize;
    for (int i = 0; i < (int)fb->len; i += step) {
        sum += fb->buf[i];
    }
    int avgBrightness = sum / sampleSize;
    Serial.printf("[CAM] Luminosite moyenne: %d\n", avgBrightness);

    sensor_t* s = esp_camera_sensor_get();
    if (!s) return;

    if (avgBrightness < 60) {
        s->set_aec_value(s, 600);
        s->set_agc_gain(s, 20);
    } else if (avgBrightness < 100) {
        s->set_aec_value(s, 400);
        s->set_agc_gain(s, 10);
    } else if (avgBrightness > 200) {
        s->set_aec_value(s, 100);
        s->set_agc_gain(s, 0);
    }
}

// ── NTP ─────────────────────────────────────────────────────────────
bool isInPhotoWindow() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 5000)) {
        Serial.println("[NTP] Heure indisponible — photo par defaut");
        return true;  // fail-open
    }
    int h = timeinfo.tm_hour;
    Serial.printf("[NTP] Heure: %02d:%02d\n", h, timeinfo.tm_min);
    return (h >= HOUR_START && h < HOUR_END);
}

// ── Envoi photo ─────────────────────────────────────────────────────
bool sendPhoto() {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("[CAM] Capture echouee");
        return false;
    }

    Serial.printf("[CAM] Photo: %u octets\n", fb->len);

#if USE_SD
    pictureNumber++;
    String path = "/picture" + String(pictureNumber) + ".jpg";
    fs::FS& fs = SD_MMC;
    File file = fs.open(path.c_str(), FILE_WRITE);
    if (file) {
        file.write(fb->buf, fb->len);
        file.close();
        Serial.printf("[SD] Enregistree: %s\n", path.c_str());
    }
    EEPROM.write(0, pictureNumber);
    EEPROM.commit();
#endif

    adjustExposure(fb);

    WiFiClient client;
    if (!client.connect(SERVER_NAME, SERVER_PORT)) {
        Serial.println("[HTTP] Connexion echouee");
        esp_camera_fb_return(fb);
        return false;
    }
    client.setTimeout(HTTP_TIMEOUT_MS / 1000);

    String head = "--" + String(BOUNDARY) + "\r\n"
                  "Content-Disposition: form-data; name=\"imageFile\"; filename=\"esp32-cam.jpg\"\r\n"
                  "Content-Type: image/jpeg\r\n\r\n";
    String tail = "\r\n--" + String(BOUNDARY) + "--\r\n";

    uint32_t totalLen = head.length() + fb->len + tail.length();

    client.println("POST " + String(SERVER_PATH) + " HTTP/1.1");
    client.println("Host: " + String(SERVER_NAME));
    client.println("Content-Length: " + String(totalLen));
    client.println("Content-Type: multipart/form-data; boundary=" + String(BOUNDARY));
    client.println();
    client.print(head);

    uint8_t* buf = fb->buf;
    size_t remaining = fb->len;
    while (remaining > 0) {
        size_t chunk = min(remaining, (size_t)HTTP_CHUNK_SIZE);
        client.write(buf, chunk);
        buf += chunk;
        remaining -= chunk;
    }

    client.print(tail);

    // Lire la reponse
    long timeout = millis() + HTTP_TIMEOUT_MS;
    while (client.connected() && millis() < timeout) {
        if (client.available()) {
            String line = client.readStringUntil('\n');
            Serial.println(line);
            if (line.length() <= 1) break;
        }
    }
    String response = client.readString();
    Serial.println("[HTTP] Reponse: " + response);

    client.stop();
    esp_camera_fb_return(fb);

    ledBlink(1500, 1500, 1);
    return true;
}

// ── OTA distant ─────────────────────────────────────────────────────
void checkOta() {
    // Parser le JSON global et extraire l'URL pour notre cible
    WiFiClient client;
    HTTPClient http;
    http.begin(client, OTA_METADATA_URL);
    http.setTimeout(10000);
    int code = http.GET();

    if (code != 200) {
        Serial.printf("[OTA] Erreur HTTP %d\n", code);
        http.end();
        return;
    }

    String payload = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        Serial.printf("[OTA] JSON parse error: %s\n", err.c_str());
        return;
    }

    JsonObject target = doc[OTA_TARGET_KEY];
    if (target.isNull()) {
        Serial.printf("[OTA] Cible '%s' absente du JSON\n", OTA_TARGET_KEY);
        return;
    }

    const char* remoteVersion = target["version"];
    const char* firmwareUrl   = target["url"];
    if (!remoteVersion || !firmwareUrl) {
        Serial.println("[OTA] Champs version/url manquants");
        return;
    }

    // Construire un metadata URL simplifie pour n3_ota
    // On extrait directement version et url depuis le JSON global
    // et on appelle n3OtaCheck avec une URL forge par cible
    N3OtaConfig otaConfig = {
        firmwareUrl,         // on passe directement l'URL du binaire... non.
        FIRMWARE_VERSION,
        LED_STATUS_PIN
    };

    // Comparaison manuelle car n3OtaCheck attend un metadata.json simple
    int cmp = 0;
    {
        int maj1 = 0, min1 = 0, pat1 = 0;
        int maj2 = 0, min2 = 0, pat2 = 0;
        sscanf(FIRMWARE_VERSION, "%d.%d.%d", &maj1, &min1, &pat1);
        sscanf(remoteVersion, "%d.%d.%d", &maj2, &min2, &pat2);
        if (maj2 != maj1) cmp = maj2 - maj1;
        else if (min2 != min1) cmp = min2 - min1;
        else cmp = pat2 - pat1;
    }

    if (cmp <= 0) {
        Serial.printf("[OTA] Deja a jour (%s)\n", FIRMWARE_VERSION);
        return;
    }

    Serial.printf("[OTA] MAJ %s -> %s, telechargement...\n",
                  FIRMWARE_VERSION, remoteVersion);

    httpUpdate.setLedPin(LED_STATUS_PIN, LOW);
    httpUpdate.rebootOnUpdate(true);

    WiFiClient updateClient;
    t_httpUpdate_return ret = httpUpdate.update(updateClient, firmwareUrl);
    if (ret == HTTP_UPDATE_FAILED) {
        Serial.printf("[OTA] Echec: %s\n", httpUpdate.getLastErrorString().c_str());
    }
}

// ═══════════════════════════════════════════════════════════════════
void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);  // desactiver brown-out

    pinMode(LED_STATUS_PIN, OUTPUT);
    digitalWrite(LED_STATUS_PIN, HIGH);

    Serial.begin(115200);
    delay(100);
    Serial.printf("\n[n3cam] %s v%s — boot\n", OTA_TARGET_KEY, FIRMWARE_VERSION);

#if USE_SD
    if (!SD_MMC.begin()) {
        Serial.println("[SD] Montage echoue");
    } else {
        Serial.println("[SD] Montee OK");
    }
    EEPROM.begin(EEPROM_SIZE);
    pictureNumber = EEPROM.read(0) + 1;
#endif

#if USE_DEEP_SLEEP
    bootCount++;
    Serial.printf("[SLEEP] Boot #%d\n", bootCount);
#endif

    if (!initCamera()) {
        Serial.println("[CAM] ERREUR FATALE");
#if USE_DEEP_SLEEP
        esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_DURATION_S * 1000000ULL);
        esp_deep_sleep_start();
#endif
        return;
    }

    warmupCamera();

    if (!Wificonnect()) {
#if USE_DEEP_SLEEP
        esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_DURATION_S * 1000000ULL);
        esp_deep_sleep_start();
#endif
        return;
    }

    configTime(GMT_OFFSET, DST_OFFSET, NTP_SERVER);

    // OTA
#if USE_DEEP_SLEEP && defined(OTA_CHECK_EVERY_N)
    otaBootCount++;
    if (otaBootCount >= OTA_CHECK_EVERY_N) {
        otaBootCount = 0;
        checkOta();
    }
#else
    checkOta();
#endif

#if USE_DEEP_SLEEP
    ledBlink(100, 100, 2);
    if (isInPhotoWindow()) {
        sendPhoto();
    } else {
        Serial.println("[NTP] Hors creneau, pas de photo");
    }

    Serial.printf("[SLEEP] Deep sleep %d s...\n", SLEEP_DURATION_S);
    esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_DURATION_S * 1000000ULL);
    esp_deep_sleep_start();
#endif
}

// ═══════════════════════════════════════════════════════════════════
void loop() {
#if USE_DEEP_SLEEP
    // Jamais atteint — deep sleep dans setup()
    delay(1000);
#else
    ledBlink(100, 100, 1);

    unsigned long now = millis();
    if (now - previousMillis >= TIMER_INTERVAL_MS) {
        previousMillis = now;

        if (WiFi.status() != WL_CONNECTED) {
            Wificonnect();
        }

        if (isInPhotoWindow()) {
            sendPhoto();
        }
    }
#endif
}
