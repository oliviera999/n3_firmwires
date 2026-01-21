#include "data_queue.h"

namespace {
const char* kLittleFsLabel = "littlefs";
}

DataQueue::DataQueue(uint16_t maxEntries)
    : _maxEntries(maxEntries)
    , _currentSize(0)
    , _initialized(false)
{
}

bool DataQueue::begin() {
    if (_initialized) {
        return true;
    }
    
    // Monter LittleFS si pas déjà fait
    if (!LittleFS.begin(false, "/littlefs", 10, kLittleFsLabel)) {
        Serial.println(F("[DataQueue] ✗ Échec montage LittleFS"));
        return false;
    }
    
    // Créer répertoire si nécessaire
    if (!ensureDirectoryExists()) {
        Serial.println(F("[DataQueue] ✗ Échec création répertoire"));
        return false;
    }
    
    // Compter les entrées existantes
    _currentSize = countEntries();
    _initialized = true;
    
    Serial.printf("[DataQueue] ✓ Initialisée: %u entrées en attente\n", _currentSize);
    return true;
}

bool DataQueue::ensureDirectoryExists() {
    if (!LittleFS.exists(QUEUE_DIR)) {
        return LittleFS.mkdir(QUEUE_DIR);
    }
    return true;
}

bool DataQueue::push(const char* payload) {
    if (!_initialized) {
        Serial.println(F("[DataQueue] ✗ Non initialisée"));
        return false;
    }
    
    if (payload == nullptr || strlen(payload) == 0) {
        Serial.println(F("[DataQueue] ✗ Payload vide"));
        return false;
    }
    
    // Vérifier limite mémoire
    if (isFull()) {
        Serial.println(F("[DataQueue] ⚠️ Queue pleine, rotation..."));
        rotateIfNeeded();
    }
    
    uint32_t heapBefore = ESP.getFreeHeap();
    // Ouvrir fichier en append
    File file = LittleFS.open(QUEUE_FILE, FILE_APPEND);
    if (!file) {
        Serial.println(F("[DataQueue] ✗ Échec ouverture fichier"));
        return false;
    }
    
    // Écrire payload + newline
    file.println(payload);
    file.close();
    
    _currentSize++;
    Serial.printf("[DataQueue] ✓ Payload enregistré (%zu bytes, total: %u entrées)\n", 
                  strlen(payload), _currentSize);

    uint32_t heapAfter = ESP.getFreeHeap();
    int32_t heapDelta = static_cast<int32_t>(heapAfter) - static_cast<int32_t>(heapBefore);
    if (heapDelta != 0) {
        Serial.printf("[DataQueue] 📊 Heap delta push: %d bytes (avant=%u, après=%u)\n",
                      heapDelta, heapBefore, heapAfter);
    }
    
    return true;
}

bool DataQueue::pop(char* buffer, size_t bufferSize) {
    if (buffer == nullptr || bufferSize == 0) {
        return false;
    }

    if (!peek(buffer, bufferSize)) {
        return false;
    }
    
    uint32_t heapBefore = ESP.getFreeHeap();
    
    // Ouvrir fichier source
    File src = LittleFS.open(QUEUE_FILE, FILE_READ);
    if (!src) {
        Serial.println(F("[DataQueue] ✗ Échec lecture fichier"));
        return false;
    }
    
    // Créer fichier temporaire
    File tmp = LittleFS.open(TEMP_FILE, FILE_WRITE);
    if (!tmp) {
        Serial.println(F("[DataQueue] ✗ Échec création fichier temp"));
        src.close();
        return false;
    }
    
    // v11.156: Utilisation de buffer fixe au lieu de readStringUntil() pour éviter fragmentation mémoire
    const size_t LINE_BUF_SIZE = 512;  // Taille max raisonnable pour une ligne JSON
    char lineBuf[LINE_BUF_SIZE];
    bool firstLine = true;
    
    while (src.available()) {
        size_t len = src.readBytesUntil('\n', lineBuf, LINE_BUF_SIZE - 1);
        if (len > 0) {
            lineBuf[len] = '\0';
            // Supprimer \r si présent (format Windows)
            if (len > 0 && lineBuf[len - 1] == '\r') {
                lineBuf[len - 1] = '\0';
                len--;
            }
            
            if (!firstLine && len > 0) {
                tmp.println(lineBuf);
            }
            firstLine = false;
        } else {
            // Fin de fichier ou ligne vide
            break;
        }
    }
    
    src.close();
    tmp.close();
    
    // Remplacer fichier original par le temp
    LittleFS.remove(QUEUE_FILE);
    LittleFS.rename(TEMP_FILE, QUEUE_FILE);
    
    _currentSize--;
    Serial.printf("[DataQueue] ✓ Payload supprimé (restant: %u)\n", _currentSize);

    uint32_t heapAfter = ESP.getFreeHeap();
    int32_t heapDelta = static_cast<int32_t>(heapAfter) - static_cast<int32_t>(heapBefore);
    if (heapDelta != 0) {
        Serial.printf("[DataQueue] 📉 Heap delta pop: %d bytes (avant=%u, après=%u)\n",
                      heapDelta, heapBefore, heapAfter);
    }
    
    return true;
}

