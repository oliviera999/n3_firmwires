#include "data_queue.h"
#include <cstring>

DataQueue::DataQueue(uint16_t maxEntries)
    : _maxEntries(0)
    , _head(0)
    , _tail(0)
    , _count(0)
    , _initialized(false)
{
    if (maxEntries == 0) {
        _maxEntries = 5;
    } else if (maxEntries > DATA_QUEUE_MAX_ENTRIES) {
        _maxEntries = DATA_QUEUE_MAX_ENTRIES;
    } else {
        _maxEntries = maxEntries;
    }
}

DataQueue::~DataQueue() {
    _initialized = false;
}

bool DataQueue::begin() {
    if (_initialized) {
        return true;
    }
    for (uint16_t i = 0; i < _maxEntries; i++) {
        _storage[i][0] = '\0';
    }
    _initialized = true;
    Serial.printf("[DataQueue] ✓ Queue RAM initialisée (%u entrées max)\n", _maxEntries);
    return true;
}

bool DataQueue::push(const char* payload) {
    if (!_initialized) {
        return false;
    }
    if (payload == nullptr || strlen(payload) == 0) {
        return false;
    }
    size_t payloadLen = strlen(payload);
    if (payloadLen >= PAYLOAD_SIZE) {
        Serial.println(F("[DataQueue] ✗ Payload trop grand"));
        return false;
    }
    if (_count >= _maxEntries) {
        Serial.println(F("[DataQueue] Ecrasement entree ancienne (queue pleine)"));
        _tail = (_tail + 1) % _maxEntries;
        _count = _maxEntries;
    } else {
        _count++;
    }
    strncpy(_storage[_head], payload, PAYLOAD_SIZE - 1);
    _storage[_head][PAYLOAD_SIZE - 1] = '\0';
    _head = (_head + 1) % _maxEntries;
    return true;
}

bool DataQueue::pop(char* buffer, size_t bufferSize) {
    if (buffer == nullptr || bufferSize == 0) {
        return false;
    }

    if (!peek(buffer, bufferSize)) {
        return false;
    }
    
    _storage[_tail][0] = '\0';
    _tail = (_tail + 1) % _maxEntries;
    _count--;
    
    return true;
}

bool DataQueue::peek(char* buffer, size_t bufferSize) {
    if (buffer == nullptr || bufferSize == 0) {
        return false;
    }

    if (!_initialized || _count == 0) {
        buffer[0] = '\0';
        return false;
    }
    size_t len = strlen(_storage[_tail]);
    if (len >= bufferSize) {
        len = bufferSize - 1;
    }
    strncpy(buffer, _storage[_tail], len);
    buffer[len] = '\0';
    
    return true;
}

uint16_t DataQueue::size() {
    return _count;
}

void DataQueue::clear() {
    if (!_initialized) {
        return;
    }
    for (uint16_t i = 0; i < _maxEntries; i++) {
        _storage[i][0] = '\0';
    }
    _head = 0;
    _tail = 0;
    _count = 0;
}

