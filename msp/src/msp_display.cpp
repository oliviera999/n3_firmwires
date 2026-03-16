/* MeteoStationPrototype (msp1) — Affichage OLED
 * affichageOLED
 */

#include "msp_display.h"
#include "msp_config.h"
#include "msp_globals.h"
#include <WiFi.h>

void affichageOLED() {
  if (!displayOk) return;
  for (int i = 0; i < 6; i++) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("    msp1 ");  // Affichage version et réseau WiFi
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
    delay(1000);
  }
  for (int i = 0; i < 6; i++) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("HS:");
    display.println(analogRead(HumiditeSol));
    display.print("P:");
    display.println(analogRead(27));
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
    delay(1000);
  }
  for (int i = 0; i < 6; i++) {
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
    delay(1000);
  }
}
