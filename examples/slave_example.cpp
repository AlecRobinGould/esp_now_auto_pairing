/**
 * Slave Example for ESP-NOW Auto Pairing Library
 * 
 * This example demonstrates how to use the library as a Slave device
 * that initiates pairing with a Master device.
 */

#include <esp_now_auto_pairing.h>

// Global instance
EspNowAutoPairing* g_pairing = nullptr;

// Callback for receiving data from master
void onDataReceived(const uint8_t* data, size_t len) {
    ESP_LOGI("SLAVE", "Received %d bytes from master", len);
    // Process received data here
    ESP_LOG_BUFFER_HEX("SLAVE", data, len);
}

extern "C" void app_main(void) {
    ESP_LOGI("SLAVE", "Starting Slave Device");
    
    // Create pairing instance - slave expects 1 master
    g_pairing = new EspNowAutoPairing(EspNowRole::SLAVE, 1);
    
    // Initialize the pairing system
    esp_err_t ret = g_pairing->init();
    if (ret != ESP_OK) {
        ESP_LOGE("SLAVE", "Failed to initialize pairing: %s", esp_err_to_name(ret));
        return;
    }
    
    // Register receive callback
    g_pairing->registerReceiveCallback(onDataReceived);
    
    // Check if already paired
    if (g_pairing->isPaired()) {
        ESP_LOGI("SLAVE", "Already paired with master, ready to communicate");
    } else {
        ESP_LOGI("SLAVE", "Not yet paired, initiating pairing with master...");
        
        // Start pairing process (sends pairing request)
        ret = g_pairing->startPairing();
        if (ret != ESP_OK) {
            ESP_LOGE("SLAVE", "Failed to start pairing: %s", esp_err_to_name(ret));
            return;
        }
        
        // Wait for pairing to complete
        vTaskDelay(pdMS_TO_TICKS(10000)); // Wait 10 seconds for pairing response
    }
    
    // Send acknowledgment data every 10 seconds
    uint8_t ack_data[] = {0xAC, 0xAC, 0x00, 0x00};
    uint32_t counter = 0;
    
    while (true) {
        if (g_pairing->isPaired()) {
            ack_data[2] = (counter >> 8) & 0xFF;
            ack_data[3] = counter & 0xFF;
            counter++;
            
            esp_err_t send_ret = g_pairing->sendData(ack_data, sizeof(ack_data));
            if (send_ret == ESP_OK) {
                ESP_LOGI("SLAVE", "Acknowledgment sent (counter: %d)", counter - 1);
            } else {
                ESP_LOGE("SLAVE", "Failed to send acknowledgment: %s", esp_err_to_name(send_ret));
            }
        } else {
            ESP_LOGW("SLAVE", "Not paired yet, cannot send data");
        }
        
        vTaskDelay(pdMS_TO_TICKS(10000)); // 10 seconds
    }
}