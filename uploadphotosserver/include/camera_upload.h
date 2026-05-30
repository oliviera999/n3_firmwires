#ifndef CAMERA_UPLOAD_H
#define CAMERA_UPLOAD_H

#include <Arduino.h>

class MultipartCameraStream : public Stream {
 public:
  MultipartCameraStream(const String& head, const uint8_t* image, size_t imageLen, const String& tail);

  int available() override;
  int read() override;
  int peek() override;
  void flush() override;
  size_t write(uint8_t) override;
  size_t write(const uint8_t*, size_t) override;
  size_t readBytes(char* buffer, size_t length) override;

 private:
  size_t totalSize() const;
  String head_;
  const uint8_t* image_;
  size_t imageLen_;
  String tail_;
  size_t position_;
};

#endif
