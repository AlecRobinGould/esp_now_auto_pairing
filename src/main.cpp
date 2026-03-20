#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "esp_log.h"
#include <esp_wifi.h>
#include <nvs_flash.h>
#include <esp_event.h>
#include "esp_now_auto_pairing.h"

static const char *TAG = "SENDER";
// static const char *TAG = "RECEIVER";
// Global auto-pairing instance
EspNowAutoPairing* pairing = nullptr;

// Message queue for received data
QueueHandle_t messageQueue = nullptr;

// Structure for queued messages
struct ReceivedMessage {
    uint8_t data[250];
    size_t len;
};

// MAC address formatting macros
#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"

// ========== CONFIGURATION SECTION ==========
// Set DEVICE_MODE to either "SENDER" or "RECEIVER"
#define DEVICE_MODE "SENDER"  // Change this to "SENDER" or "RECEIVER"
// #define DEVICE_MODE "RECEIVER"
// ==========================================

// Callback for received data - queue messages for the receiver task
void onDataReceived(const uint8_t* data, size_t len) {
    if (messageQueue == nullptr) {
        ESP_LOGW(TAG, "Message queue not initialized");
        return;
    }
    
    ESP_LOGI(TAG, "onDataReceived called: len=%d", len);
    
    ReceivedMessage msg;
    msg.len = (len > sizeof(msg.data) - 1) ? sizeof(msg.data) - 1 : len;  // Leave room for null terminator
    memcpy(msg.data, data, msg.len);
    msg.data[msg.len] = '\0';  // Null-terminate the message
    
    if (xQueueSend(messageQueue, &msg, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to queue message");
    } else {
        ESP_LOGI(TAG, "Message queued successfully");
    }
}

// Sender task - sends hello messages after pairing
void senderTask(void *pvParameter) {
    ESP_LOGI(TAG, "Sender task started");
    
    // Wait for pairing to complete (120 seconds timeout)
    int timeout = 0;
    while (!pairing->isPaired() && timeout < 120) {
        if (timeout % 10 == 0) {
            ESP_LOGI(TAG, "Waiting for pairing... (%d/120s)", timeout);
        }
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
        char text[32];
        snprintf(text, sizeof(text), "Hello from Master!");
        
        // Wrap message with DATA_MESSAGE type (0x04)
        uint8_t message[33];  // 1 byte type + 32 bytes data
        message[0] = 0x04;    // DATA_MESSAGE type
        size_t text_len = strlen(text);
        memcpy(message + 1, text, text_len);
        
        esp_err_t result = pairing->sendData(message, text_len + 1);
        
        if (result == ESP_OK) {
            ESP_LOGI(TAG, "Message sent: %s", text);
        } else {
            ESP_LOGE(TAG, "Send failed: %s", esp_err_to_name(result));
        }
        
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}

// Receiver task - waits for pairing, then processes incoming messages
void receiverTask(void *pvParameter) {
    ESP_LOGI(TAG, "Receiver task started - waiting for pairing from SENDER...");
    
    // Wait for pairing to complete (300 seconds timeout)
    int timeout = 0;
    while (!pairing->isPaired() && timeout < 300) {
        if (timeout % 30 == 0) {
            ESP_LOGI(TAG, "Waiting for pairing... (%d/300s)", timeout);
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        timeout++;
    }
    
    if (!pairing->isPaired()) {
        ESP_LOGE(TAG, "Pairing timeout!");
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "SLAVE successfully paired! Listening for messages...");
    
    // Process incoming messages from queue with short timeout (100ms)
    ReceivedMessage msg;
    int idle_count = 0;
    while (1) {
        if (xQueueReceive(messageQueue, &msg, pdMS_TO_TICKS(100)) == pdTRUE) {
            // Got a message - print it
            ESP_LOGI(TAG, ">>> MESSAGE RECEIVED (%d bytes): %s", msg.len, (char*)msg.data);
            idle_count = 0;
        } else {
            // No message - log status every 50 iterations (5 seconds of idle)
            idle_count++;
            if (idle_count >= 50) {
                ESP_LOGI(TAG, "[IDLE] Waiting for messages...");
                idle_count = 0;
            }
        }
    }
}

extern "C" void app_main(void)
{

    if (strcmp(DEVICE_MODE, "SENDER") == 0) {
        ESP_LOGI(TAG, "=== ESP-NOW Auto-Pairing SENDER (MASTER) ===");
    } else {
        ESP_LOGI(TAG, "=== ESP-NOW Auto-Pairing RECEIVER (SLAVE) ===");
    }
    
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
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "My MAC Address: " MACSTR, MAC2STR(my_mac));
    ESP_LOGI(TAG, "========================================");

    // Initialize auto-pairing based on device mode
    if (strcmp(DEVICE_MODE, "SENDER") == 0) {
        // Initialize as MASTER
        pairing = new EspNowAutoPairing(EspNowRole::MASTER, 1);
        // ESP_ERROR_CHECK(pairing->unpair());
        // ESP_LOGI(TAG, "Device unpaired for testing");
        // return;
        
        ESP_ERROR_CHECK(pairing->init());
        
        // Register callback for received data
        ESP_ERROR_CHECK(pairing->registerReceiveCallback(onDataReceived));
        
        // Start pairing process only if not already paired
        if (!pairing->isPaired()) {
            ESP_LOGI(TAG, "Starting pairing process as MASTER...");
            ESP_ERROR_CHECK(pairing->startPairing());
        } else {
            ESP_LOGI(TAG, "MASTER already paired! Ready to send messages.");
        }
        
        // Create sender task
        xTaskCreate(senderTask, "sender_task", 4096, NULL, 5, NULL);
    } else {
        // Initialize as SLAVE
        pairing = new EspNowAutoPairing(EspNowRole::SLAVE, 1);
        // ESP_ERROR_CHECK(pairing->unpair());
        // ESP_LOGI(TAG, "Device unpaired for testing");
        // return;
        
        ESP_ERROR_CHECK(pairing->init());
        
        // Create message queue for received data
        messageQueue = xQueueCreate(10, sizeof(ReceivedMessage));
        if (messageQueue == nullptr) {
            ESP_LOGE(TAG, "Failed to create message queue");
            return;
        }
        
        // Register callback for received data
        ESP_ERROR_CHECK(pairing->registerReceiveCallback(onDataReceived));
        
        // Start pairing process only if not already paired
        if (!pairing->isPaired()) {
            ESP_LOGI(TAG, "Waiting for pairing request from MASTER...");
            ESP_ERROR_CHECK(pairing->startPairing());
        } else {
            ESP_LOGI(TAG, "SLAVE already paired! Ready to receive messages.");
        }
        
        // Create receiver task
        xTaskCreate(receiverTask, "receiver_task", 4096, NULL, 5, NULL);
    }
}