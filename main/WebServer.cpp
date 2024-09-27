#include <ctime> 
#include <vector>
#include <cstring>
#include <string>
#include "esp_random.h"
#include "esp_timer.h"

#include "JsonWrapper.h"
#include "WebServer.h"


static const char *TAG = "WebServer";

#define GET_CONTEXT(req, ws) \
    auto* ws = static_cast<WebServer*>(req->user_ctx); \
    if (!ws) { \
        ESP_LOGE(TAG,"ctx null?"); \
        httpd_resp_send_500(req); \
        return ESP_FAIL; \
    }

QueueHandle_t WebServer::async_req_queue = nullptr;
SemaphoreHandle_t WebServer::worker_ready_count = nullptr;
TaskHandle_t WebServer::worker_handles[MAX_ASYNC_REQUESTS] = {nullptr};

WebServer::WebServer(WebContext& webContext) : webContext(webContext), server(nullptr) {
    start_async_req_workers();
}

WebServer::~WebServer() {
    stop();
}

esp_err_t WebServer::start() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.server_port = 80;

    // Set max_open_sockets > MAX_ASYNC_REQUESTS to allow for synchronous requests
    config.max_open_sockets = MAX_ASYNC_REQUESTS + 1;

    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);

    esp_err_t ret = httpd_start(&server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error starting server!");
        return ret;
    }

    httpd_uri_t index_uri = {
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = index_handler,
        .user_ctx  = this
    };

    httpd_uri_t long_uri = {
        .uri       = "/long",
        .method    = HTTP_GET,
        .handler   = long_async_handler,
        .user_ctx  = this
    };

    httpd_uri_t quick_uri = {
        .uri       = "/quick",
        .method    = HTTP_GET,
        .handler   = quick_handler,
        .user_ctx  = this
    };

    httpd_uri_t pump_uri = {
        .uri       = "/pump",
        .method    = HTTP_POST,
        .handler   = pump_handler,
        .user_ctx  = this
    };

	httpd_uri_t healthz_uri = {
    .uri       = "/healthz",
    .method    = HTTP_GET,
    .handler   = healthz_handler,
    .user_ctx  = this
};


    httpd_register_uri_handler(server, &index_uri);
    httpd_register_uri_handler(server, &long_uri);
    httpd_register_uri_handler(server, &quick_uri);
	httpd_register_uri_handler(server, &pump_uri);
	httpd_register_uri_handler(server, &healthz_uri);

    return ESP_OK;
}

esp_err_t WebServer::stop() {
    if (server != nullptr) {
        esp_err_t ret = httpd_stop(server);
        if (ret == ESP_OK) {
            server = nullptr;
        }
        return ret;
    }
    return ESP_OK;
}

bool WebServer::is_on_async_worker_thread() {
    TaskHandle_t handle = xTaskGetCurrentTaskHandle();
    for (int i = 0; i < MAX_ASYNC_REQUESTS; ++i) {
        if (worker_handles[i] == handle) {
            return true;
        }
    }
    return false;
}

esp_err_t WebServer::submit_async_req(httpd_req_t *req, httpd_req_handler_t handler) {
    // Create a copy of the request
    httpd_req_t* copy = nullptr;
    esp_err_t err = httpd_req_async_handler_begin(req, &copy);
    if (err != ESP_OK) {
        return err;
    }

    httpd_async_req_t async_req = {
        .req = copy,
        .handler = handler,
    };

    // Check for available workers
    if (xSemaphoreTake(worker_ready_count, 0) == pdFALSE) {
        ESP_LOGE(TAG, "No workers are available");
        httpd_req_async_handler_complete(copy);
        return ESP_FAIL;
    }

    // Send the request to the worker queue
    if (xQueueSend(async_req_queue, &async_req, pdMS_TO_TICKS(100)) == pdFALSE) {
        ESP_LOGE(TAG, "Worker queue is full");
        httpd_req_async_handler_complete(copy);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t WebServer::long_async_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "uri: /long");

    if (!is_on_async_worker_thread()) {
        if (submit_async_req(req, long_async_handler) == ESP_OK) {
            return ESP_OK;
        } else {
            httpd_resp_set_status(req, "503 Service Unavailable");
            httpd_resp_sendstr(req, "<div>No workers available. Server busy.</div>");
            return ESP_OK;
        }
    }

    // Track the number of long requests
    static uint8_t req_count = 0;
    req_count++;

    // Send initial response
    char s[100];
    snprintf(s, sizeof(s), "<div>req: %u</div>\n", req_count);
    httpd_resp_sendstr_chunk(req, s);

    // Simulate long-running task
    for (int i = 0; i < 10; ++i) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        snprintf(s, sizeof(s), "<div>%u</div>\n", i);
        httpd_resp_sendstr_chunk(req, s);
    }
    httpd_resp_sendstr_chunk(req, nullptr);
    return ESP_OK;
}

