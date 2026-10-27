#pragma once
#include <cmath>
#include <cstdlib>
#include <string>

#include "esp_log.h"

#include "NvsStorageManager.h"

class SettingsManager {
    NvsStorageManager& nvs;
public:
    explicit SettingsManager(NvsStorageManager& nvs) : nvs(nvs) {
        loadSettings();
    }

    std::string tz = "AEST-10AEDT,M10.1.0,M4.1.0/3";
    std::string ntpServer = "time.google.com";
    int frequency = 1000;
    float duty = 10;
    bool invert = false;

    void loadSettings() {
        std::string value;
        nvs.retrieve("tz", tz);
        nvs.retrieve("ntpServer", ntpServer);
        if (nvs.retrieve("frequency", value)) {
            char* end = nullptr;
            const long f = std::strtol(value.c_str(), &end, 10);
            if (end != value.c_str() && f > 0) frequency = static_cast<int>(f);
        }
        if (nvs.retrieve("duty", value)) {
            char* end = nullptr;
            const float d = std::strtof(value.c_str(), &end);
            if (end != value.c_str() && std::isfinite(d)) duty = d;
        }
        // Older builds wrote "t"/"f", the /signal handler writes "true"/"false".
        if (nvs.retrieve("invert", value)) {
            invert = (value == "true" || value == "t");
        }
    }

    void Store(const std::string& key, const std::string& value) {
        nvs.store(key, value);
    }
};
