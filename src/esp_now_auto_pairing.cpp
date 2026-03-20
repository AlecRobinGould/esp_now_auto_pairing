#include "esp_now_auto_pairing.h"
#include <esp_event.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <esp_private/wifi.h>
#include <nvs.h>
#include <cstring>
#include <algorithm>
#include <freertos/timers.h>

// MAC address formatting macros if not already defined
#ifndef MAC2STR
#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"
#endif

// Forward declarations
static void espnowRecvCallback(const esp_now_recv_info_t *info, const uint8_t *data, int len);
static EspNowAutoPairing* g_esp_now_instance = nullptr;

// Static recv callback implementation
static void espnowRecvCallback(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (g_esp_now_instance && info && info->src_addr)
    {
        g_esp_now_instance->handleReceivedData(info->src_addr, data, len);
    }
}

// ESP-NOW message types for pairing
enum class MessageType : uint8_t
{
    PAIRING_REQUEST = 0x01,
    PAIRING_RESPONSE = 0x02,
    PAIRING_ACK = 0x03,
    DATA_MESSAGE = 0x04
};

// Pairing message structure
struct PairingMessage
{
    MessageType type;
    uint8_t mac_addr[ESP_NOW_ETH_ALEN];
    uint8_t key[16]; // 128-bit key
    uint8_t role;    // EspNowRole cast to uint8_t
    uint8_t expected_devices;
    uint32_t timestamp;
};

EspNowAutoPairing::EspNowAutoPairing(EspNowRole role, uint8_t expectedDevices)
    : role_(role), expected_devices_(expectedDevices), state_(PairingState::UNPAIRED), 
      nvs_handle_(0), pairing_timer_(nullptr), broadcast_attempts_(0)
{
    // Initialize paired devices vector
    paired_devices_.reserve(expectedDevices);

    // Generate initial pairing key
    generatePairingKey(pairing_key_, sizeof(pairing_key_));

    ESP_LOGI(TAG, "ESP-NOW Auto Pairing initialized with role: %d, expected devices: %d",
             static_cast<int>(role_), expected_devices_);
}

EspNowAutoPairing::~EspNowAutoPairing()
{
    if (pairing_timer_ != nullptr)
    {
        xTimerStop(pairing_timer_, portMAX_DELAY);
        xTimerDelete(pairing_timer_, portMAX_DELAY);
        pairing_timer_ = nullptr;
    }
    
    if (nvs_handle_ != 0)
    {
        nvs_close(nvs_handle_);
    }
    esp_now_deinit();
}

esp_err_t EspNowAutoPairing::init()
{
    esp_err_t ret;

    // Initialize NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Initialize ESP-NOW
    ESP_ERROR_CHECK(esp_now_init());

    // Store instance pointer for recv callback
    g_esp_now_instance = this;

    // Register receive callback
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnowRecvCallback));

    // Set PMK and LMK for security
    uint8_t pmk[ESP_NOW_KEY_LEN];
    uint8_t lmk[ESP_NOW_KEY_LEN];
    generatePairingKey(pmk, ESP_NOW_KEY_LEN);
    generatePairingKey(lmk, ESP_NOW_KEY_LEN);

    ESP_ERROR_CHECK(esp_now_set_pmk(pmk));

    // Load existing pairing data from NVS
    ret = loadPairingData();
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Loaded existing pairing data from NVS");
        state_ = PairingState::PAIRED;
        
        // Re-add all stored peers
        for (const auto& dev : paired_devices_)
        {
            esp_now_peer_info_t peer_info = {};
            memcpy(peer_info.peer_addr, dev.data(), ESP_NOW_ETH_ALEN);
            memcpy(peer_info.lmk, pairing_key_, ESP_NOW_KEY_LEN);
            peer_info.channel = 0;
            peer_info.encrypt = false;  // <<< TEMPORARILY DISABLE ENCRYPTION FOR TESTING

            // Check if peer already exists
            esp_now_peer_info_t check_peer = {};
            esp_err_t check_ret = esp_now_get_peer(dev.data(), &check_peer);
            if (check_ret != ESP_OK)
            {
                esp_err_t add_ret = esp_now_add_peer(&peer_info);
                if (add_ret != ESP_OK)
                {
                    ESP_LOGW(TAG, "Failed to re-add peer on init: %s", esp_err_to_name(add_ret));
                }
                else
                {
                    ESP_LOGI(TAG, "Re-added peer from NVS: " MACSTR, MAC2STR(dev.data()));
                }
            }
        }
    }
    else
    {
        ESP_LOGI(TAG, "No existing pairing data found, starting in UNPAIRED state");
    }

    ESP_LOGI(TAG, "ESP-NOW Auto Pairing initialized successfully");
    return ESP_OK;
}

