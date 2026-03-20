# ESP-NOW Auto-Pairing Library

A professional C++ library for automatic ESP-NOW pairing on ESP32 devices, designed to work with all ESP32 variants without Arduino dependencies.

## Features

- **Generic ESP32 Support**: Works with ESP32, ESP32-S2, ESP32-S3, ESP32-C3, etc.
- **Multiple Connection Types**: Supports master-slave, broadcaster, and multi-device configurations
- **Persistent Pairing**: Saves pairing data to NVS (Non-Volatile Storage) to avoid re-pairing
- **Secure and Robust**: Implements secure pairing protocols and error handling
- **Easy Integration**: Simple API for initialization, pairing check, and unpairing

## Requirements

- ESP-IDF framework (via PlatformIO)
- ESP32 device with ESP-NOW support

## Installation

Add this library to your PlatformIO project:

```ini
lib_deps =
    https://github.com/AlecRobinGould/esp-now-auto-pairing.git
```

## Usage

### Basic Initialization

```cpp
#include <esp_now_auto_pairing.h>

void app_main() {
    // Initialize ESP-NOW auto-pairing
    EspNowAutoPairing pairing(EspNowRole::MASTER, 2); // Master role, expect 2 slaves
    
    if (pairing.isPaired()) {
        // Already paired, proceed with communication
    } else {
        // Start pairing process
        pairing.startPairing();
    }
}
```

### API Reference

#### Constructor
```cpp
EspNowAutoPairing(EspNowRole role, uint8_t expectedDevices = 1);
```

- `role`: The role of this device (MASTER, SLAVE, BROADCASTER)
- `expectedDevices`: Number of devices expected in the network

#### Methods
- `bool isPaired()`: Check if device is already paired
- `esp_err_t startPairing()`: Start the pairing process
- `esp_err_t unpair()`: Remove pairing data and reset
- `esp_err_t sendData(const uint8_t* data, size_t len)`: Send data to paired devices
- `esp_err_t registerReceiveCallback(void (*callback)(const uint8_t* data, size_t len))`: Register callback for received data

## Configuration

The library uses ESP-IDF's NVS for storing pairing information. Ensure NVS is initialized in your application.

## Security Considerations

- Pairing process uses secure key exchange
- All communications are encrypted using ESP-NOW's built-in security
- Pairing data is stored securely in NVS

## Contributing

Please read CONTRIBUTING.md for details on our code of conduct and the process for submitting pull requests.

## License

This project is licensed under the MIT License - see the LICENSE file for details.