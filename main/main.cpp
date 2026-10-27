#include <cstdlib>
#include <ctime>

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "sdkconfig.h"

#include "Button.h"
#include "LocalWebServer.h"
#include "PWMControl.h"
#include "SettingsManager.h"
#include "WifiManager.h"

static const char* TAG = "esppwm";

static SemaphoreHandle_t wifiSemaphore = nullptr;
static SemaphoreHandle_t s_got_ip = nullptr;

// A freshly-OTA'd image boots in PENDING_VERIFY and is rolled back on the next
// reset unless it marks itself good; it does so only once it is back on the
// network, so an image that can't reach Wi-Fi doesn't strand the device.
constexpr int OTA_VERIFY_TIMEOUT_MS = 120000;

static void localEventHandler(void*, esp_event_base_t, int32_t event_id, void*) {
    if (event_id == IP_EVENT_STA_GOT_IP) {
        xSemaphoreGive(wifiSemaphore);
        xSemaphoreGive(s_got_ip);
    }
}

static void otaVerifyTask(void*) {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGW(TAG, "OTA: image pending verify; waiting up to %ds for connectivity", OTA_VERIFY_TIMEOUT_MS / 1000);
        if (xSemaphoreTake(s_got_ip, pdMS_TO_TICKS(OTA_VERIFY_TIMEOUT_MS)) == pdTRUE) {
            esp_ota_mark_app_valid_cancel_rollback();
            ESP_LOGI(TAG, "OTA: connectivity confirmed, image marked valid");
        } else {
            ESP_LOGE(TAG, "OTA: no IP within timeout; rolling back to previous image");
            esp_ota_mark_app_invalid_rollback_and_reboot();
            ESP_LOGE(TAG, "OTA: rollback not possible; keeping current image");
            esp_ota_mark_app_valid_cancel_rollback();
        }
    }
    vTaskDelete(nullptr);
}

static void button_task(void* pvParameters) {
    auto* wifiManager = static_cast<WiFiManager*>(pvParameters);
    Button button(static_cast<gpio_num_t>(CONFIG_BUTTON_PIN));
    for (;;) {
        if (button.longPressed()) {
            ESP_LOGI("BUTTON", "Long press detected, resetting WiFi settings.");
            wifiManager->clear();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

extern "C" void app_main() {
    wifiSemaphore = xSemaphoreCreateBinary();
    s_got_ip = xSemaphoreCreateBinary();

    NvsStorageManager nv;
    SettingsManager settings(nv);

    // Before Wi-Fi, so the pump resumes its last duty after a power blip even
    // if the network is down.
    static PWMControl pump(settings);
    ESP_LOGI(TAG, "duty %g%%, frequency %d Hz, invert %d", settings.duty, settings.frequency, settings.invert);

    static WiFiManager wifiManager(nv, localEventHandler, nullptr);
    xTaskCreate(button_task, "button_task", 2048, &wifiManager, 10, nullptr);
    xTaskCreate(otaVerifyTask, "ota_verify", 4096, nullptr, 4, nullptr);

    xSemaphoreTake(wifiSemaphore, portMAX_DELAY);

    setenv("TZ", settings.tz.c_str(), 1);
    tzset();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, settings.ntpServer.c_str());
    esp_sntp_init();

    static LocalWebContext ctx{&wifiManager, &pump, &settings};
    static LocalWebServer webServer{&ctx};
    if (webServer.start() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start web server.");
    }

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