bool EspNowAutoPairing::isPaired()
{
    bool result = state_ == PairingState::PAIRED && !paired_devices_.empty();
    ESP_LOGD(TAG, "[isPaired] state=%d (PAIRED=%d), devices_count=%d, result=%d",
             static_cast<int>(state_), static_cast<int>(PairingState::PAIRED),
             static_cast<int>(paired_devices_.size()), result ? 1 : 0);
    return result;
}

esp_err_t EspNowAutoPairing::startPairing()
{
    if (state_ == PairingState::PAIRED)
    {
        ESP_LOGW(TAG, "Already paired, unpair first if needed");
        return ESP_ERR_INVALID_STATE;
    }

    state_ = PairingState::PAIRING;
    broadcast_attempts_ = 0;
    
    ESP_LOGI(TAG, "Starting pairing process as %s (NON-BLOCKING)",
             role_ == EspNowRole::MASTER ? "MASTER" : role_ == EspNowRole::SLAVE ? "SLAVE" : "BROADCASTER");

    // Create or restart pairing timer - broadcasts every 1 second
    if (pairing_timer_ == nullptr)
    {
        pairing_timer_ = xTimerCreate("pairing_timer", 1000 / portTICK_PERIOD_MS, pdTRUE, this,
                                      pairingTimerCallback);
        if (pairing_timer_ == nullptr)
        {
            ESP_LOGE(TAG, "Failed to create pairing timer");
            return ESP_ERR_NO_MEM;
        }
    }

    // Start the timer
    if (xTimerStart(pairing_timer_, portMAX_DELAY) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to start pairing timer");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Pairing started asynchronously - device will look for %d peer(s)", expected_devices_);
    return ESP_OK;
}

esp_err_t EspNowAutoPairing::unpair()
{
    esp_err_t ret = clearPairingData();
    if (ret != ESP_OK)
    {
        return ret;
    }

    paired_devices_.clear();
    state_ = PairingState::UNPAIRED;

    // Generate new key for next pairing
    generatePairingKey(pairing_key_, sizeof(pairing_key_));

    ESP_LOGI(TAG, "Device unpaired successfully");
    return ESP_OK;
}

esp_err_t EspNowAutoPairing::sendData(const uint8_t *data, size_t len)
{
    if (!isPaired())
    {
        ESP_LOGE(TAG, "Cannot send data: device not paired");
        return ESP_ERR_INVALID_STATE;
    }

    if (len > ESP_NOW_MAX_DATA_LEN)
    {
        ESP_LOGE(TAG, "Data too large: %d bytes (max %d)", len, ESP_NOW_MAX_DATA_LEN);
        return ESP_ERR_INVALID_SIZE;
    }

    // Send to all paired devices
    for (const auto &mac : paired_devices_)
    {
        esp_err_t ret = esp_now_send(mac.data(), data, len);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to send data to device: %s", esp_err_to_name(ret));
            return ret;
        }
    }

    return ESP_OK;
}

esp_err_t EspNowAutoPairing::registerReceiveCallback(std::function<void(const uint8_t *, size_t)> callback)
{
    receive_callback_ = callback;
    return ESP_OK;
}

