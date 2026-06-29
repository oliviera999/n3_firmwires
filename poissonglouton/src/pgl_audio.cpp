#include "pgl_audio.h"



#include <Arduino.h>

#include <cstdio>



#include "config.h"

#include "pgl_log.h"

#include "pgl_sd_storage.h"



namespace {

void formatMp3Path(uint16_t track, char* out, size_t outLen) {

  snprintf(out, outLen, "/mp3/%03u.mp3", track);

}

}  // namespace



#if !PGL_HEADLESS



#include "Audio.h"

#include <SD.h>



// Moteur I2S instancie a la demande (pas de tache decodeur au boot) : la lib

// ESP32-audioI2S demarre une tache FreeRTOS + PSRAM dans le constructeur Audio ;

// concurrente au scan WiFi, elle provoquait Cache MMU fault + reboot en boucle.

Audio* sAudioEngine = nullptr;



bool ensureAudioEngine() {

  if (sAudioEngine != nullptr) {

    return true;

  }

  PGL_LOG_V("Audio: instanciation moteur I2S (BCLK=%d LRCK=%d DOUT=%d)...",

            PGL_I2S_BCLK, PGL_I2S_LRCK, PGL_I2S_DOUT);

  sAudioEngine = new Audio();

  if (sAudioEngine == nullptr) {

    PGL_LOG("Audio: allocation moteur I2S echouee");

    return false;

  }

  if (!sAudioEngine->setPinout(PGL_I2S_BCLK, PGL_I2S_LRCK, PGL_I2S_DOUT)) {

    PGL_LOG("Audio: setPinout I2S echoue");

    delete sAudioEngine;

    sAudioEngine = nullptr;

    return false;

  }

  sAudioEngine->setVolume(PGL_I2S_VOLUME);

  PGL_LOG_V("Audio: moteur I2S pret (vol=%u/21)", static_cast<unsigned>(PGL_I2S_VOLUME));

  return true;

}



void releaseAudioEngine() {

  if (sAudioEngine == nullptr) {

    return;

  }

  sAudioEngine->stopSong();

  delete sAudioEngine;

  sAudioEngine = nullptr;

  PGL_LOG_V("Audio: moteur I2S libere (WiFi/reseau sans conflit PSRAM)");

}



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

      if (i == 1) {

        PGL_LOG("Audio: scan — /mp3/001.mp3 absent (SD montee=%s)",

                pglSdStorageIsMounted() ? "oui" : "non");

      } else {

        PGL_LOG_V("Audio: scan — fin a piste %u (absent)", i);

      }

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

#if !PGL_HEADLESS

  if (sAudioEngine != nullptr) {

    sAudioEngine->stopSong();

    drainAfterPlayback();

    releaseAudioEngine();

  } else {

    audioIdle_ = true;

  }

#endif

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

  if (!pglSdStorageBegin()) {

    PGL_LOG("Audio: montage SD echoue — mode silencieux");

    available_ = false;

    return;

  }

  PGL_LOG_V("Audio: carte SD disponible (moteur I2S lazy — instancie sur detection)");



  trackCount_ = scanMp3TrackCount();

  available_ = true;

  audioIdle_ = true;

  if (trackCount_ == 0) {

    PGL_LOG("Audio: SD OK mais aucune piste /mp3/ detectee");

  } else {

    PGL_LOG("Audio: %u piste(s) /mp3/ — lecture sur detection uniquement", trackCount_);

  }

#endif

}



void PglAudio::poll() {

#if PGL_HEADLESS

  return;

#else

  if (!available_ || sAudioEngine == nullptr) {

    return;

  }

  if (audioIdle_ && !sAudioEngine->isRunning()) {

    return;

  }

  sAudioEngine->loop();

  if (wasPlaying_ && playingTrack_ > 0 && !sAudioEngine->isRunning()) {

    onPlaybackFinished();

  }

#endif

}



void PglAudio::drainAfterPlayback() {

#if PGL_HEADLESS

  return;

#else

  if (sAudioEngine == nullptr) {

    audioIdle_ = true;

    return;

  }

  for (int i = 0; i < 30; ++i) {

    sAudioEngine->loop();

    delay(5);

  }

  audioIdle_ = true;

#endif

}



void PglAudio::playRandomTrack(const char* reason) {

  if (!available_) {

    PGL_LOG_V("Audio: %s ignore — SD absent", reason ? reason : "?");

    return;

  }

  if (trackCount_ == 0) {

    trackCount_ = scanMp3TrackCount();

    if (trackCount_ == 0) {

      PGL_LOG_V("Audio: %s ignore — aucune piste /mp3/", reason ? reason : "?");

      return;

    }

  }



#if !PGL_HEADLESS

  if (sAudioEngine != nullptr && wasPlaying_ && playingTrack_ > 0 &&

      sAudioEngine->isRunning()) {

    PGL_LOG_V("Audio: %s ignore — lecture deja en cours (piste %u)",

              reason ? reason : "?", playingTrack_);

    return;

  }

  if (!ensureAudioEngine()) {

    PGL_LOG("Audio: %s ignore — moteur I2S indisponible", reason ? reason : "?");

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

  if (!SD.exists(playingPath_)) {

    PGL_LOG("Audio: %s ignore — fichier absent %s", reason ? reason : "?", playingPath_);

    trackCount_ = scanMp3TrackCount();

    return;

  }



  playingTrack_ = track;

  strncpy(playingLabel_, reason ? reason : "?", sizeof(playingLabel_) - 1);

  playingLabel_[sizeof(playingLabel_) - 1] = '\0';



  PGL_LOG("Audio: >>> LECTURE [%s] %s (piste %u/%u, vol=%u)",

          playingLabel_, playingPath_, track, trackCount_,

          static_cast<unsigned>(PGL_AUDIO_VOLUME));

  if (notifyFn_) {

    notifyFn_(reason, playingPath_, true, notifyUser_);

  }



  audioIdle_ = false;

  if (sAudioEngine->isRunning()) {

    sAudioEngine->stopSong();

    wasPlaying_ = false;

  }

  sAudioEngine->connecttoFS(SD, playingPath_);

  wasPlaying_ = true;

#endif

}



void PglAudio::playStartup() {

#if PGL_ENABLE_STARTUP_JINGLE

  playRandomTrack("demarrage");

#else

  PGL_LOG_V("Audio: playStartup ignore — PGL_ENABLE_STARTUP_JINGLE=0");

#endif

}



void PglAudio::playThanks() {

  playRandomTrack("remerciement");

}



void PglAudio::scheduleStartupJingle() {

#if PGL_ENABLE_STARTUP_JINGLE

  playStartup();

#endif

}



bool PglAudio::isAvailable() const {

  return available_;

}



bool PglAudio::isPlaying() const {

  return !audioIdle_ && wasPlaying_ && playingTrack_ > 0;

}



bool PglAudio::isBusyForWifi() const {

#if PGL_HEADLESS

  return false;

#else

  if (!available_ || sAudioEngine == nullptr) {

    return false;

  }

  return !audioIdle_ || sAudioEngine->isRunning();

#endif

}



uint16_t PglAudio::getTrackCount() const {

  return trackCount_;

}


