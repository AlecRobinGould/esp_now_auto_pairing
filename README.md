# ESP-NOW Auto-Pairing Library

A C++ library for automatic ESP-NOW pairing on ESP32 devices, designed to work with all ESP32 variants without Arduino dependencies(I dont like them and vanilla C used in ESP IDF frightens me).

<img width="1200" height="900" alt="image" src="https://github.com/user-attachments/assets/f57d68f4-200b-4f6b-91c8-d227503e8de8" />

# Prelim test board

top
<img width="587" height="631" alt="image" src="https://github.com/user-attachments/assets/f41ded2e-7dca-4edc-8c43-939bfad42a2d" />


bottom
<img width="628" height="657" alt="image" src="https://github.com/user-attachments/assets/99c44e4e-1789-4ed8-9b97-2f9910208997" />


To do:
 - add test pads (bigger silkscreen text)
 - fix mounting hole placement and clearance
 - add SPOs
 - add IMU (BMX055) DONE
 - add TVS diodes DONE
 - fix USB diff pair routing DONE
 - fix ground plane 
 - optimise component placement and component size reduction
 - replace ESP with no antenna variant, for self made antenna
 - reduce board size
 - TBC


## Features

- **Generic ESP32 Support**: Works with ESP32, ESP32-S2, ESP32-S3, ESP32-C3 so far that I've tested. Feel free to experiment.
- **Multiple Connection Types**: Supports master-slave, broadcaster, and multi-device configurations (not fully tested)
- **Persistent Pairing**: Saves pairing data to NVS (Non-Volatile Storage) to avoid re-pairing. Can be unpaired and removed from NVS. Make sure your code does not clash with this when implementing.
- **Secure and Robust (Not proven ;))**: Implements secure pairing protocols and error handling
- **Easy Integration**: Simple API for initialization, pairing check, and unpairing. I would recommend using the pairing from a button press, such as one would with a bluetooth device.

## Requirements

- ESP-IDF framework (via PlatformIO)
- ESP32 device with ESP-NOW support

## Installation

Add this library to your PlatformIO project:

```ini
lib_deps =
    https://github.com/AlecRobinGould/esp_now_auto_pairing.git
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
- Pairing data is stored securely in NVS - this thing from the ESP IDF: (https://docs.espressif.com/projects/esp-idf/en/v4.3/esp32c3/api-reference/storage/nvs_flash.html)

## Contributing

Please read CONTRIBUTING.md for details on our code of conduct and the process for submitting pull requests.

## License

This project is licensed under the MIT License - see the LICENSE file for details.