void EspNowAutoPairing::handleReceivedData(const uint8_t *mac_addr, const uint8_t *data, size_t len)
{
    ESP_LOGI(TAG, "handleReceivedData called: mac=" MACSTR ", len=%d, data[0]=%d", 
             MAC2STR(mac_addr), len, data[0]);
    
    if (len < sizeof(MessageType))
    {
        ESP_LOGW(TAG, "Received data too small: %d", len);
        return;
    }

    MessageType msg_type = static_cast<MessageType>(data[0]);
    ESP_LOGI(TAG, "Message type: %d", static_cast<int>(msg_type));

    switch (msg_type)
    {
    case MessageType::PAIRING_REQUEST:
        ESP_LOGI(TAG, "Processing PAIRING_REQUEST");
        handlePairingRequest(mac_addr, data, len);
        break;
    case MessageType::PAIRING_RESPONSE:
        ESP_LOGI(TAG, "Processing PAIRING_RESPONSE");
        handlePairingResponse(mac_addr, data, len);
        break;
    case MessageType::PAIRING_ACK:
        ESP_LOGI(TAG, "Processing PAIRING_ACK");
        handlePairingAck(mac_addr, data, len);
        break;
    case MessageType::DATA_MESSAGE:
        ESP_LOGI(TAG, "Processing DATA_MESSAGE");
        if (receive_callback_ && isPaired())
        {
            receive_callback_(data + 1, len - 1); // Skip message type byte
        }
        break;
    default:
        ESP_LOGW(TAG, "Unknown message type: %d", static_cast<int>(msg_type));
        break;
    }
}

