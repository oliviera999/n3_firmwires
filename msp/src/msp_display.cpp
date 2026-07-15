/* MeteoStationPrototype (msp1) — Affichage OLED
 * affichageOLED
 */

#include "msp_display.h"
#include "msp_config.h"
#include "msp_globals.h"
#include <WiFi.h>

static const uint16_t OLED_PAGE_DELAY_MS = 500;

// Avant v2.42 : 3 boucles de 6 pages OLED (~18 x 500 ms = 9 s) → cumule avec le
// scan tracker solaire et risquait de declencher le WDT (CONFIG_ESP_TASK_WDT_TIMEOUT_S=30).
// Maintenant : une seule page par section, plus de risque de cumul.
void affichageOLED() {
  if (!displayOk) return;

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("    msp1 ");
  display.print(" v");
  display.println(version);
  display.println(WiFi.SSID());
  display.println(WiFi.localIP());
  display.print("La:");
  display.print(photocellReadingA);
  display.print(" Lb:");
  display.println(photocellReadingB);
  display.print("Lc:");
  display.print(photocellReadingC);
  display.print(" Ld:");
  display.println(photocellReadingD);
  display.print("Lm:");
  display.println(photocellReadingMoy);
  display.print("pd:");
  display.print(batteryVoltage);
  display.println("V");
  display.print(rtc.getTime("%H:%M:%S %d/%m/%Y"));
  display.display();
  delay(OLED_PAGE_DELAY_MS);

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("HS:");
  display.println(HumidSol);  // globale filtree (LectureCapteurs), coherente avec le POST
  display.print("P:");
  display.println(Pluie);  // globale filtree + sentinelle deconnexion, coherente avec le POST
  display.print("TS:");
  display.print(temperatureSol, 1);
  display.cp437(true);
  display.write(167);
  display.println("C");
  display.print("t:");
  display.print(tempAirInt, 1);
  display.cp437(true);
  display.write(167);
  display.print("C");
  display.print(" H:");
  display.print(humidAirInt, 1);
  display.println("% ");
  display.print(tempAirExt, 1);
  display.cp437(true);
  display.write(167);
  display.print("C");
  display.print(" H:");
  display.print(humidAirExt, 1);
  display.println("% ");
  display.print(" pd:");
  display.println(batteryVoltage);
  display.print(rtc.getTime("%H:%M:%S %d/%m/%Y"));
  display.display();
  delay(OLED_PAGE_DELAY_MS);

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(" variables");
  display.print(" SSec:");
  display.print(SeuilSec);
  display.print(" Spd:");
  display.println(SeuilPontDiv);
  display.print("WakeUp:");
  display.print(WakeUp);
  display.print(" FWakeUp:");
  display.println(FreqWakeUp);
  display.print(" sHB:");
  display.print(AngleServoHB);
  display.print(" sGD:");
  display.println(AngleServoGD);
  display.print(" eRelais:");
  display.println(etatRelais);
  display.print("resetM: ");
  display.println(resetMode);
  display.print("pd:");
  display.print(batteryVoltage);
  display.print(rtc.getTime("%H:%M:%S %d/%m/%Y"));
  display.display();
  delay(OLED_PAGE_DELAY_MS);
}
