#include <cstring>
#include "esp_netif.h"
#include "esp_system.h"

#include <wifi_provisioning/manager.h>

#include "wifimanager.h"

EventGroupHandle_t WiFiManager::wifi_event_group;
const char* WiFiManager::TAG = "WiFiManager";

WiFiManager::WiFiManager(NvsStorageManager& storageManager,
						 esp_event_handler_t eventHandler,
						 void* eventHandlerArg, 
						 bool clear_settings) : storageManager(storageManager) {
	if (clear_settings) {
		clear();
	}
    ESP_ERROR_CHECK(esp_netif_init());
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &localEventHandler, this));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &localEventHandler, this));
    ESP_ERROR_CHECK(esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &localEventHandler, this));
	if (eventHandler != nullptr) {
		ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, eventHandler, eventHandlerArg));
	}
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

WiFiManager::~WiFiManager() {
    vEventGroupDelete(wifi_event_group);
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &localEventHandler);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &localEventHandler);
    esp_event_handler_unregister(SC_EVENT, ESP_EVENT_ANY_ID, &localEventHandler);
    esp_wifi_stop();
    esp_wifi_deinit();
}


void WiFiManager::setHostname(const char* hostname) {
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
        ESP_ERROR_CHECK(esp_netif_set_hostname(netif, hostname));
        ESP_LOGI(TAG, "Hostname set to: %s", hostname);
    } else {
        ESP_LOGE(TAG, "Failed to set hostname, network interface not found.");
    }
}

void WiFiManager::localEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
	//auto *wev = static_cast<WiFiManager *>(arg);
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
		ESP_LOGI(TAG, "INTO WIFI START EVENT");
		bool provisioned = false;
		ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));
		if (!provisioned) {
			ESP_LOGI(TAG, "Not provisioned");
			xTaskCreate(smartConfigTask, "smartConfigTask", 4096, NULL, 3, NULL);
		} else {
			ESP_LOGI(TAG, "already provisioned");
			esp_wifi_connect();
		}
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_SCAN_DONE) {
        ESP_LOGI(TAG, "Scan done");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_FOUND_CHANNEL) {
        ESP_LOGI(TAG, "Found channel");
	
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD) {
        ESP_LOGI(TAG, "Got SSID and password");

        auto* evt = (smartconfig_event_got_ssid_pswd_t*)event_data;

        wifi_config_t wifi_config{};
        memset(&wifi_config, 0, sizeof(wifi_config));

        // Copy SSID and Password safely
        memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
        memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));

        ESP_LOGI(TAG, "SSID: %s", (char*)wifi_config.sta.ssid);
        ESP_LOGI(TAG, "Password: %s", (char*)wifi_config.sta.password);

#ifdef CONFIG_SET_MAC_ADDRESS_OF_TARGET_AP
        wifi_config.sta.bssid_set = evt->bssid_set;
        if (wifi_config.sta.bssid_set) {
            memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
            ESP_LOGI(TAG, "Target AP MAC Address: " MACSTR, MAC2STR(evt->bssid));
        }
#endif

        // Extract reserved data if using ESP-Touch V2
        if (evt->type == SC_TYPE_ESPTOUCH_V2) {
            uint8_t rvd_data[33] = {0};  // Buffer for reserved data (e.g., hostname)

            ESP_ERROR_CHECK(esp_smartconfig_get_rvd_data(rvd_data, sizeof(rvd_data)));

            char hostname[33] = {0};  // Hostname buffer (max 32 chars + null terminator)
            strncpy(hostname, (char*)rvd_data, sizeof(hostname) - 1);
            ESP_LOGI(TAG, "Hostname received: %s", hostname);

            setHostname(hostname);  // Set the hostname on the network interface
        }

        ESP_ERROR_CHECK(esp_wifi_disconnect());  // Disconnect if already connected
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_connect());  // Connect to the Wi-Fi network

	} else if (event_base == SC_EVENT && event_id == SC_EVENT_SEND_ACK_DONE) {
		xEventGroupSetBits(wifi_event_group, ESPTOUCH_DONE_BIT);
	}

}


void WiFiManager::smartConfigTask(void* param) {
    // Enable ESP-Touch V2 for custom data support
    ESP_ERROR_CHECK(esp_smartconfig_set_type(SC_TYPE_ESPTOUCH_V2));

    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_smartconfig_start(&cfg));

    while (1) {
        EventBits_t uxBits = xEventGroupWaitBits(
            wifi_event_group, CONNECTED_BIT | ESPTOUCH_DONE_BIT,
            pdTRUE, pdFALSE, portMAX_DELAY);

        if (uxBits & CONNECTED_BIT) {
            ESP_LOGI(TAG, "WiFi Connected to AP");
        }
        if (uxBits & ESPTOUCH_DONE_BIT) {
            ESP_LOGI(TAG, "SmartConfig complete");
            esp_smartconfig_stop();
            vTaskDelete(NULL);
        }
    }
}

void WiFiManager::clear() {
	wifi_prov_mgr_reset_provisioning();
	ESP_LOGI(TAG, "WiFi credentials cleared.");
	esp_restart();
}