void EspNowAutoPairing::handlePairingRequest(const uint8_t *mac_addr, const uint8_t *data, int len)
{
    ESP_LOGI(TAG, "handlePairingRequest called: role=%d, msg_len=%d, expected_len=%d", 
             static_cast<int>(role_), len, static_cast<int>(sizeof(PairingMessage)));
    
    if (len != sizeof(PairingMessage))
    {
        ESP_LOGW(TAG, "Invalid pairing request size: got %d, expected %d", len, static_cast<int>(sizeof(PairingMessage)));
        return;
    }

    const PairingMessage *msg = reinterpret_cast<const PairingMessage *>(data);

    if (!validateMacAddress(mac_addr))
    {
        ESP_LOGW(TAG, "Invalid MAC address in pairing request");
        return;
    }

    // SLAVE should respond to pairing requests from MASTER
    // MASTER should NOT receive its own broadcast
    if (role_ == EspNowRole::MASTER)
    {
        // Check if we already have this device
        auto it = std::find_if(paired_devices_.begin(), paired_devices_.end(),
                               [mac_addr](const std::array<uint8_t, ESP_NOW_ETH_ALEN> &dev)
                               {
                                   return memcmp(dev.data(), mac_addr, ESP_NOW_ETH_ALEN) == 0;
                               });

        if (it != paired_devices_.end())
        {
            ESP_LOGI(TAG, "Device already paired: " MACSTR, MAC2STR(mac_addr));
            return;
        }

        // Check if peer already exists in ESP-NOW
        esp_now_peer_info_t existing_peer = {};
        esp_err_t check_ret = esp_now_get_peer(mac_addr, &existing_peer);
        
        if (check_ret != ESP_OK)
        {
            // Add new peer
            memset(&peer_info_, 0, sizeof(peer_info_));
            memcpy(peer_info_.peer_addr, mac_addr, ESP_NOW_ETH_ALEN);
            memcpy(peer_info_.lmk, msg->key, ESP_NOW_KEY_LEN);
            peer_info_.channel = 0; // Use current channel
            peer_info_.encrypt = false; // Unencrypted for pairing phase

            esp_err_t ret = esp_now_add_peer(&peer_info_);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to add peer: %s", esp_err_to_name(ret));
                return;
            }
        }
        else
        {
            // Update key if needed
            memcpy(existing_peer.lmk, msg->key, ESP_NOW_KEY_LEN);
            esp_err_t ret = esp_now_mod_peer(&existing_peer);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to mod peer: %s", esp_err_to_name(ret));
                return;
            }
        }

        // Send pairing response
        PairingMessage response;
        response.type = MessageType::PAIRING_RESPONSE;
        uint8_t mac[ESP_NOW_ETH_ALEN];
        esp_wifi_get_mac(WIFI_IF_STA, mac);
        memcpy(response.mac_addr, mac, ESP_NOW_ETH_ALEN);
        memcpy(response.key, pairing_key_, sizeof(pairing_key_));
        response.role = static_cast<uint8_t>(role_);
        response.expected_devices = expected_devices_;
        response.timestamp = esp_log_timestamp();

        esp_err_t ret = esp_now_send(mac_addr, reinterpret_cast<uint8_t *>(&response), sizeof(response));
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to send pairing response: %s", esp_err_to_name(ret));
            return;
        }

        ESP_LOGI(TAG, "Sent pairing response to " MACSTR, MAC2STR(mac_addr));
    }
    else if (role_ == EspNowRole::SLAVE)
    {
        // SLAVE receives pairing request from MASTER
        ESP_LOGI(TAG, "Slave received pairing request from " MACSTR, MAC2STR(mac_addr));
        
        // Check if this master is already in our paired devices
        auto it = std::find_if(paired_devices_.begin(), paired_devices_.end(),
                               [mac_addr](const std::array<uint8_t, ESP_NOW_ETH_ALEN> &dev)
                               {
                                   return memcmp(dev.data(), mac_addr, ESP_NOW_ETH_ALEN) == 0;
                               });

        // Add to paired_devices if not already there - SLAVE should track the MASTER
        if (it == paired_devices_.end())
        {
            std::array<uint8_t, ESP_NOW_ETH_ALEN> device_mac;
            memcpy(device_mac.data(), mac_addr, ESP_NOW_ETH_ALEN);
            paired_devices_.push_back(device_mac);
            ESP_LOGI(TAG, "Slave: Added MASTER to paired_devices: " MACSTR, MAC2STR(mac_addr));
        }
        
        // Check if peer already exists
        esp_now_peer_info_t existing_peer = {};
        esp_err_t check_ret = esp_now_get_peer(mac_addr, &existing_peer);
        
        if (check_ret == ESP_OK)
        {
            ESP_LOGI(TAG, "Peer already exists, updating with new key");
            // Update existing peer
            memcpy(existing_peer.lmk, msg->key, ESP_NOW_KEY_LEN);
            existing_peer.encrypt = false; // Unencrypted for pairing phase
            esp_err_t ret = esp_now_mod_peer(&existing_peer);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "Slave: Failed to mod existing peer: %s", esp_err_to_name(ret));
                return;
            }
        }
        else
        {
            // Add new peer
            memset(&peer_info_, 0, sizeof(peer_info_));
            memcpy(peer_info_.peer_addr, mac_addr, ESP_NOW_ETH_ALEN);
            memcpy(peer_info_.lmk, msg->key, ESP_NOW_KEY_LEN);
            peer_info_.channel = 0;
            peer_info_.encrypt = false; // Unencrypted for pairing phase

            esp_err_t ret = esp_now_add_peer(&peer_info_);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "Slave: Failed to add master peer: %s", esp_err_to_name(ret));
                return;
            }
        }

        // Send pairing response back to MASTER
        PairingMessage response;
        response.type = MessageType::PAIRING_RESPONSE;
        uint8_t mac[ESP_NOW_ETH_ALEN];
        esp_wifi_get_mac(WIFI_IF_STA, mac);
        memcpy(response.mac_addr, mac, ESP_NOW_ETH_ALEN);
        memcpy(response.key, pairing_key_, sizeof(pairing_key_));
        response.role = static_cast<uint8_t>(role_);
        response.expected_devices = expected_devices_;
        response.timestamp = esp_log_timestamp();

        ESP_LOGI(TAG, "SLAVE: Sending PAIRING_RESPONSE to " MACSTR " (msg_len=%d)", 
                 MAC2STR(mac_addr), static_cast<int>(sizeof(response)));
        
        esp_err_t ret = esp_now_send(mac_addr, reinterpret_cast<uint8_t *>(&response), sizeof(response));
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "SLAVE: Failed to send pairing response: %s", esp_err_to_name(ret));
            return;
        }

        ESP_LOGI(TAG, ">>> SLAVE sent PAIRING_RESPONSE to " MACSTR, MAC2STR(mac_addr));
        state_ = PairingState::PAIRING;  // Set to pairing state, waiting for ACK
    }
}

