# Draft issue : Boot loop TG0WDT sur ESP32-S3 (Arduino / pioarduino)

À copier-coller ou adapter pour ouvrir une issue sur le dépôt Arduino-ESP32 ou pioarduino/platform-espressif32.

---

**Title:** ESP32-S3: TG0WDT boot loop before initVariant() / setup() (Task WDT timeout ~15–16s)

**Environment**
- Board: 4D Systems GEN4-ESP32 16MB (ESP32-S3-R8N16) or similar ESP32-S3 with PSRAM
- Platform: pioarduino/platform-espressif32 (or PlatformIO with Arduino framework)
- Framework: Arduino (framework-arduinoespressif32)
- ESP-IDF: 5.5.x (as used by the platform)

**Description**

After flashing, the device resets in a loop with `rst:0x7 (TG0WDT_SYS_RST)` (Task Watchdog Timer). The first reset occurs about 15–16 seconds after power-on. No application code runs: there is no serial output (e.g. no "BOOT" message), so `setup()` is never reached.

**Observed**
- Boot: `rst:0x1 (POWERON)`, then after ~16s: `rst:0x7 (TG0WDT_SYS_RST)` repeatedly.
- `initVariant()` is called only at the **end** of `initArduino()`, after `nvs_flash_init()` and `init()`. By that time the Task WDT has already timed out (default 5s in the prebuilt lib, or a longer timeout that triggers around 16s).

**Attempted workarounds (none fully effective)**
1. `CONFIG_ESP_TASK_WDT_INIT=n` and `CONFIG_ESP_TASK_WDT_TIMEOUT_S=300` in custom sdkconfig: the build log shows "Replace: ... CONFIG_ESP_TASK_WDT_INIT ..." and "Compile Arduino IDF libs", but the flashed firmware still hits TG0WDT. So either the recompiled libs are not the ones used at link time, or another code path starts the TWDT.
2. `CONFIG_ESP_TASK_WDT=n` in custom sdkconfig: same result.
3. Calling `esp_task_wdt_deinit()` from a C++ constructor or from `initVariant()`: too late (constructor runs in a context where it doesn’t help, and `initVariant()` runs after the long inits).
4. `ESP_SYSTEM_INIT_FN` hook to call `esp_task_wdt_deinit()`: still getting TG0WDT (hook may run before TWDT is started, or TWDT is started elsewhere).

**Suggested fix (upstream)**

- Call an optional **early init** hook at the **very beginning** of `initArduino()`, **before** `nvs_flash_init()` and `init()`, so that user code can e.g. call `esp_task_wdt_deinit()` or feed the Task WDT before long-running inits. For example:
  - Add a weak symbol `earlyInitVariant(void)` (like `initVariant()` but invoked at the start of `initArduino()`).
  - In `cores/esp32/esp32-hal-misc.c`, at the top of `initArduino()`, call `earlyInitVariant();`.

This allows projects to work around the Task WDT boot timeout on ESP32-S3 (and possibly other targets) without changing the default behaviour of the core.

**Temporary workaround in project**

We patch the Arduino core locally with a script that adds `earlyInitVariant()` and call it at the start of `initArduino()`, and in our app we define `earlyInitVariant()` to call `esp_task_wdt_deinit()`. This needs to be reapplied after each framework update.

---

*Rédigé pour le projet FFP5CS. Voir aussi docs/technical/ESP32S3_TG1WDT_BOOT_FIX.md (section TG0WDT).*
