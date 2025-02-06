#include "LocalWebServer.h"
#include <esp_log.h>
#include <vector>
#include <string>

static const char* TAG_LOCAL = "LocalWebServer";

// Constructor/destructor
LocalWebServer::LocalWebServer(LocalWebContext* context)
    : WebServer(context) {
}

LocalWebServer::~LocalWebServer() {
}

// Register our additional endpoints after calling the base start()
esp_err_t LocalWebServer::start() {
    esp_err_t result = WebServer::start();
    if (result != ESP_OK) {
        return result;
    }

    httpd_uri_t pumpUri;
    pumpUri.uri       = "/pump";
    pumpUri.method    = HTTP_POST;
    pumpUri.handler   = pump_handler;
    pumpUri.user_ctx  = this;
    httpd_register_uri_handler(server, &pumpUri);

    httpd_uri_t signalUri;
    signalUri.uri      = "/signal";
    signalUri.method   = HTTP_POST;
    signalUri.handler  = signal_handler;
    signalUri.user_ctx = this;
    httpd_register_uri_handler(server, &signalUri);

    return ESP_OK;
}

// /pump handler
esp_err_t LocalWebServer::pump_handler(httpd_req_t* req) {
    auto* localServer = static_cast<LocalWebServer*>(req->user_ctx);
    if (!localServer) {
        return WebServer::sendJsonError(req, 500, "Missing LocalWebServer instance");
    }

    auto* localCtx = static_cast<LocalWebContext*>(localServer->webContext);
    if (!localCtx || !localCtx->pump || !localCtx->settings) {
        return WebServer::sendJsonError(req, 500, "Invalid LocalWebContext / pump / settings");
    }

    int totalLen = req->content_len;
    if (totalLen <= 0) {
        return WebServer::sendJsonError(req, 411, "Content-Length required");
    }
    std::vector<char> buffer(totalLen + 1);
    int received = 0;
    while (received < totalLen) {
        int ret = httpd_req_recv(req, buffer.data() + received, totalLen - received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                httpd_resp_send_408(req);
            } else {
                WebServer::sendJsonError(req, 500, "Failed to read POST data");
            }
            return ESP_FAIL;
        }
        received += ret;
    }
    buffer[totalLen] = '\0';

    JsonWrapper json = JsonWrapper::Parse(buffer.data());

    float duty = 0.0f;
    int period = 0;

    if (!json.GetField<float>("duty", duty)) {
        return WebServer::sendJsonError(req, 400, "Missing or invalid 'duty'");
    }

    if (json.ContainsField("period")) {
        if (!json.GetField<int>("period", period) || period <= 0) {
            return WebServer::sendJsonError(req, 400, "Invalid 'period' field");
        }
        localCtx->pump->setDutyCyclePercentage(duty, period);
        // Not persisting duty to NVS since it's a temporary change
    } else {
        localCtx->pump->setDutyCyclePercentage(duty);
        localCtx->settings->Store("duty", std::to_string(duty));
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"OK\"}");
    return ESP_OK;
}

// /signal handler
esp_err_t LocalWebServer::signal_handler(httpd_req_t* req) {
    auto* localServer = static_cast<LocalWebServer*>(req->user_ctx);
    if (!localServer) {
        return WebServer::sendJsonError(req, 500, "Missing LocalWebServer instance");
    }

    auto* localCtx = static_cast<LocalWebContext*>(localServer->webContext);
    if (!localCtx || !localCtx->pump || !localCtx->settings) {
        return WebServer::sendJsonError(req, 500, "Invalid LocalWebContext / pump / settings");
    }

    int totalLen = req->content_len;
    if (totalLen <= 0) {
        return WebServer::sendJsonError(req, 411, "Content-Length required");
    }
    std::vector<char> buffer(totalLen + 1);
    int received = 0;
    while (received < totalLen) {
        int ret = httpd_req_recv(req, buffer.data() + received, totalLen - received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                httpd_resp_send_408(req);
            } else {
                WebServer::sendJsonError(req, 500, "Failed to read POST data");
            }
            return ESP_FAIL;
        }
        received += ret;
    }
    buffer[totalLen] = '\0';

    JsonWrapper json = JsonWrapper::Parse(buffer.data());

    if (json.ContainsField("invert")) {
        bool invertVal = false;
        json.GetField<bool>("invert", invertVal);
        localCtx->settings->Store("invert", invertVal ? "true" : "false");
        localCtx->settings->invert = invertVal;

        float currentDuty = localCtx->pump->getCurrentPercentage();
        localCtx->pump->setDutyCyclePercentage(currentDuty);
    }

    if (json.ContainsField("frequency")) {
        int frequencyVal = 0;
        if (!json.GetField<int>("frequency", frequencyVal) || frequencyVal <= 0) {
            return WebServer::sendJsonError(req, 400, "Invalid 'frequency'");
        }
        localCtx->pump->setFrequency(frequencyVal);
        localCtx->settings->Store("frequency", std::to_string(frequencyVal));
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"OK\"}");
    return ESP_OK;
}