void WebServer::async_req_worker_task(void *arg) {
    ESP_LOGI(TAG, "Starting async request worker task");

    while (true) {
        // Signal that a worker is ready
        xSemaphoreGive(worker_ready_count);

        // Wait for a request
        httpd_async_req_t async_req;
        if (xQueueReceive(async_req_queue, &async_req, portMAX_DELAY)) {
            ESP_LOGI(TAG, "Invoking %s", async_req.req->uri);

            // Call the handler
            async_req.handler(async_req.req);

            // Complete the asynchronous request
            if (httpd_req_async_handler_complete(async_req.req) != ESP_OK) {
                ESP_LOGE(TAG, "Failed to complete async request");
            }
        }
    }

    vTaskDelete(nullptr);
}

void WebServer::start_async_req_workers() {
    // Create counting semaphore
    worker_ready_count = xSemaphoreCreateCounting(MAX_ASYNC_REQUESTS, 0);
    if (worker_ready_count == nullptr) {
        ESP_LOGE(TAG, "Failed to create workers counting semaphore");
        return;
    }
    async_req_queue = xQueueCreate(1, sizeof(httpd_async_req_t));
    if (async_req_queue == nullptr) {
        ESP_LOGE(TAG, "Failed to create async request queue");
        vSemaphoreDelete(worker_ready_count);
        return;
    }

    // Start worker tasks
    for (int i = 0; i < MAX_ASYNC_REQUESTS; ++i) {
        BaseType_t success = xTaskCreate(
            async_req_worker_task,
            "async_req_worker",
            ASYNC_WORKER_TASK_STACK_SIZE,
            nullptr,
            ASYNC_WORKER_TASK_PRIORITY,
            &worker_handles[i]
        );

        if (success != pdPASS) {
            ESP_LOGE(TAG, "Failed to start async request worker");
            continue;
        }
    }
}



esp_err_t WebServer::quick_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "uri: /quick");
    char s[100];
    uint32_t random_number = 0;
    esp_fill_random(&random_number, sizeof(random_number));
    snprintf(s, sizeof(s), "random: %lu\n", random_number);
    httpd_resp_sendstr(req, s);
    return ESP_OK;
}
esp_err_t WebServer::index_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "uri: /");
    const char* html = "<div><a href=\"/long\">long</a></div>"
                       "<div><a href=\"/quick\">quick</a></div>";
    httpd_resp_sendstr(req, html);
    return ESP_OK;
}



esp_err_t WebServer::pump_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "uri: /pump");
    GET_CONTEXT(req, ws);

    int total_len = req->content_len;
    int received = 0;

    if (total_len <= 0) {
        ESP_LOGE(TAG, "Invalid content length: %d", total_len);
        httpd_resp_send_err(req, HTTPD_411_LENGTH_REQUIRED, "Content-Length required");
        return ESP_FAIL;
    }

    std::vector<char> buffer(total_len + 1); // +1 for null-terminator

    while (received < total_len) {
        int ret = httpd_req_recv(req, buffer.data() + received, total_len - received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                httpd_resp_send_408(req); // Request Timeout
            } else {
                ESP_LOGE(TAG, "Failed to receive POST data");
                httpd_resp_send_500(req);
            }
            return ESP_FAIL;
        }
        received += ret;
    }
    buffer[total_len] = '\0'; // Null-terminate the string

	// Parse the buffer using JsonWrapper
	JsonWrapper json = JsonWrapper::Parse(buffer.data());
	
	// Check if a "duty" parameter is present and is a number
	float duty = 0.0;
	if (json.GetField<float>("duty", duty)) {
		ESP_LOGI(TAG, "Received duty: %g", duty);
		ws->webContext.pump.setDutyCyclePercentage(duty);
	} else {
		ESP_LOGE(TAG, "duty cycle is missing or not a number");
	}

	// Check if a "frequency" field is present (optional) and is a number
	if (json.ContainsField("frequency")) {
		float frequency = 0.0;
		if (json.GetField<float>("frequency", frequency)) {
			if (frequency > 0.0f) {
				ESP_LOGI(TAG, "Received frequency: %g", frequency);
				ws->webContext.pump.setFrequency(static_cast<int>(frequency));  // Cast to int for frequency
			} else {
				ESP_LOGE(TAG, "Invalid frequency: %g", frequency);
			}
		} else {
			ESP_LOGE(TAG, "frequency is not a valid number");
		}
	} else {
		ESP_LOGI(TAG, "frequency field is not present, keeping previous frequency.");
	}

	// Send the response back
	httpd_resp_set_type(req, "application/json");
	httpd_resp_sendstr(req, "{\"status\":\"OK\"}");
	return ESP_OK;
}

esp_err_t WebServer::healthz_handler(httpd_req_t *req) {
    // Get ESP uptime in seconds
    uint64_t uptime_us = esp_timer_get_time();
    uint32_t uptime_sec = static_cast<uint32_t>(uptime_us / 1000000ULL);

    // Get current time
    time_t now;
    time(&now);
    struct tm time_info;
    localtime_r(&now, &time_info);

    // Create ISO 8601 time string
    char time_str[30];
    strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%S%z", &time_info);

    JsonWrapper json;
    json.AddItem("uptime", uptime_sec);
    json.AddItem("time", time_str);
    std::string json_str = json.ToString();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str.c_str());
    return ESP_OK;
}

