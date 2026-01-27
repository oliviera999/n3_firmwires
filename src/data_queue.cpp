#include "data_queue.h"
#include <cstdlib>
#include <cstring>

DataQueue::DataQueue(uint16_t maxEntries)
    : _maxEntries(maxEntries > 0 ? maxEntries : 5)
    , _head(0)
    , _tail(0)
    , _count(0)
    , _entries(nullptr)
    , _initialized(false)
{
}

DataQueue::~DataQueue() {
    if (_entries) {
        for (uint16_t i = 0; i < _maxEntries; i++) {
            if (_entries[i]) {
                free(_entries[i]);
            }
        }
        free(_entries);
        _entries = nullptr;
    }
    _initialized = false;
}

bool DataQueue::begin() {
    if (_initialized) {
        return true;
    }
    
    // Allouer le tableau de pointeurs
    _entries = (char**)malloc(_maxEntries * sizeof(char*));
    if (!_entries) {
        Serial.println(F("[DataQueue] ✗ Échec allocation mémoire"));
        return false;
    }
    
    // Allouer les buffers pour chaque entrée
    for (uint16_t i = 0; i < _maxEntries; i++) {
        _entries[i] = (char*)malloc(PAYLOAD_SIZE);
        if (!_entries[i]) {
            // Libérer ce qui a été alloué
            for (uint16_t j = 0; j < i; j++) {
                free(_entries[j]);
            }
            free(_entries);
            _entries = nullptr;
            Serial.println(F("[DataQueue] ✗ Échec allocation buffers"));
            return false;
        }
        _entries[i][0] = '\0';  // Initialiser à vide
    }
    
    _initialized = true;
    Serial.printf("[DataQueue] ✓ Queue RAM initialisée (%u entrées max)\n", _maxEntries);
    return true;
}

bool DataQueue::push(const char* payload) {
    if (!_initialized || !_entries) {
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
    
    // Si queue pleine, écraser la plus ancienne (FIFO)
    if (_count >= _maxEntries) {
        // Avancer tail pour écraser l'entrée la plus ancienne
        _tail = (_tail + 1) % _maxEntries;
        _count = _maxEntries;  // Reste à max
    } else {
        _count++;
    }
    
    // Copier le payload dans la position head
    strncpy(_entries[_head], payload, PAYLOAD_SIZE - 1);
    _entries[_head][PAYLOAD_SIZE - 1] = '\0';
    
    // Avancer head
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
    
    // Supprimer l'entrée en avançant tail
    _entries[_tail][0] = '\0';  // Marquer comme vide
    _tail = (_tail + 1) % _maxEntries;
    _count--;
    
    return true;
}

bool DataQueue::peek(char* buffer, size_t bufferSize) {
    if (buffer == nullptr || bufferSize == 0) {
        return false;
    }

    if (!_initialized || !_entries || _count == 0) {
        buffer[0] = '\0';
        return false;
    }
    
    // Copier l'entrée à la position tail
    size_t len = strlen(_entries[_tail]);
    if (len >= bufferSize) {
        len = bufferSize - 1;
    }
    strncpy(buffer, _entries[_tail], len);
    buffer[len] = '\0';
    
    return true;
}

uint16_t DataQueue::size() {
    return _count;
}

void DataQueue::clear() {
    if (!_initialized || !_entries) {
        return;
    }
    
    // Vider tous les buffers
    for (uint16_t i = 0; i < _maxEntries; i++) {
        _entries[i][0] = '\0';
    }
    
    _head = 0;
    _tail = 0;
    _count = 0;
}

size_t DataQueue::getMemoryUsage() {
    if (!_initialized || !_entries) {
        return 0;
    }
    
    size_t total = 0;
    for (uint16_t i = 0; i < _maxEntries; i++) {
        if (_entries[i] && _entries[i][0] != '\0') {
            total += strlen(_entries[i]);
        }
    }
    
    return total;
}

bool DataQueue::isFull() {
    return _count >= _maxEntries;
}

bool DataQueue::isEmpty() {
    return _count == 0;
}

