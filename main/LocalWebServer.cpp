#include "LocalWebServer.h"
#include <esp_log.h>
#include <vector>
#include <string>

static const char* TAG_LOCAL = "LocalWebServer";

LocalWebServer::LocalWebServer(LocalWebContext* context)
    : WebServer(context) {
}

LocalWebServer::~LocalWebServer() {
}

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

    httpd_uri_t otaUri;
    otaUri.uri         = "/ota";
    otaUri.method      = HTTP_POST;
    otaUri.handler     = ota_handler;
    otaUri.user_ctx    = this;
    httpd_register_uri_handler(server, &otaUri);

    return ESP_OK;
}

esp_err_t LocalWebServer::readRequestBody(LocalWebServer* localServer, httpd_req_t* req, std::string& body) {
    int contentLength = req->content_len;
    if (contentLength <= 0) {
        return localServer->sendJsonError(req, 411, "Content-Length required");
    }
    std::vector<char> buffer(contentLength + 1);
    int received = 0;
    while (received < contentLength) {
        int ret = httpd_req_recv(req, buffer.data() + received, contentLength - received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                httpd_resp_send_408(req);
            } else {
                localServer->sendJsonError(req, 500, "Failed to read POST data");
            }
            return ESP_FAIL;
        }
        received += ret;
    }
    buffer[contentLength] = '\0';
    body = std::string(buffer.data());
    return ESP_OK;
}

esp_err_t LocalWebServer::pump_handler(httpd_req_t* req) {
    auto* localServer = static_cast<LocalWebServer*>(req->user_ctx);
    auto* localCtx = static_cast<LocalWebContext*>(localServer->webContext);
    if (!localCtx || !localCtx->pump || !localCtx->settings) {
        return localServer->sendJsonError(req, 500, "Invalid LocalWebContext / pump / settings");
    }

    std::string requestBody;
    esp_err_t readResult = readRequestBody(localServer, req, requestBody);
    if (readResult != ESP_OK) {
        return readResult;
    }
    JsonWrapper json = JsonWrapper::Parse(requestBody.c_str());

    float duty = 0.0f;
    int period = 0;
    if (!json.GetField<float>("duty", duty)) {
        return localServer->sendJsonError(req, 400, "Missing or invalid 'duty'");
    }

    if (json.ContainsField("period")) {
        if (!json.GetField<int>("period", period) || period <= 0) {
            return localServer->sendJsonError(req, 400, "Invalid 'period' field");
        }
        localCtx->pump->setDutyCyclePercentage(duty, period);
        localCtx->settings->duty = duty;
    } else {
        localCtx->pump->setDutyCyclePercentage(duty);
        localCtx->settings->duty = duty;
        localCtx->settings->Store("duty", std::to_string(duty));
    }

    JsonWrapper response;
    response.AddItem("status", "OK");
    response.AddItem("duty", localCtx->settings->duty);

    std::string responseStr = response.ToString();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, responseStr.c_str());
    return ESP_OK;
}

esp_err_t LocalWebServer::signal_handler(httpd_req_t* req) {
    auto* localServer = static_cast<LocalWebServer*>(req->user_ctx);
    auto* localCtx = static_cast<LocalWebContext*>(localServer->webContext);
    if (!localCtx || !localCtx->pump || !localCtx->settings) {
        return localServer->sendJsonError(req, 500, "Invalid LocalWebContext / pump / settings");
    }

    std::string requestBody;
    esp_err_t readResult = readRequestBody(localServer, req, requestBody);
    if (readResult != ESP_OK) {
        return readResult;
    }
    JsonWrapper json = JsonWrapper::Parse(requestBody.c_str());

    if (json.ContainsField("invert")) {
        bool invertValue = false;
        json.GetField<bool>("invert", invertValue);
        localCtx->settings->Store("invert", invertValue ? "true" : "false");
        localCtx->settings->invert = invertValue;
        float currentDuty = localCtx->pump->getCurrentPercentage();
        localCtx->pump->setDutyCyclePercentage(currentDuty);
    }

    if (json.ContainsField("frequency")) {
        int frequencyValue = 0;
        if (!json.GetField<int>("frequency", frequencyValue) || frequencyValue <= 0) {
            return localServer->sendJsonError(req, 400, "Invalid 'frequency'");
        }
        localCtx->pump->setFrequency(frequencyValue);
        localCtx->settings->Store("frequency", std::to_string(frequencyValue));
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"OK\"}");
    return ESP_OK;
}

esp_err_t LocalWebServer::ota_handler(httpd_req_t* req) {
    auto* localServer = static_cast<LocalWebServer*>(req->user_ctx);
    auto* localCtx = static_cast<LocalWebContext*>(localServer->webContext);
    if (!localCtx || !localCtx->ota) {
        return localServer->sendJsonError(req, 500, "Invalid LocalWebContext or OTA updater");
    }

    std::string requestBody;
    esp_err_t readResult = readRequestBody(localServer, req, requestBody);
    if (readResult != ESP_OK) {
        return readResult;
    }
    JsonWrapper json = JsonWrapper::Parse(requestBody.c_str());
    std::string otaURL;
    if (json.GetField("ota_url", otaURL, true)) {
        localCtx->ota->perform_update(otaURL);
        ESP_LOGI(TAG_LOCAL, "Flashed from '%s'", otaURL.c_str());
    } else {
        ESP_LOGW(TAG_LOCAL, "Missing or invalid 'ota_url'");
        return localServer->sendJsonError(req, 400, "Missing or invalid 'ota_url'");
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"OK\"}");
    return ESP_OK;
}

void LocalWebServer::populate_healthz_fields(WebContext* context, JsonWrapper& json) {
    auto* localContext = static_cast<LocalWebContext*>(context);
    json.AddItem("duty", localContext->settings->duty);
}

