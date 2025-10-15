#include "data_queue.h"

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
    if (!LittleFS.begin(false)) {
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

bool DataQueue::push(const String& payload) {
    if (!_initialized) {
        Serial.println(F("[DataQueue] ✗ Non initialisée"));
        return false;
    }
    
    if (payload.length() == 0) {
        Serial.println(F("[DataQueue] ✗ Payload vide"));
        return false;
    }
    
    // Vérifier limite mémoire
    if (isFull()) {
        Serial.println(F("[DataQueue] ⚠️ Queue pleine, rotation..."));
        rotateIfNeeded();
    }
    
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
    Serial.printf("[DataQueue] ✓ Payload enregistré (%u bytes, total: %u entrées)\n", 
                  payload.length(), _currentSize);
    
    return true;
}

String DataQueue::pop() {
    String first = peek();
    if (first.length() == 0) {
        return first;
    }
    
    // Ouvrir fichier source
    File src = LittleFS.open(QUEUE_FILE, FILE_READ);
    if (!src) {
        Serial.println(F("[DataQueue] ✗ Échec lecture fichier"));
        return String();
    }
    
    // Créer fichier temporaire
    File tmp = LittleFS.open(TEMP_FILE, FILE_WRITE);
    if (!tmp) {
        Serial.println(F("[DataQueue] ✗ Échec création fichier temp"));
        src.close();
        return String();
    }
    
    // Copier toutes les lignes sauf la première
    bool firstLine = true;
    while (src.available()) {
        String line = src.readStringUntil('\n');
        if (!firstLine && line.length() > 0) {
            tmp.println(line);
        }
        firstLine = false;
    }
    
    src.close();
    tmp.close();
    
    // Remplacer fichier original par le temp
    LittleFS.remove(QUEUE_FILE);
    LittleFS.rename(TEMP_FILE, QUEUE_FILE);
    
    _currentSize--;
    Serial.printf("[DataQueue] ✓ Payload supprimé (restant: %u)\n", _currentSize);
    
    return first;
}

String DataQueue::peek() {
    if (!_initialized) {
        return String();
    }
    
    File file = LittleFS.open(QUEUE_FILE, FILE_READ);
    if (!file) {
        return String();
    }
    
    // Lire première ligne
    String first;
    if (file.available()) {
        first = file.readStringUntil('\n');
        first.trim(); // Enlever \r\n
    }
    
    file.close();
    return first;
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
    while (file.available()) {
        String line = file.readStringUntil('\n');
        if (line.length() > 0) {
            count++;
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

