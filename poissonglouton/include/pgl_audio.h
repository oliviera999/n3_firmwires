#pragma once

#include <Arduino.h>

using PglAudioNotifyFn = void (*)(const char* reason, const char* mp3Path, bool started, void* userData);

class PglAudio {
 public:
  void begin();
  void poll();
  void playThanks();
  void playStartup();
  bool isAvailable() const;
  uint16_t getTrackCount() const;
  void setNotifyCallback(PglAudioNotifyFn fn, void* userData = nullptr);

 private:
  void playRandomTrack(const char* reason);
  uint16_t scanMp3TrackCount();
  void onPlaybackFinished();

  bool available_ = false;
  uint16_t trackCount_ = 0;
  uint16_t lastTrack_ = 0;
  uint16_t playingTrack_ = 0;
  bool wasPlaying_ = false;
  char playingLabel_[20] = {};
  char playingPath_[24] = {};
  PglAudioNotifyFn notifyFn_ = nullptr;
  void* notifyUser_ = nullptr;
};
