#pragma once
#include <string>

#include "WebServer.h"
#include "PWMControl.h"
#include "SettingsManager.h"

struct LocalWebContext : public WebContext {
    PWMControl* pump;
    SettingsManager* settings;

    LocalWebContext(WiFiManager* wifi, PWMControl* pumpPtr, SettingsManager* settingsPtr)
        : WebContext(wifi), pump(pumpPtr), settings(settingsPtr) {}
};

// Adds to the shared WebServer base (/reset, /set_hostname, /healthz):
//   POST /pump      {"duty": %, "period": ms?}
//   GET  /pump      {"duty", "frequency", "invert"}; POST /pump and /signal reply the same
//   POST /signal    {"invert": bool?, "frequency": Hz?}
//   POST /firmware  raw .bin body -> inactive OTA slot -> reboot
//   GET  /firmware  running image version / partition
class LocalWebServer : public WebServer {
public:
    explicit LocalWebServer(LocalWebContext* context);
    esp_err_t start() override;

protected:
    void populate_healthz_fields(WebContext* context, JsonWrapper& json) override;

private:
    static esp_err_t pump_handler(httpd_req_t* req);
    static esp_err_t pump_get_handler(httpd_req_t* req);
    static esp_err_t signal_handler(httpd_req_t* req);
    static esp_err_t firmware_post_handler(httpd_req_t* req);
    static esp_err_t firmware_get_handler(httpd_req_t* req);

    static void pumpState(LocalWebContext* ctx, JsonWrapper& json);
};