void EspNowAutoPairing::handlePairingResponse(const uint8_t *mac_addr, const uint8_t *data, int len)
{
    ESP_LOGI(TAG, "handlePairingResponse called: role=%d, state=%d, msg_len=%d, expected_len=%d", 
             static_cast<int>(role_), static_cast<int>(state_), len, static_cast<int>(sizeof(PairingMessage)));
    
    if (len != sizeof(PairingMessage))
    {
        ESP_LOGW(TAG, "Invalid pairing response size: got %d, expected %d", len, static_cast<int>(sizeof(PairingMessage)));
        return;
    }

    const PairingMessage *msg = reinterpret_cast<const PairingMessage *>(data);

    if (role_ == EspNowRole::MASTER)
    {
        // MASTER receives PAIRING_RESPONSE from SLAVE
        ESP_LOGI(TAG, ">>> MASTER received PAIRING_RESPONSE from " MACSTR, MAC2STR(mac_addr));
        
        if (state_ != PairingState::PAIRING)
        {
            ESP_LOGW(TAG, "MASTER: Received pairing response but not in PAIRING state (state=%d)", static_cast<int>(state_));
            return;
        }

        // Check if peer already exists
        esp_now_peer_info_t existing_peer = {};
        esp_err_t check_ret = esp_now_get_peer(mac_addr, &existing_peer);
        
        if (check_ret != ESP_OK)
        {
            // Add new peer
            memset(&peer_info_, 0, sizeof(peer_info_));
            memcpy(peer_info_.peer_addr, mac_addr, ESP_NOW_ETH_ALEN);
            memcpy(peer_info_.lmk, msg->key, ESP_NOW_KEY_LEN);
            peer_info_.channel = 0;
            peer_info_.encrypt = false; // Unencrypted for pairing phase

            esp_err_t ret = esp_now_add_peer(&peer_info_);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "Master: Failed to add peer: %s", esp_err_to_name(ret));
                return;
            }
        }
        else
        {
            // Update key if needed
            memcpy(existing_peer.lmk, msg->key, ESP_NOW_KEY_LEN);
            existing_peer.encrypt = false; // Unencrypted for pairing phase
            esp_err_t ret = esp_now_mod_peer(&existing_peer);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "Master: Failed to mod peer: %s", esp_err_to_name(ret));
                return;
            }
        }

        // Add to paired devices if not already there
        auto it = std::find_if(paired_devices_.begin(), paired_devices_.end(),
                               [mac_addr](const std::array<uint8_t, ESP_NOW_ETH_ALEN> &dev)
                               {
                                   return memcmp(dev.data(), mac_addr, ESP_NOW_ETH_ALEN) == 0;
                               });

        if (it == paired_devices_.end())
        {
            std::array<uint8_t, ESP_NOW_ETH_ALEN> device_mac;
            memcpy(device_mac.data(), mac_addr, ESP_NOW_ETH_ALEN);
            paired_devices_.push_back(device_mac);
            ESP_LOGI(TAG, "MASTER: Added SLAVE to paired_devices (size now %d)", paired_devices_.size());
        }

        ESP_LOGI(TAG, "MASTER: Checking if complete - paired_devices_.size()=%d, expected_devices_=%d", 
                 paired_devices_.size(), expected_devices_);
        
        // Send ACK
        PairingMessage ack;
        ack.type = MessageType::PAIRING_ACK;
        uint8_t mac[ESP_NOW_ETH_ALEN];
        esp_wifi_get_mac(WIFI_IF_STA, mac);
        memcpy(ack.mac_addr, mac, ESP_NOW_ETH_ALEN);
        memcpy(ack.key, pairing_key_, sizeof(pairing_key_));
        ack.role = static_cast<uint8_t>(role_);
        ack.expected_devices = expected_devices_;
        ack.timestamp = esp_log_timestamp();

        esp_err_t ret = esp_now_send(mac_addr, reinterpret_cast<uint8_t *>(&ack), sizeof(ack));
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "MASTER: Failed to send ACK: %s", esp_err_to_name(ret));
            return;
        }

        ESP_LOGI(TAG, ">>> MASTER sent PAIRING_ACK to " MACSTR, MAC2STR(mac_addr));
        
        // Mark as paired if all expected devices are connected
        if (paired_devices_.size() >= expected_devices_)
        {
            state_ = PairingState::PAIRED;
            ESP_LOGI(TAG, "*** MASTER STATE CHANGED TO PAIRED ***");
            savePairingData();
            ESP_LOGI(TAG, "MASTER Pairing completed! Master is now PAIRED with %zu device(s)", paired_devices_.size());
        }
    }
    else
    {
        // SLAVE should not receive pairing response
        ESP_LOGW(TAG, "Slave received pairing response - ignoring");
        return;
    }
}

