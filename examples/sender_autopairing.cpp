#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "esp_log.h"
#include <esp_wifi.h>
#include <nvs_flash.h>
#include <esp_event.h>
#include "esp_now_auto_pairing.h"

static const char *TAG = "SENDER";

// Global auto-pairing instance
EspNowAutoPairing* pairing = nullptr;

// Callback for received data (from receiver's ack)
void onDataReceived(const uint8_t* data, size_t len) {
    ESP_LOGI(TAG, "Received %d bytes: %s", len, (char*)data);
}

// Sender task - sends hello messages after pairing
void senderTask(void *pvParameter) {
    ESP_LOGI(TAG, "Sender task started");
    
    // Wait for pairing to complete
    int timeout = 0;
    while (!pairing->isPaired() && timeout < 60) {
        ESP_LOGI(TAG, "Waiting for pairing... (%d/60s)", timeout);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        timeout++;
    }
    
    if (!pairing->isPaired()) {
        ESP_LOGE(TAG, "Pairing failed!");
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "Pairing successful! Starting to send messages...");
    
    // Send hello messages every 2 seconds
    while (1) {
        char message[32];
        snprintf(message, sizeof(message), "Hello from Master!");
        
        esp_err_t result = pairing->sendData((uint8_t*)message, strlen(message));
        
        if (result == ESP_OK) {
            ESP_LOGI(TAG, "Message sent: %s", message);
        } else {
            ESP_LOGE(TAG, "Send failed: %s", esp_err_to_name(result));
        }
        
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}

extern "C" void app_main(void)
{
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "=== ESP-NOW Auto-Pairing SENDER (MASTER) ===");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Print this device's MAC address
    uint8_t my_mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, my_mac);
    ESP_LOGI(TAG, "My MAC Address: %02x:%02x:%02x:%02x:%02x:%02x", 
             my_mac[0], my_mac[1], my_mac[2], my_mac[3], my_mac[4], my_mac[5]);

    // Initialize auto-pairing as MASTER (can discover and pair with SLAVEs)
    pairing = new EspNowAutoPairing(EspNowRole::MASTER, 1);
    ESP_ERROR_CHECK(pairing->init());
    
    // Register callback for received data
    ESP_ERROR_CHECK(pairing->registerReceiveCallback(onDataReceived));
    
    // Start pairing process - MASTER will actively search for SLAVEs
    ESP_LOGI(TAG, "Starting pairing process as MASTER...");
    ESP_ERROR_CHECK(pairing->startPairing());
    
    // Create sender task
    xTaskCreate(senderTask, "sender_task", 4096, NULL, 5, NULL);
}
