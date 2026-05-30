#include "n3_outputs_json.h"
#include <stdlib.h>

namespace N3Outputs {

int readIntByKey(JSONVar& obj, const char* key, int defaultValue) {
    if (!obj.hasOwnProperty(key)) {
        return defaultValue;
    }
    JSONVar val = obj[key];
    String valueType = JSON.typeof(val);
    if (valueType == "undefined" || valueType == "null") {
        return defaultValue;
    }
    if (valueType == "number" || valueType == "boolean") {
        return (int)val;
    }
    if (valueType == "string") {
        const char* raw = (const char*)val;
        if (raw == nullptr || raw[0] == '\0') {
            return defaultValue;
        }
        return atoi(raw);
    }
    return defaultValue;
}

bool tryReadIntByKey(JSONVar& obj, const char* key, int* outValue) {
    if (outValue == nullptr) return false;
    if (!obj.hasOwnProperty(key)) {
        return false;
    }
    JSONVar val = obj[key];
    String valueType = JSON.typeof(val);
    if (valueType == "undefined" || valueType == "null") {
        return false;
    }
    if (valueType == "number" || valueType == "boolean") {
        *outValue = (int)val;
        return true;
    }
    if (valueType == "string") {
        const char* raw = (const char*)val;
        if (raw == nullptr) {
            return false;
        }
        char* endPtr = nullptr;
        long parsed = strtol(raw, &endPtr, 10);
        if (endPtr == raw) {
            return false;
        }
        while (endPtr != nullptr && (*endPtr == ' ' || *endPtr == '\t' ||
                                     *endPtr == '\r' || *endPtr == '\n')) {
            ++endPtr;
        }
        if (endPtr != nullptr && *endPtr != '\0') {
            return false;
        }
        *outValue = (int)parsed;
        return true;
    }
    return false;
}

String readStringByKey(JSONVar& obj, const char* key, const String& defaultValue) {
    if (!obj.hasOwnProperty(key)) {
        return defaultValue;
    }
    JSONVar val = obj[key];
    String valueType = JSON.typeof(val);
    if (valueType == "undefined" || valueType == "null") {
        return defaultValue;
    }
    if (valueType == "string") {
        const char* raw = (const char*)val;
        if (raw == nullptr) {
            return defaultValue;
        }
        return String(raw);
    }
    // Fallback : stringify pour les valeurs numeriques/booleennes.
    return JSON.stringify(val);
}

} // namespace N3Outputs
