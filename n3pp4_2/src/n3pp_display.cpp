#include "n3pp_display.h"
#include "n3pp_globals.h"

void affichageOLED() {
  if (!displayOk) return;
  display.clearDisplay();
  //display.setTextColor(0xFFE0);
  display.setTextSize(1);
  display.setCursor(0, 0);
  //affichage version
  display.print("    n3pp ");
  display.print(" v");
  display.println(version);
  //display.setTextColor(0x07FF);
  // nom réseau Wifi
  display.println(WiFi.SSID());
  //IP réseau Wifi
  display.println(WiFi.localIP());
  /*display.print ("IP AP ");
  display.println(WiFi.softAPIP());*/
  // display temperature
  display.print("h1:");
  display.print(Humid1);
  //display eau aquarium
  display.print(" h2:");
  display.println(Humid2);
  //display eau réserve
  display.print("h3:");
  display.print(Humid3);
  //display eau potager
  display.print(" h4:");
  display.println(Humid4);
  //display eau potager
  display.print("hm:");
  display.print(HumidMoy);
  // display Luminosité
  display.print(" L:");
  display.println(photocellReading);
  //display eau potager
  display.print(" pd:");
  display.println(PontDiv);
  //display humidité
  /*  display.print("t:");
  display.print(temperatureAir,1);
  display.cp437(true);
  display.write(167);
  display.print("C");
  display.print(" H:");
  display.print(h,1);
  display.println("% "); */
  //display.display();
  //rtc.adjust(DateTime(__DATE__, __TIME__));
  // display heure et date
  //display.setTextColor(0xF800);
  //DateTime now = rtc.now();
  display.print(rtc.getTime("%H:%M:%S %d/%m/%Y"));
  Serial.print(rtc.getTime("%H:%M:%S %d/%m/%Y"));

  display.display();
  delay(4000);

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  //affichage variables
  display.println(" variables");
  display.print("HeureArros:");
  display.print(HeureArrosage);
  display.print(" tpsArros:");
  display.println(tempsArrosageSec);
  display.print(" SSec:");
  display.print(SeuilSec);
  display.print(" Spd:");
  display.println(SeuilPontDiv);
  display.print("WakeUp:");
  display.print(WakeUp);
  //Heures de nourrisage
  display.print(" FWakeUp:");
  display.println(FreqWakeUp);

  //Temps de nourrisage
  display.print("Pompe:");
  display.print(etatPompe);
  //Temps de nourrisage
  display.print(" eRelais:");
  display.println(etatRelais);
  display.print("resetM: ");
  display.print(resetMode);
  display.display();
  delay(4000);
}
