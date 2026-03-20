/**
 * Broadcaster Example for ESP-NOW Auto Pairing Library
 * 
 * This example demonstrates how to use the library as a Broadcaster device
 * that pairs with multiple devices in a one-to-many communication pattern.
 */

#include <esp_now_auto_pairing.h>

// Global instance
EspNowAutoPairing* g_pairing = nullptr;

// Callback for receiving data from paired devices
void onDataReceived(const uint8_t* data, size_t len) {
    ESP_LOGI("BROADCASTER", "Received %d bytes from device", len);
    // Process received data here
    ESP_LOG_BUFFER_HEX("BROADCASTER", data, len);
}

extern "C" void app_main(void) {
    ESP_LOGI("BROADCASTER", "Starting Broadcaster Device");
    
    // Create pairing instance - expecting 3 devices to pair
    g_pairing = new EspNowAutoPairing(EspNowRole::BROADCASTER, 3);
    
    // Initialize the pairing system
    esp_err_t ret = g_pairing->init();
    if (ret != ESP_OK) {
        ESP_LOGE("BROADCASTER", "Failed to initialize pairing: %s", esp_err_to_name(ret));
        return;
    }
    
    // Register receive callback
    g_pairing->registerReceiveCallback(onDataReceived);
    
    // Check if already paired
    if (g_pairing->isPaired()) {
        ESP_LOGI("BROADCASTER", "Already paired with devices, ready to broadcast");
    } else {
        ESP_LOGI("BROADCASTER", "Not yet paired, waiting for devices to connect...");
        
        // Start pairing process
        ret = g_pairing->startPairing();
        if (ret != ESP_OK) {
            ESP_LOGE("BROADCASTER", "Failed to start pairing: %s", esp_err_to_name(ret));
            return;
        }
        
        // Wait for all devices to pair
        vTaskDelay(pdMS_TO_TICKS(30000)); // Wait 30 seconds for all devices to pair
    }
    
    // Broadcast message to all paired devices every 3 seconds
    uint8_t broadcast_data[] = {0xBC, 0x01, 0x02, 0x03};
    uint32_t counter = 0;
    
    while (true) {
        if (g_pairing->isPaired()) {
            broadcast_data[1] = (counter >> 16) & 0xFF;
            broadcast_data[2] = (counter >> 8) & 0xFF;
            broadcast_data[3] = counter & 0xFF;
            counter++;
            
            esp_err_t send_ret = g_pairing->sendData(broadcast_data, sizeof(broadcast_data));
            if (send_ret == ESP_OK) {
                ESP_LOGI("BROADCASTER", "Broadcast sent to all devices (counter: %d)", counter - 1);
            } else {
                ESP_LOGE("BROADCASTER", "Failed to broadcast: %s", esp_err_to_name(send_ret));
            }
        } else {
            ESP_LOGW("BROADCASTER", "Not all devices paired yet, cannot broadcast");
        }
        
        vTaskDelay(pdMS_TO_TICKS(3000)); // 3 seconds
    }
}