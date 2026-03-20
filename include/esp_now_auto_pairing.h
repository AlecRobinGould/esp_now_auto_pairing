#ifndef ESP_NOW_AUTO_PAIRING_H
#define ESP_NOW_AUTO_PAIRING_H

#include <esp_now.h>
#include <nvs_flash.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_random.h>
#include <string.h>
#include <vector>
#include <array>
#include <functional>

enum class EspNowRole {
    MASTER,
    SLAVE,
    BROADCASTER
};

enum class PairingState {
    UNPAIRED,
    PAIRING,
    PAIRED
};

class EspNowAutoPairing {
public:
    /**
     * @brief Constructor for ESP-NOW Auto Pairing
     * @param role The role of this device in the network
     * @param expectedDevices Number of devices expected in the network (for master/broadcaster)
     */
    EspNowAutoPairing(EspNowRole role, uint8_t expectedDevices = 1);
    
    /**
     * @brief Destructor
     */
    ~EspNowAutoPairing();
    
    /**
     * @brief Initialize the ESP-NOW pairing system
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t init();
    
    /**
     * @brief Check if the device is already paired
     * @return true if paired, false otherwise
     */
    bool isPaired();
    
    /**
     * @brief Start the pairing process
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t startPairing();
    
    /**
     * @brief Unpair the device and clear stored data
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t unpair();
    
    /**
     * @brief Send data to paired devices
     * @param data Pointer to data buffer
     * @param len Length of data
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t sendData(const uint8_t* data, size_t len);
    
    /**
     * @brief Register callback for received data
     * @param callback Function to call when data is received
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t registerReceiveCallback(std::function<void(const uint8_t*, size_t)> callback);
    
    /**
     * @brief Get the current pairing state
     * @return Current pairing state
     */
    PairingState getState() const { return state_; }
    
    /**
     * @brief Handle received data (called by recv callback)
     * @param mac_addr MAC address of sender
     * @param data Pointer to received data
     * @param len Length of received data
     */
    void handleReceivedData(const uint8_t* mac_addr, const uint8_t* data, size_t len);

private:
    
    // Pairing message handlers
    void handlePairingRequest(const uint8_t* mac_addr, const uint8_t* data, int len);
    void handlePairingResponse(const uint8_t* mac_addr, const uint8_t* data, int len);
    void handlePairingAck(const uint8_t* mac_addr, const uint8_t* data, int len);
    
    // Send pairing request
    esp_err_t sendPairingRequest();
    
    // NVS operations
    esp_err_t savePairingData();
    esp_err_t loadPairingData();
    esp_err_t clearPairingData();
    
    // Utility functions
    esp_err_t generatePairingKey(uint8_t* key, size_t len);
    bool validateMacAddress(const uint8_t* mac);
    
    // Member variables
    EspNowRole role_;
    uint8_t expected_devices_;
    PairingState state_;
    std::vector<std::array<uint8_t, ESP_NOW_ETH_ALEN>> paired_devices_;
    uint8_t pairing_key_[16]; // 128-bit key
    std::function<void(const uint8_t*, size_t)> receive_callback_;
    
    // ESP-NOW peer info
    esp_now_peer_info_t peer_info_;
    
    // NVS handle
    nvs_handle_t nvs_handle_;
    
    // Timer handle for periodic pairing broadcast
    TimerHandle_t pairing_timer_;
    
    // Pairing broadcast attempt counter
    uint32_t broadcast_attempts_;
    static constexpr uint32_t MAX_BROADCAST_ATTEMPTS = 120;
    
    // Static timer callback wrapper
    static void pairingTimerCallback(TimerHandle_t xTimer);
    
    // Logging tag
    static constexpr const char* TAG = "ESP_NOW_PAIRING";
};

#endif // ESP_NOW_AUTO_PAIRING_H