void EspNowAutoPairing::handlePairingAck(const uint8_t *mac_addr, const uint8_t *data, int len)
{
    ESP_LOGI(TAG, "handlePairingAck called: role=%d, state=%d, msg_len=%d, expected_len=%d", 
             static_cast<int>(role_), static_cast<int>(state_), len, static_cast<int>(sizeof(PairingMessage)));
    
    if (len != sizeof(PairingMessage))
    {
        ESP_LOGW(TAG, "Invalid pairing ACK size: got %d, expected %d", len, static_cast<int>(sizeof(PairingMessage)));
        return;
    }

    if (role_ == EspNowRole::SLAVE)
    {
        // SLAVE receives ACK from MASTER when pairing is complete
        ESP_LOGI(TAG, ">>> SLAVE received PAIRING_ACK from " MACSTR, MAC2STR(mac_addr));
        
        if (state_ != PairingState::PAIRING)
        {
            ESP_LOGW(TAG, "SLAVE: Received ACK but not in PAIRING state (state=%d)", static_cast<int>(state_));
            return;
        }

        // Add master to paired devices if not already there
        auto it = std::find_if(paired_devices_.begin(), paired_devices_.end(),
                               [mac_addr](const std::array<uint8_t, ESP_NOW_ETH_ALEN> &dev)
                               {
                                   return memcmp(dev.data(), mac_addr, ESP_NOW_ETH_ALEN) == 0;
                               });

        if (it == paired_devices_.end())
        {
            std::array<uint8_t, ESP_NOW_ETH_ALEN> device_mac;
            memcpy(device_mac.data(), mac_addr, ESP_NOW_ETH_ALEN);
            paired_devices_.push_back(device_mac);
            ESP_LOGI(TAG, "SLAVE: Added MASTER to paired_devices (size now %d)", paired_devices_.size());
        }

        state_ = PairingState::PAIRED;
        ESP_LOGI(TAG, "*** SLAVE STATE CHANGED TO PAIRED ***");
        savePairingData();
        ESP_LOGI(TAG, "SLAVE successfully paired with MASTER!"); 
    }
    else
    {
        // MASTER/Other should not receive pairing ACK
        ESP_LOGW(TAG, "Non-slave (role=%d) received pairing ACK - ignoring", static_cast<int>(role_));
        return;
    }
}

void EspNowAutoPairing::pairingTimerCallback(TimerHandle_t xTimer)
{
    // Get the this pointer from timer
    EspNowAutoPairing* instance = static_cast<EspNowAutoPairing*>(pvTimerGetTimerID(xTimer));
    if (!instance)
        return;

    // If already paired, stop the timer
    if (instance->isPaired())
    {
        xTimerStop(xTimer, 0);
        return;
    }

    // Stop if exceeded max attempts
    if (instance->broadcast_attempts_ >= instance->MAX_BROADCAST_ATTEMPTS)
    {
        ESP_LOGW(instance->TAG, "Pairing timeout: max broadcast attempts reached");
        xTimerStop(xTimer, 0);
        return;
    }

    // Send pairing request (for MASTER/BROADCASTER role)
    if (instance->role_ == EspNowRole::MASTER || instance->role_ == EspNowRole::BROADCASTER)
    {
        instance->sendPairingRequest();
        instance->broadcast_attempts_++;
    }
    // SLAVE waits passively, timer has no effect for SLAVE
}

