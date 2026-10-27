#include "LocalWebServer.h"

#include <algorithm>
#include <array>
#include <cinttypes>
#include <string>
#include <vector>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "LocalWebServer";

namespace {

constexpr int kMaxBodyBytes = 1024;

esp_err_t send_json(httpd_req_t* req, const JsonWrapper& json) {
    httpd_resp_set_type(req, "application/json");
    const std::string out = json.ToString();
    return httpd_resp_sendstr(req, out.c_str());
}

}  // namespace

LocalWebServer::LocalWebServer(LocalWebContext* context) : WebServer(context) {}

esp_err_t LocalWebServer::start() {
    esp_err_t r = WebServer::start();
    if (r != ESP_OK) return r;

    struct Route {
        const char*    uri;
        httpd_method_t method;
        esp_err_t (*handler)(httpd_req_t*);
    };
    const std::array<Route, 4> routes = {{
        {"/pump",     HTTP_POST, pump_handler},
        {"/signal",   HTTP_POST, signal_handler},
        {"/firmware", HTTP_POST, firmware_post_handler},
        {"/firmware", HTTP_GET,  firmware_get_handler},
    }};
    for (const Route& route : routes) {
        httpd_uri_t uri = {
            .uri      = route.uri,
            .method   = route.method,
            .handler  = route.handler,
            .user_ctx = this,
        };
        esp_err_t err = httpd_register_uri_handler(server, &uri);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "register %s: %s", route.uri, esp_err_to_name(err));
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t LocalWebServer::readRequestBody(LocalWebServer* localServer, httpd_req_t* req, std::string& body) {
    const int contentLength = req->content_len;
    if (contentLength <= 0) return localServer->sendJsonError(req, 411, "Content-Length required");
    if (contentLength > kMaxBodyBytes) return localServer->sendJsonError(req, 413, "request body too large");
    std::vector<char> buffer(contentLength);
    int received = 0;
    while (received < contentLength) {
        int ret = httpd_req_recv(req, buffer.data() + received, contentLength - received);
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (ret <= 0) {
            localServer->sendJsonError(req, 400, "request body truncated");
            return ESP_FAIL;
        }
        received += ret;
    }
    body.assign(buffer.data(), contentLength);
    return ESP_OK;
}

// A timed duty ("period" ms) reverts to the previous duty afterwards and is
// neither stored nor reported as the standing duty.
esp_err_t LocalWebServer::pump_handler(httpd_req_t* req) {
    auto* self = static_cast<LocalWebServer*>(req->user_ctx);
    auto* ctx = static_cast<LocalWebContext*>(self->webContext);

    std::string body;
    if (readRequestBody(self, req, body) != ESP_OK) return ESP_FAIL;
    JsonWrapper json = JsonWrapper::Parse(body);

    float duty = 0.0f;
    if (!json.GetField<float>("duty", duty)) return self->sendJsonError(req, 400, "Missing or invalid 'duty'");
    if (duty < 0.0f || duty > 100.0f) return self->sendJsonError(req, 400, "duty out of range [0,100]");

    int period = 0;
    if (json.ContainsField("period")) {
        if (!json.GetField<int>("period", period) || period <= 0) {
            return self->sendJsonError(req, 400, "Invalid 'period' field");
        }
        ctx->pump->setDutyCyclePercentage(duty, period);
    } else {
        ctx->pump->setDutyCyclePercentage(duty);
        ctx->settings->duty = duty;
        ctx->settings->Store("duty", std::to_string(duty));
    }

    JsonWrapper response;
    response.AddItem("status", std::string("OK"));
    response.AddItem("duty", ctx->settings->duty);
    return send_json(req, response);
}

esp_err_t LocalWebServer::signal_handler(httpd_req_t* req) {
    auto* self = static_cast<LocalWebServer*>(req->user_ctx);
    auto* ctx = static_cast<LocalWebContext*>(self->webContext);

    std::string body;
    if (readRequestBody(self, req, body) != ESP_OK) return ESP_FAIL;
    JsonWrapper json = JsonWrapper::Parse(body);

    if (json.ContainsField("frequency")) {
        int frequency = 0;
        if (!json.GetField<int>("frequency", frequency) || !ctx->pump->setFrequency(frequency)) {
            return self->sendJsonError(req, 400, "Invalid 'frequency'");
        }
        ctx->settings->frequency = frequency;
        ctx->settings->Store("frequency", std::to_string(frequency));
    }

    if (json.ContainsField("invert")) {
        bool invert = false;
        if (!json.GetField<bool>("invert", invert)) return self->sendJsonError(req, 400, "Invalid 'invert'");
        ctx->settings->invert = invert;
        ctx->settings->Store("invert", invert ? "true" : "false");
        // Re-apply the commanded duty, not the raw PWM value, which already
        // has the old polarity baked in.
        ctx->pump->setDutyCyclePercentage(ctx->settings->duty);
    }

    JsonWrapper response;
    response.AddItem("status", std::string("OK"));
    response.AddItem("frequency", ctx->settings->frequency);
    response.AddItem("invert", ctx->settings->invert);
    return send_json(req, response);
}

void LocalWebServer::populate_healthz_fields(WebContext* context, JsonWrapper& json) {
    auto* ctx = static_cast<LocalWebContext*>(context);
    json.AddItem("duty", ctx->settings->duty);
    json.AddItem("version", std::string(esp_app_get_description()->version));
    json.AddItem("partition", std::string(esp_ota_get_running_partition()->label));
    json.AddItem("heap_free", static_cast<int>(esp_get_free_heap_size()));
}

// POST /firmware — raw .bin body. Streams into the inactive OTA slot, sets it as
// the next boot partition, then reboots.
// Deploy: curl --data-binary @build/esppwm.bin http://<host>/firmware
esp_err_t LocalWebServer::firmware_post_handler(httpd_req_t* req) {
    auto* self = static_cast<LocalWebServer*>(req->user_ctx);
    if (req->content_len <= 0) return self->sendJsonError(req, 400, "Content-Length required");

    const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
    if (target == nullptr) return self->sendJsonError(req, 500, "no OTA partition available");
    if (req->content_len > static_cast<int>(target->size)) {
        return self->sendJsonError(req, 413, "image larger than OTA partition");
    }
    ESP_LOGI(TAG, "OTA: writing %d bytes to %s @ 0x%" PRIx32, req->content_len, target->label, target->address);

    esp_ota_handle_t handle = 0;
    esp_err_t err = esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &handle);
    if (err != ESP_OK) return self->sendJsonError(req, 500, esp_err_to_name(err));

    char buf[1024];
    int remaining = req->content_len;
    int written = 0;
    while (remaining > 0) {
        int got = httpd_req_recv(req, buf, std::min<int>(remaining, static_cast<int>(sizeof(buf))));
        if (got == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (got <= 0) {
            esp_ota_abort(handle);
            return self->sendJsonError(req, 400, "request body truncated");
        }
        err = esp_ota_write(handle, buf, got);
        if (err != ESP_OK) {
            esp_ota_abort(handle);
            return self->sendJsonError(req, 500, esp_err_to_name(err));
        }
        written   += got;
        remaining -= got;
    }

    err = esp_ota_end(handle);
    if (err != ESP_OK) return self->sendJsonError(req, 400, esp_err_to_name(err));
    err = esp_ota_set_boot_partition(target);
    if (err != ESP_OK) return self->sendJsonError(req, 500, esp_err_to_name(err));

    ESP_LOGW(TAG, "OTA: %d bytes written to %s; rebooting", written, target->label);
    JsonWrapper resp;
    resp.AddItem("status",    std::string("ok"));
    resp.AddItem("written",   written);
    resp.AddItem("partition", std::string(target->label));
    send_json(req, resp);

    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

esp_err_t LocalWebServer::firmware_get_handler(httpd_req_t* req) {
    const esp_app_desc_t*  desc    = esp_app_get_description();
    const esp_partition_t* running = esp_ota_get_running_partition();
    JsonWrapper resp;
    resp.AddItem("version",   std::string(desc->version));
    resp.AddItem("idf_ver",   std::string(desc->idf_ver));
    resp.AddItem("date",      std::string(desc->date));
    resp.AddItem("time",      std::string(desc->time));
    resp.AddItem("partition", std::string(running->label));
    return send_json(req, resp);
}
