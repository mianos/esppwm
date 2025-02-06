#pragma once
#include "WebServer.h"
#include "PWMControl.h"
#include "SettingsManager.h"

// Derived context must see the definition of WebContext from WebServer.h
struct LocalWebContext : public WebContext {
    PWMControl* pump;
    SettingsManager* settings;

    // Order of parameters must match your usage
    // 1) WiFiManager* 2) PWMControl* 3) SettingsManager*
    LocalWebContext(WiFiManager* wifi, PWMControl* pumpPtr, SettingsManager* settingsPtr)
        : WebContext(wifi),
          pump(pumpPtr),
          settings(settingsPtr) {
    }
};

class LocalWebServer : public WebServer {
public:
    // Takes a LocalWebContext* to pass up to WebServer
    LocalWebServer(LocalWebContext* context);
    // Remove 'override' if base destructor isn't virtual, or keep if it is
    ~LocalWebServer() override;

    // Our base declares "virtual esp_err_t start()" so we can override it
    esp_err_t start() override;

private:
    // Additional endpoints
    static esp_err_t pump_handler(httpd_req_t* req);
    static esp_err_t signal_handler(httpd_req_t* req);
protected:
    virtual void populate_healthz_fields(WebContext *ctx, JsonWrapper& json);
};

