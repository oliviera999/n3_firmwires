#include "pgl_audio.h"

#include <Arduino.h>
#include <cstdio>

#include "config.h"
#include "pgl_log.h"

namespace {
void formatMp3Path(uint16_t track, char* out, size_t outLen) {
  snprintf(out, outLen, "/mp3/%03u.mp3", track);
}
}  // namespace

#if !PGL_HEADLESS

#include "Audio.h"
#include <SD.h>
#include <SPI.h>

namespace {
SPIClass spiSD(HSPI);
Audio audio;
}  // namespace

#endif

void PglAudio::setNotifyCallback(PglAudioNotifyFn fn, void* userData) {
  notifyFn_ = fn;
  notifyUser_ = userData;
}

uint16_t PglAudio::scanMp3TrackCount() {
#if PGL_HEADLESS
  return 0;
#else
  uint16_t count = 0;
  for (uint16_t i = 1; i <= PGL_AUDIO_MAX_TRACKS; ++i) {
    char path[24];
    formatMp3Path(i, path, sizeof(path));
    if (!SD.exists(path)) {
      PGL_LOG_V("Audio: scan — fin a piste %u (absent)", i);
      break;
    }
    count = i;
    PGL_LOG_V("Audio: scan piste %u — OK", i);
  }
  return count;
#endif
}

void PglAudio::onPlaybackFinished() {
  if (playingTrack_ == 0) {
    return;
  }
  PGL_LOG("Audio: <<< FIN [%s] %s (piste %u)",
          playingLabel_[0] != '\0' ? playingLabel_ : "lecture",
          playingPath_,
          playingTrack_);
  playingTrack_ = 0;
  playingLabel_[0] = '\0';
  playingPath_[0] = '\0';
  wasPlaying_ = false;
  if (notifyFn_) {
    notifyFn_(nullptr, nullptr, false, notifyUser_);
  }
}

void PglAudio::begin() {
#if PGL_HEADLESS
  PGL_LOG("Audio: headless — mode silencieux");
  available_ = false;
  return;
#else
  PGL_LOG_V("Audio: init I2S (BCLK=%d LRCK=%d DOUT=%d) + SD (CS=%d)...",
            PGL_I2S_BCLK, PGL_I2S_LRCK, PGL_I2S_DOUT, PGL_SD_CS);

  spiSD.begin(PGL_SD_SCK, PGL_SD_MISO, PGL_SD_MOSI, PGL_SD_CS);
  if (!SD.begin(PGL_SD_CS, spiSD, 10000000)) {
    PGL_LOG("Audio: montage SD echoue — mode silencieux");
    available_ = false;
    return;
  }
  PGL_LOG_V("Audio: carte SD montee");

  audio.setPinout(PGL_I2S_BCLK, PGL_I2S_LRCK, PGL_I2S_DOUT);
  audio.setVolume(PGL_I2S_VOLUME);
  PGL_LOG_V("Audio: volume I2S=%u/21", static_cast<unsigned>(PGL_I2S_VOLUME));

  trackCount_ = scanMp3TrackCount();
  available_ = true;
  if (trackCount_ == 0) {
    PGL_LOG("Audio: I2S pret mais aucune piste /mp3/ detectee");
  } else {
    PGL_LOG("Audio: I2S pret — %u piste(s) dans /mp3/", trackCount_);
  }
#endif
}

void PglAudio::poll() {
#if PGL_HEADLESS
  return;
#else
  if (!available_) {
    return;
  }
  audio.loop();
  if (wasPlaying_ && playingTrack_ > 0 && !audio.isRunning()) {
    onPlaybackFinished();
  }
#endif
}

void PglAudio::playRandomTrack(const char* reason) {
  if (!available_) {
    PGL_LOG_V("Audio: %s ignore — I2S/SD absent", reason);
    return;
  }
  if (trackCount_ == 0) {
    PGL_LOG_V("Audio: %s ignore — aucune piste /mp3/", reason);
    return;
  }

  uint16_t track = 1;
  if (trackCount_ == 1) {
    track = 1;
  } else {
    track = static_cast<uint16_t>((esp_random() % trackCount_) + 1);
    if (track == lastTrack_) {
      track = static_cast<uint16_t>((track % trackCount_) + 1);
    }
  }
  lastTrack_ = track;

  formatMp3Path(track, playingPath_, sizeof(playingPath_));
  playingTrack_ = track;
  strncpy(playingLabel_, reason, sizeof(playingLabel_) - 1);
  playingLabel_[sizeof(playingLabel_) - 1] = '\0';

  PGL_LOG("Audio: >>> LECTURE [%s] %s (piste %u/%u, vol=%u)",
          reason, playingPath_, track, trackCount_,
          static_cast<unsigned>(PGL_AUDIO_VOLUME));
  if (notifyFn_) {
    notifyFn_(reason, playingPath_, true, notifyUser_);
  }

#if !PGL_HEADLESS
  if (audio.isRunning()) {
    audio.stopSong();
    wasPlaying_ = false;
  }
  audio.connecttoFS(SD, playingPath_);
  wasPlaying_ = true;
#endif
}

void PglAudio::playStartup() {
  playRandomTrack("demarrage");
}

void PglAudio::playThanks() {
  playRandomTrack("remerciement");
}

bool PglAudio::isAvailable() const {
  return available_;
}

uint16_t PglAudio::getTrackCount() const {
  return trackCount_;
}