esp_err_t EspNowAutoPairing::sendPairingRequest()
{
    // Can be called by MASTER/BROADCASTER to broadcast, or SLAVE during pairing
    uint8_t broadcast_mac[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    // Ensure broadcast peer exists - but only add once
    esp_now_peer_info_t check_peer = {};
    if (esp_now_get_peer(broadcast_mac, &check_peer) != ESP_OK)
    {
        // Peer doesn't exist, add it
        esp_now_peer_info_t broadcast_peer = {};
        memcpy(broadcast_peer.peer_addr, broadcast_mac, ESP_NOW_ETH_ALEN);
        broadcast_peer.channel = 0;
        broadcast_peer.encrypt = false;
        
        esp_err_t add_ret = esp_now_add_peer(&broadcast_peer);
        if (add_ret != ESP_OK && add_ret != ESP_ERR_ESPNOW_EXIST)
        {
            ESP_LOGW(TAG, "Warning adding broadcast peer: %s", esp_err_to_name(add_ret));
        }
    }

    PairingMessage request;
    request.type = MessageType::PAIRING_REQUEST;
    uint8_t local_mac[ESP_NOW_ETH_ALEN];
    esp_wifi_get_mac(WIFI_IF_STA, local_mac);
    memcpy(request.mac_addr, local_mac, ESP_NOW_ETH_ALEN);
    memcpy(request.key, pairing_key_, sizeof(pairing_key_));
    request.role = static_cast<uint8_t>(role_);
    request.expected_devices = expected_devices_;
    request.timestamp = esp_log_timestamp();

    esp_err_t ret = esp_now_send(broadcast_mac, reinterpret_cast<uint8_t *>(&request), sizeof(request));
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to send pairing request: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Broadcasted pairing request (role: %d, expected: %d)", 
             static_cast<int>(role_), expected_devices_);
    return ESP_OK;
}

esp_err_t EspNowAutoPairing::savePairingData()
{
    esp_err_t ret = nvs_open("espnow_pairing", NVS_READWRITE, &nvs_handle_);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    // Save role
    ret = nvs_set_u8(nvs_handle_, "role", static_cast<uint8_t>(role_));
    if (ret != ESP_OK)
        goto cleanup;

    // Save expected devices
    ret = nvs_set_u8(nvs_handle_, "expected_dev", expected_devices_);
    if (ret != ESP_OK)
        goto cleanup;

    // Save pairing key
    ret = nvs_set_blob(nvs_handle_, "pairing_key", pairing_key_, sizeof(pairing_key_));
    if (ret != ESP_OK)
        goto cleanup;

    // Save number of paired devices
    ret = nvs_set_u8(nvs_handle_, "num_devices", paired_devices_.size());
    if (ret != ESP_OK)
        goto cleanup;

    // Save paired device MACs
    for (size_t i = 0; i < paired_devices_.size(); ++i)
    {
        char key[16];
        snprintf(key, sizeof(key), "device_%d", static_cast<int>(i));
        ret = nvs_set_blob(nvs_handle_, key, paired_devices_[i].data(), ESP_NOW_ETH_ALEN);
        if (ret != ESP_OK)
            goto cleanup;
    }

    ret = nvs_commit(nvs_handle_);

cleanup:
    nvs_close(nvs_handle_);
    nvs_handle_ = 0;

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to save pairing data: %s", esp_err_to_name(ret));
    }
    else
    {
        ESP_LOGI(TAG, "Pairing data saved successfully");
    }

    return ret;
}

