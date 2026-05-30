#include "pgl_audio.h"

#include <Arduino.h>

#include "pgl_log.h"

void PglAudio::begin(HardwareSerial& serialPort) {
  available_ = player_.begin(serialPort, true, false);
  if (!available_) {
    PGL_LOG("Audio: DFPlayer absent — mode silencieux");
    return;
  }

  player_.volume(22);
  player_.EQ(DFPLAYER_EQ_NORMAL);
  PGL_LOG("Audio: DFPlayer pret (volume 22)");
}

void PglAudio::playThanks() {
  if (!available_) return;

  // 001..010 mp3 sur carte SD
  uint16_t track = static_cast<uint16_t>((esp_random() % 10) + 1);
  if (track == lastTrack_) {
    track = static_cast<uint16_t>((track % 10) + 1);
  }
  lastTrack_ = track;
  PGL_LOG_V("Audio: lecture piste %03u.mp3", track);
  player_.playMp3Folder(track);
}

bool PglAudio::isAvailable() const {
  return available_;
}
