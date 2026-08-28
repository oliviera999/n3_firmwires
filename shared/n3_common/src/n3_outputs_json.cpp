#include "n3_outputs_json.h"
#include <stdlib.h>
#include <string.h>

namespace N3Outputs {

/** Cherche une valeur dans le format plat { "112": "0" } ou nested
 *  { "outputs": [ { "gpio": 112, "state": "0" }, ... ] } (API realtime). */
static bool resolveValue(JSONVar& obj, const char* key, JSONVar& outVal) {
    if (key == nullptr || key[0] == '\0') {
        return false;
    }

    if (obj.hasOwnProperty(key)) {
        outVal = obj[key];
        String valueType = JSON.typeof(outVal);
        if (valueType != "undefined" && valueType != "null") {
            return true;
        }
    }

    if (!obj.hasOwnProperty("outputs")) {
        return false;
    }
    JSONVar outputs = obj["outputs"];
    if (JSON.typeof(outputs) != "array") {
        return false;
    }

    const int wantGpio = atoi(key);
    const int len = outputs.length();
    for (int i = 0; i < len; ++i) {
        JSONVar item = outputs[i];
        if (JSON.typeof(item) != "object" || !item.hasOwnProperty("gpio") ||
            !item.hasOwnProperty("state")) {
            continue;
        }
        JSONVar gpioVal = item["gpio"];
        int gpio = 0;
        String gpioType = JSON.typeof(gpioVal);
        if (gpioType == "number" || gpioType == "boolean") {
            gpio = (int)gpioVal;
        } else if (gpioType == "string") {
            const char* raw = (const char*)gpioVal;
            if (raw == nullptr) {
                continue;
            }
            gpio = atoi(raw);
        } else {
            continue;
        }
        if (gpio != wantGpio) {
            continue;
        }
        outVal = item["state"];
        String stateType = JSON.typeof(outVal);
        return (stateType != "undefined" && stateType != "null");
    }
    return false;
}

static bool parseIntValue(JSONVar& val, int* outValue) {
    if (outValue == nullptr) {
        return false;
    }
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

int readIntByKey(JSONVar& obj, const char* key, int defaultValue) {
    JSONVar val;
    if (!resolveValue(obj, key, val)) {
        return defaultValue;
    }
    int parsed = defaultValue;
    if (!parseIntValue(val, &parsed)) {
        return defaultValue;
    }
    return parsed;
}

bool tryReadIntByKey(JSONVar& obj, const char* key, int* outValue) {
    if (outValue == nullptr) {
        return false;
    }
    JSONVar val;
    if (!resolveValue(obj, key, val)) {
        return false;
    }
    return parseIntValue(val, outValue);
}

String readStringByKey(JSONVar& obj, const char* key, const String& defaultValue) {
    JSONVar val;
    if (!resolveValue(obj, key, val)) {
        return defaultValue;
    }
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
