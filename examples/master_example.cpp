/**
 * Master Example for ESP-NOW Auto Pairing Library
 * 
 * This example demonstrates how to use the library as a Master device
 * that expects multiple slave devices to pair with it.
 */

#include <esp_now_auto_pairing.h>

// Global instance
EspNowAutoPairing* g_pairing = nullptr;

// Callback for receiving data from slaves
void onDataReceived(const uint8_t* data, size_t len) {
    ESP_LOGI("MASTER", "Received %d bytes from slave", len);
    // Process received data here
    ESP_LOG_BUFFER_HEX("MASTER", data, len);
}

extern "C" void app_main(void) {
    ESP_LOGI("MASTER", "Starting Master Device");
    
    // Create pairing instance - expecting 2 slave devices
    g_pairing = new EspNowAutoPairing(EspNowRole::MASTER, 2);
    
    // Initialize the pairing system
    esp_err_t ret = g_pairing->init();
    if (ret != ESP_OK) {
        ESP_LOGE("MASTER", "Failed to initialize pairing: %s", esp_err_to_name(ret));
        return;
    }
    
    // Register receive callback
    g_pairing->registerReceiveCallback(onDataReceived);
    
    // Check if already paired
    if (g_pairing->isPaired()) {
        ESP_LOGI("MASTER", "Already paired with devices, ready to communicate");
    } else {
        ESP_LOGI("MASTER", "Not yet paired, waiting for slaves to connect...");
        
        // Start pairing process
        ret = g_pairing->startPairing();
        if (ret != ESP_OK) {
            ESP_LOGE("MASTER", "Failed to start pairing: %s", esp_err_to_name(ret));
            return;
        }
        
        // Wait for pairing to complete (in a real app, use proper synchronization)
        vTaskDelay(pdMS_TO_TICKS(30000)); // Wait 30 seconds for slaves to pair
    }
    
    // Send test data every 5 seconds
    uint8_t test_data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    uint32_t counter = 0;
    
    while (true) {
        if (g_pairing->isPaired()) {
            test_data[4] = (counter++) & 0xFF;
            
            esp_err_t send_ret = g_pairing->sendData(test_data, sizeof(test_data));
            if (send_ret == ESP_OK) {
                ESP_LOGI("MASTER", "Data sent successfully (counter: %d)", counter - 1);
            } else {
                ESP_LOGE("MASTER", "Failed to send data: %s", esp_err_to_name(send_ret));
            }
        } else {
            ESP_LOGW("MASTER", "Not paired yet, cannot send data");
        }
        
        vTaskDelay(pdMS_TO_TICKS(5000)); // 5 seconds
    }
}