bool DataQueue::peek(char* buffer, size_t bufferSize) {
    if (buffer == nullptr || bufferSize == 0) {
        return false;
    }

    if (!_initialized) {
        buffer[0] = '\0';
        return false;
    }
    
    File file = LittleFS.open(QUEUE_FILE, FILE_READ);
    if (!file) {
        buffer[0] = '\0';
        return false;
    }
    
    // Lire première ligne
    if (file.available()) {
        size_t len = file.readBytesUntil('\n', buffer, bufferSize - 1);
        if (len > 0) {
            buffer[len] = '\0';
            // Supprimer \r si présent (format Windows)
            if (len > 0 && buffer[len - 1] == '\r') {
                buffer[len - 1] = '\0';
            }
            
            // Supprimer espaces blancs
            size_t actualLen = strlen(buffer);
            while (actualLen > 0 && (buffer[actualLen - 1] == ' ' || buffer[actualLen - 1] == '\t')) {
                buffer[actualLen - 1] = '\0';
                actualLen--;
            }
            
            file.close();
            return true;
        }
    }
    
    buffer[0] = '\0';
    file.close();
    return false;
}

uint16_t DataQueue::size() {
    return _currentSize;
}

void DataQueue::clear() {
    if (!_initialized) {
        return;
    }
    
    LittleFS.remove(QUEUE_FILE);
    _currentSize = 0;
    
    Serial.println(F("[DataQueue] ✓ Queue vidée"));
}

uint16_t DataQueue::countEntries() {
    File file = LittleFS.open(QUEUE_FILE, FILE_READ);
    if (!file) {
        return 0;
    }
    
    uint16_t count = 0;
    const size_t LINE_BUF_SIZE = 512;
    char lineBuf[LINE_BUF_SIZE];
    while (file.available()) {
        size_t len = file.readBytesUntil('\n', lineBuf, LINE_BUF_SIZE - 1);
        if (len > 0) {
            lineBuf[len] = '\0';
            // Supprimer \r si présent
            if (len > 0 && lineBuf[len - 1] == '\r') {
                lineBuf[len - 1] = '\0';
                len--;
            }
            if (len > 0) {
                count++;
            }
        }
    }
    
    file.close();
    return count;
}

void DataQueue::rotateIfNeeded() {
    if (_currentSize <= _maxEntries) {
        return;
    }
    
    // Supprimer les entrées les plus anciennes
    uint16_t toRemove = _currentSize - _maxEntries;
    
    File src = LittleFS.open(QUEUE_FILE, FILE_READ);
    if (!src) {
        return;
    }
    
    File tmp = LittleFS.open(TEMP_FILE, FILE_WRITE);
    if (!tmp) {
        src.close();
        return;
    }
    
    // Sauter les N premières lignes
    uint16_t skipped = 0;
    while (src.available() && skipped < toRemove) {
        src.readStringUntil('\n');
        skipped++;
    }
    
    // Copier le reste
    while (src.available()) {
        String line = src.readStringUntil('\n');
        if (line.length() > 0) {
            tmp.println(line);
        }
    }
    
    src.close();
    tmp.close();
    
    LittleFS.remove(QUEUE_FILE);
    LittleFS.rename(TEMP_FILE, QUEUE_FILE);
    
    _currentSize = _maxEntries;
    
    Serial.printf("[DataQueue] ⚠️ Rotation effectuée: %u entrées supprimées\n", toRemove);
}

size_t DataQueue::getMemoryUsage() {
    if (!LittleFS.exists(QUEUE_FILE)) {
        return 0;
    }
    
    File file = LittleFS.open(QUEUE_FILE, FILE_READ);
    if (!file) {
        return 0;
    }
    
    size_t size = file.size();
    file.close();
    
    return size;
}

bool DataQueue::isFull() {
    return _currentSize >= _maxEntries;
}

bool DataQueue::isEmpty() {
    return _currentSize == 0;
}

