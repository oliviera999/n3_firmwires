#include "camera_upload.h"

#include <climits>
#include <cstring>

#include "config.h"

MultipartCameraStream::MultipartCameraStream(const String& head, const uint8_t* image, size_t imageLen, const String& tail)
    : head_(head), image_(image), imageLen_(imageLen), tail_(tail), position_(0) {}

int MultipartCameraStream::available() {
  const size_t remaining = totalSize() - position_;
  return remaining > static_cast<size_t>(INT_MAX) ? INT_MAX : static_cast<int>(remaining);
}

int MultipartCameraStream::read() {
  uint8_t b = 0;
  return readBytes(reinterpret_cast<char*>(&b), 1) == 1 ? b : -1;
}

int MultipartCameraStream::peek() {
  return -1;
}

void MultipartCameraStream::flush() {}

size_t MultipartCameraStream::write(uint8_t) { return 0; }
size_t MultipartCameraStream::write(const uint8_t*, size_t) { return 0; }

size_t MultipartCameraStream::readBytes(char* buffer, size_t length) {
  if (!buffer || length == 0 || position_ >= totalSize()) return 0;
  const size_t remain = totalSize() - position_;
  if (length > remain) length = remain;
  size_t copied = 0;
  while (copied < length) {
    const size_t pos = position_;
    if (pos < head_.length()) {
      const size_t n = min(length - copied, head_.length() - pos);
      memcpy(buffer + copied, head_.c_str() + pos, n);
      copied += n;
      position_ += n;
    } else if (pos < head_.length() + imageLen_) {
      const size_t imagePos = pos - head_.length();
      const size_t n = min(length - copied, imageLen_ - imagePos);
      memcpy(buffer + copied, image_ + imagePos, n);
      copied += n;
      position_ += n;
    } else {
      const size_t tailPos = pos - head_.length() - imageLen_;
      const size_t n = min(length - copied, tail_.length() - tailPos);
      memcpy(buffer + copied, tail_.c_str() + tailPos, n);
      copied += n;
      position_ += n;
    }
  }
  return copied;
}

size_t MultipartCameraStream::totalSize() const {
  return head_.length() + imageLen_ + tail_.length();
}

MultipartFileStream::MultipartFileStream(const String& head, File& file, size_t fileLen, const String& tail)
    : head_(head), file_(file), fileLen_(fileLen), tail_(tail), position_(0) {}

int MultipartFileStream::available() {
  const size_t remaining = totalSize() - position_;
  return remaining > static_cast<size_t>(INT_MAX) ? INT_MAX : static_cast<int>(remaining);
}

int MultipartFileStream::read() {
  uint8_t b = 0;
  return readBytes(reinterpret_cast<char*>(&b), 1) == 1 ? b : -1;
}

int MultipartFileStream::peek() {
  return -1;
}

void MultipartFileStream::flush() {}

size_t MultipartFileStream::write(uint8_t) { return 0; }
size_t MultipartFileStream::write(const uint8_t*, size_t) { return 0; }

size_t MultipartFileStream::readBytes(char* buffer, size_t length) {
  if (!buffer || length == 0 || position_ >= totalSize()) return 0;
  const size_t remain = totalSize() - position_;
  if (length > remain) length = remain;
  size_t copied = 0;
  while (copied < length) {
    const size_t pos = position_;
    if (pos < head_.length()) {
      const size_t n = min(length - copied, head_.length() - pos);
      memcpy(buffer + copied, head_.c_str() + pos, n);
      copied += n;
      position_ += n;
    } else if (pos < head_.length() + fileLen_) {
      const size_t fileRemaining = head_.length() + fileLen_ - pos;
      const size_t want = min(length - copied, fileRemaining);
      const size_t chunk = min(want, static_cast<size_t>(UPLOAD_CHUNK_SIZE));
      const size_t n = file_.read(reinterpret_cast<uint8_t*>(buffer + copied), chunk);
      if (n == 0) break;
      copied += n;
      position_ += n;
    } else {
      const size_t tailPos = pos - head_.length() - fileLen_;
      const size_t n = min(length - copied, tail_.length() - tailPos);
      memcpy(buffer + copied, tail_.c_str() + tailPos, n);
      copied += n;
      position_ += n;
    }
  }
  return copied;
}

size_t MultipartFileStream::totalSize() const {
  return head_.length() + fileLen_ + tail_.length();
}
