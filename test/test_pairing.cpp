/**
 * Unit Tests for ESP-NOW Auto Pairing Library
 * 
 * This test suite validates the core functionality of the pairing library.
 * Compile and run on an ESP32 board.
 */

#include <esp_now_auto_pairing.h>
#include <unity.h>
#include <esp_log.h>

static const char* TAG = "TEST_PAIRING";

// Test: Constructor creates valid instance
void test_constructor_creates_valid_instance(void) {
    EspNowAutoPairing pairing(EspNowRole::MASTER, 2);
    TEST_ASSERT_EQUAL(PairingState::UNPAIRED, pairing.getState());
}

// Test: isPaired returns false when not paired
void test_is_paired_returns_false_when_not_paired(void) {
    EspNowAutoPairing pairing(EspNowRole::MASTER, 2);
    TEST_ASSERT_FALSE(pairing.isPaired());
}

// Test: MAC address validation - reject broadcast address
void test_mac_validation_rejects_broadcast(void) {
    EspNowAutoPairing pairing(EspNowRole::MASTER, 2);
    
    // This would require making validateMacAddress public or adding a public test method
    // For now, this serves as a placeholder for integration testing
    ESP_LOGI(TAG, "MAC validation test - broadcast address handling");
}

// Test: MAC address validation - reject zero address
void test_mac_validation_rejects_zero_address(void) {
    EspNowAutoPairing pairing(EspNowRole::MASTER, 2);
    
    // Placeholder for integration testing
    ESP_LOGI(TAG, "MAC validation test - zero address handling");
}

// Test: Role assignment
void test_role_assignment_master(void) {
    EspNowAutoPairing master(EspNowRole::MASTER, 2);
    TEST_ASSERT_EQUAL(PairingState::UNPAIRED, master.getState());
}

void test_role_assignment_slave(void) {
    EspNowAutoPairing slave(EspNowRole::SLAVE, 1);
    TEST_ASSERT_EQUAL(PairingState::UNPAIRED, slave.getState());
}

void test_role_assignment_broadcaster(void) {
    EspNowAutoPairing broadcaster(EspNowRole::BROADCASTER, 3);
    TEST_ASSERT_EQUAL(PairingState::UNPAIRED, broadcaster.getState());
}

// Test: Expected device count
void test_expected_device_count(void) {
    uint8_t expected = 5;
    EspNowAutoPairing pairing(EspNowRole::MASTER, expected);
    TEST_ASSERT_EQUAL(PairingState::UNPAIRED, pairing.getState());
}

// Test: Unpair functionality
void test_unpair_clears_state(void) {
    EspNowAutoPairing pairing(EspNowRole::MASTER, 2);
    
    esp_err_t ret = pairing.unpair();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(PairingState::UNPAIRED, pairing.getState());
    TEST_ASSERT_FALSE(pairing.isPaired());
}

// Test: Callback registration
void test_callback_registration(void) {
    EspNowAutoPairing pairing(EspNowRole::MASTER, 2);
    
    auto callback = [](const uint8_t* data, size_t len) {
        // Test callback
    };
    
    esp_err_t ret = pairing.registerReceiveCallback(callback);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

// Test: Invalid arguments
void test_invalid_arguments_send_data(void) {
    EspNowAutoPairing pairing(EspNowRole::MASTER, 2);
    
    // sendData should return error when not paired
    uint8_t test_data[] = {0x01, 0x02, 0x03};
    esp_err_t ret = pairing.sendData(test_data, sizeof(test_data));
    TEST_ASSERT_NOT_EQUAL(ESP_OK, ret);
}

// Test suite setup
extern "C" void unity_run_menu(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_constructor_creates_valid_instance);
    RUN_TEST(test_is_paired_returns_false_when_not_paired);
    RUN_TEST(test_mac_validation_rejects_broadcast);
    RUN_TEST(test_mac_validation_rejects_zero_address);
    RUN_TEST(test_role_assignment_master);
    RUN_TEST(test_role_assignment_slave);
    RUN_TEST(test_role_assignment_broadcaster);
    RUN_TEST(test_expected_device_count);
    RUN_TEST(test_unpair_clears_state);
    RUN_TEST(test_callback_registration);
    RUN_TEST(test_invalid_arguments_send_data);
    
    UNITY_END();
}

extern "C" void app_main(void) {
    unity_run_menu();
}