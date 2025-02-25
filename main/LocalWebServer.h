#pragma once
#include "WebServer.h"
#include "PWMControl.h"
#include "SettingsManager.h"
#include "Ota.h"
#include <string>

struct LocalWebContext : public WebContext {
    PWMControl* pump;
    SettingsManager* settings;
    OTAUpdater* ota;

    LocalWebContext(WiFiManager* wifi,
                    PWMControl* pumpPtr,
                    SettingsManager* settingsPtr,
                    OTAUpdater* otaPtr)
        : WebContext(wifi),
          pump(pumpPtr),
          settings(settingsPtr),
          ota(otaPtr) {
    }
};

class LocalWebServer : public WebServer {
public:
    LocalWebServer(LocalWebContext* context);
    ~LocalWebServer() override;
    esp_err_t start() override;

private:
    static esp_err_t pump_handler(httpd_req_t* req);
    static esp_err_t signal_handler(httpd_req_t* req);
    static esp_err_t ota_handler(httpd_req_t* req);

    // Helper to read the POST body. Calls sendJsonError through the provided instance.
    static esp_err_t readRequestBody(LocalWebServer* localServer, httpd_req_t* req, std::string& body);

protected:
    virtual void populate_healthz_fields(WebContext* context, JsonWrapper& json);
};