esp_err_t EspNowAutoPairing::loadPairingData()
{
    esp_err_t ret = nvs_open("espnow_pairing", NVS_READONLY, &nvs_handle_);
    if (ret != ESP_OK)
    {
        if (ret == ESP_ERR_NVS_NOT_FOUND)
        {
            ESP_LOGI(TAG, "No existing pairing data found");
            return ESP_ERR_NVS_NOT_FOUND;
        }
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    // Load role
    uint8_t saved_role = 0;
    uint8_t saved_expected = 0;
    uint8_t num_devices = 0;
    size_t key_len = sizeof(pairing_key_);

    ret = nvs_get_u8(nvs_handle_, "role", &saved_role);
    if (ret != ESP_OK)
        goto cleanup;

    if (saved_role != static_cast<uint8_t>(role_))
    {
        ESP_LOGW(TAG, "Saved role (%d) doesn't match current role (%d)",
                 saved_role, static_cast<int>(role_));
        ret = ESP_ERR_INVALID_STATE;
        goto cleanup;
    }

    // Load expected devices
    ret = nvs_get_u8(nvs_handle_, "expected_dev", &saved_expected);
    if (ret != ESP_OK)
        goto cleanup;

    if (saved_expected != expected_devices_)
    {
        ESP_LOGW(TAG, "Saved expected devices (%d) doesn't match current (%d)",
                 saved_expected, expected_devices_);
        ret = ESP_ERR_INVALID_STATE;
        goto cleanup;
    }

    // Load pairing key
    ret = nvs_get_blob(nvs_handle_, "pairing_key", pairing_key_, &key_len);
    if (ret != ESP_OK)
        goto cleanup;

    // Load number of devices
    ret = nvs_get_u8(nvs_handle_, "num_devices", &num_devices);
    if (ret != ESP_OK)
        goto cleanup;

    // Load device MACs
    paired_devices_.clear();
    for (uint8_t i = 0; i < num_devices; ++i)
    {
        char key[16];
        snprintf(key, sizeof(key), "device_%d", i);

        std::array<uint8_t, ESP_NOW_ETH_ALEN> mac;
        size_t mac_len = ESP_NOW_ETH_ALEN;
        ret = nvs_get_blob(nvs_handle_, key, mac.data(), &mac_len);
        if (ret != ESP_OK)
            goto cleanup;

        paired_devices_.push_back(mac);

        // Re-add peer
        memset(&peer_info_, 0, sizeof(peer_info_));
        memcpy(peer_info_.peer_addr, mac.data(), ESP_NOW_ETH_ALEN);
        memcpy(peer_info_.lmk, pairing_key_, ESP_NOW_KEY_LEN);
        peer_info_.channel = 0;
        peer_info_.encrypt = false;  // <<< TEMPORARILY DISABLE ENCRYPTION FOR TESTING

        esp_err_t peer_ret = esp_now_add_peer(&peer_info_);
        if (peer_ret != ESP_OK)
        {
            ESP_LOGW(TAG, "Failed to re-add peer %d: %s", i, esp_err_to_name(peer_ret));
        }
    }

cleanup:
    nvs_close(nvs_handle_);
    nvs_handle_ = 0;

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to load pairing data: %s", esp_err_to_name(ret));
        paired_devices_.clear();
    }
    else
    {
        ESP_LOGI(TAG, "Loaded pairing data for %d device(s)", paired_devices_.size());
    }

    return ret;
}

esp_err_t EspNowAutoPairing::clearPairingData()
{
    esp_err_t ret = nvs_open("espnow_pairing", NVS_READWRITE, &nvs_handle_);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open NVS for clearing: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_erase_all(nvs_handle_);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to erase NVS data: %s", esp_err_to_name(ret));
    }
    else
    {
        ret = nvs_commit(nvs_handle_);
        ESP_LOGI(TAG, "Pairing data cleared");
    }

    nvs_close(nvs_handle_);
    nvs_handle_ = 0;

    return ret;
}

esp_err_t EspNowAutoPairing::generatePairingKey(uint8_t *key, size_t len)
{
    if (!key || len == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    // Use ESP32 hardware RNG for secure key generation
    for (size_t i = 0; i < len; ++i)
    {
        key[i] = esp_random() & 0xFF;
    }

    return ESP_OK;
}

bool EspNowAutoPairing::validateMacAddress(const uint8_t *mac)
{
    if (!mac)
        return false;

    // Check for broadcast address
    bool is_broadcast = true;
    for (int i = 0; i < ESP_NOW_ETH_ALEN; ++i)
    {
        if (mac[i] != 0xFF)
        {
            is_broadcast = false;
            break;
        }
    }

    // Check for zero address
    bool is_zero = true;
    for (int i = 0; i < ESP_NOW_ETH_ALEN; ++i)
    {
        if (mac[i] != 0x00)
        {
            is_zero = false;
            break;
        }
    }

    return !is_broadcast && !is_zero;
}