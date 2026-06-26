#pragma once

class PglSleep {
 public:
  void configure(bool useIrWakeup, unsigned long timerSeconds);
  // Reveil EXT0 selon le capteur present : IR (GPIO PGL_IR_PIN, niveau bas) ou
  // PIR (GPIO PGL_PIR_PIN, niveau haut, actif-HAUT). Si aucun : timer seul.
  // EXT0 = une seule broche ; le reveil multi-broches (ext1) est une evolution.
  void configureWakeup(bool useIrWakeup, bool usePirWakeup, unsigned long timerSeconds);
  void start();
};
