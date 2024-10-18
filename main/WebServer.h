#pragma once
#include <esp_http_server.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "PWMControl.h"
#include "SettingsManager.h"
#include "WifiManager.h"


class WebServer {
public:
	struct WebContext {
		PWMControl& pump;
		SettingsManager &settings;
		WiFiManager &wifiManager;
	};

    WebServer(WebContext& context);
    ~WebServer();
    esp_err_t start();
    esp_err_t stop();

private:
    static constexpr int ASYNC_WORKER_TASK_PRIORITY = 5;
    static constexpr int ASYNC_WORKER_TASK_STACK_SIZE = 4096;
    static constexpr int MAX_ASYNC_REQUESTS = 5;

    using httpd_req_handler_t = esp_err_t (*)(httpd_req_t *req);

    struct httpd_async_req_t {
        httpd_req_t* req;
        httpd_req_handler_t handler;
    };

    static bool is_on_async_worker_thread();
    static esp_err_t submit_async_req(httpd_req_t *req, httpd_req_handler_t handler);
    static void async_req_worker_task(void *arg);
    static void start_async_req_workers();

	static esp_err_t pump_handler(httpd_req_t *req);
	static esp_err_t healthz_handler(httpd_req_t *req);
	static esp_err_t reset_wifi_handler(httpd_req_t *req);
	static esp_err_t adjust_duty_params_handler(httpd_req_t *req);

    static QueueHandle_t async_req_queue;
    static SemaphoreHandle_t worker_ready_count;
    static TaskHandle_t worker_handles[MAX_ASYNC_REQUESTS];

	WebContext& webContext;
    httpd_handle_t server;
};

