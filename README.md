# Pico FCM Notifier

[![Work in Progress](https://img.shields.io/badge/status-work%20in%20progress-yellow)](https://example.com/your-project-status-page)

A PlatformIO library for securely provisioning WiFi and Firebase Cloud Messaging (FCM) credentials to Raspberry Pi Pico W boards over BLE (Bluetooth Low Energy), and for sending FCM notifications.

**TODO:** publish to PlatformIO registry

## Overview

This library provides a simple and secure way to provision a Raspberry Pi Pico W device with both WiFi credentials and FCM configuration (Cloud Function URL and a device token) using Bluetooth Low Energy (BLE) with secure pairing.  Once connected to WiFi, the device can send push notifications via an HTTPS request to your Firebase Cloud Function. 



It is designed for the `arduino-pico` core and requires the Pico W variant with onboard WiFi and Bluetooth.

## Features

- **Secure BLE Provisioning:** Receive WiFi SSID, password, FCM URL, and FCM token securely over a paired BLE connection. 
- **Send FCM Notifications:** Easily send notifications with a title and body to a configured Firebase project. 
- **Secure Pairing:** Utilizes the pico-ble-secure library for secure bonding and encryption during provisioning.
- **Credential Storage:** Saves WiFi network and FCM configurations to LittleFS flash memory for persistence. 
- **Automatic Connection:** Attempts to connect to stored WiFi networks on startup. 
- **Status Callbacks:** Provides callbacks to monitor the provisioning process, WiFi status, and BLE connection state. 

## Compatibility

- **Hardware:** Raspberry Pi Pico W
- **Framework:** Arduino
- **Core:** earlephilhower/arduino-pico
- **PlatformIO Platform:** maxgerhardt/platform-raspberrypi

## Dependencies

- [arduino-pico](https://github.com/earlephilhower/arduino-pico) core with BLE and WiFi support
- [pico-ble-secure](https://github.com/IoT-gamer/pico-ble-secure) library for secure BLE connections
- [pico-ble-notify](https://github.com/IoT-gamer/pico-ble-notify) library for BLE notifications
- [ArduinoJson](https://arduinojson.org/) library for configuration storage

## Installation

### Using PlatformIO Registry

1. Add the library to your `platformio.ini`:

```ini
lib_deps =
    ; If published to PlatformIO registry:Add commentMore actions
    ; pico-fcm-notifier
    ; Otherwise, use repo URL:
    https://github.com/IoT-gamer/pico-fcm-notifier   
```

## Example Project
Check out the [BasicNotification]((/examples/BasicNotification)) example for a complete implementation including: 

Onboard LED for status indication (connecting vs. connected) 


GPIO LED for BLE connection status 


BOOTSEL button handling to clear all stored networks 
A dedicated button to trigger and send an FCM notification 

WiFi connection management and BLE pairing with feedback

### Notes
- Need to connect to Serial Monitor
- Pairing on android may show duplicate requests (click both)
- Pairing request will timout quickly
- BLE will dosconnect after receiving WiFi credentials
- May take a few seconds to connect to WiFi after provisioning

## Usage

Here's a basic example of how to use the library:

```cpp
#include <Arduino.h>
#include <WiFi.h>
#include "PicoFCMNotifier.h"

const int LED_PIN = LED_BUILTIN;
const int NOTIFY_BUTTON_PIN = 18; // Button to send a notification

// Callback for WiFi status changes
void onWiFiStatus(wl_status_t status) {
  if (status == WL_CONNECTED) {
    Serial.println("WiFi connected!");
    Serial.print("IP address: "); Serial.println(WiFi.localIP());
    digitalWrite(LED_PIN, HIGH); // LED on when connected
  } else {
    Serial.println("WiFi disconnected");
    digitalWrite(LED_PIN, LOW); // LED off
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  pinMode(NOTIFY_BUTTON_PIN, INPUT_PULLUP);
  
  // Set callback for WiFi status changes
  PicoFCMNotifier.setWiFiStatusCallback(onWiFiStatus);
  
  // Initialize the notifier service
  if (PicoFCMNotifier.begin("PicoFCM")) {
    // Try to connect to any stored WiFi networks
    if (!PicoFCMNotifier.connectToStoredNetworks()) {
      Serial.println("No stored networks. Waiting for BLE to provision...");
    }
  }
}

void loop() {
  // Process library events
  PicoFCMNotifier.loop();
  
  // Check if the notification button is pressed
  if (digitalRead(NOTIFY_BUTTON_PIN) == LOW) {
    Serial.println("Button pressed, sending notification...");
    if(PicoFCMNotifier.sendNotification("Hello from Pico!", "The button was pressed.")) {
      Serial.println("Notification sent successfully.");
    } else {
      Serial.println("Failed to send notification.");
    }
    delay(500); // Simple debounce
  }
  
  delay(10);
}
```

## Key Steps
- Include the `PicoFCMNotifier.h` header.
- Define and implement callback functions for WiFi and BLE status as needed.
- Set the callback functions using `PicoFCMNotifier.set...Callback()`.
- Initialize the service using `PicoFCMNotifier.begin()`. 
- Optionally, call `PicoFCMNotifier.connectToStoredNetworks()` in `setup()` to automatically connect if credentials exist. 
- Call  `PicoFCMNotifier.loop()` in your main `loop()` function to process events. 
- Call `PicoFCMNotifier.sendNotification()` to send a message when ready. 

## BLE Service Definition

The library creates a custom BLE service with the following characteristics:

| Characteristic | UUID | Properties | Description |
|---------------|------|------------|-------------|
| SSID | 5a67d678-6361-4f32-8396-54c6926c8fa2 | Read, Write | WiFi SSID |
| Password | 5a67d678-6361-4f32-8396-54c6926c8fa3 | Write | WiFi Password |
| Command | 5a67d678-6361-4f32-8396-54c6926c8fa4 | Write | [Control commands](#commands) |
| Pairing Status | 5a67d678-6361-4f32-8396-54c6926c8fa5 | Read, Notify | BLE pairing status |
| FCM URL | 5a67d678-6361-4f32-8396-54c6926c8fa6 | Write | FCM Cloud Function URL |
| FCM Token | 5a67d678-6361-4f32-8396-54c6926c8fa7 | Write | FCM Device Registration Token |

## Configuration
You can customize the following parameters in `PicoFCMNotifier.h`:

- `MAX_WIFI_NETWORKS`: Max number of WiFi networks to store (default: 5)
- `MAX_SSID_LENGTH`: Max length for SSID (default: 32) 
- `MAX_PASSWORD_LENGTH`: Max length for password (default: 64) 
- `MAX_FCM_URL_LENGTH`: Max length for FCM URL (default: 256) 
- `MAX_FCM_TOKEN_LENGTH`: Max length for FCM Token (default: 256) 
- `WIFI_CONFIG_FILE`: File for storing credentials (default: "/wifi_config.json")

When calling `PicoFCMNotifier.begin()`, you can configure the device name, security level, and IO capability.

## Commands

The following commands can be sent to the Command characteristic:

| Command | Value | Description |
|---------|-------|-------------|
| CMD_SAVE_NETWORK | 0x01 | Save the current SSID and password as a network |
| CMD_CONNECT | 0x02 | Connect to the specified network or stored networks |
| CMD_CLEAR_NETWORKS | 0x03 | Clear all stored networks |
| CMD_GET_STATUS | 0x04 | Request the current status (Partially implemented) |
| CMD_DISCONNECT | 0x05 | Disconnect from the current WiFi network |


## Security and IO Capabilities

The library supports different security levels and IO capabilities through the `pico-ble-secure` library. Refer to its documentation for details on:

- **Security Levels:** `SECURITY_MEDIUM`, `SECURITY_HIGH`, etc.
- **IO Capabilities:** `IO_CAPABILITY_DISPLAY_ONLY`, `IO_CAPABILITY_NO_INPUT_NO_OUTPUT`, etc.

## Mobile Applications

### Example Flutter App

A companion Flutter app is available to demonstrate the provisioning process. It allows you to connect to the Pico W, securely send WiFi and FCM credentials, and receive notifications.
- [flutter_fcm_provisioning_appp](https://github.com/IoT-gamer/flutter_fcm_provisioning_app)

## Troubleshooting
- **BLE not advertising:** Ensure the `arduino-pico` core is configured with BLE support in your platformio.ini.
WiFi connection failures: Check the SSID and password. Ensure they're valid and within length limits.
- **FCM Errors:** Verify the Cloud Function URL and device token are correct. Use the Serial Monitor to check for HTTP error codes when sending notifications. 
- **Flash storage issues:** Make sure LittleFS is properly initialized and has enough space allocated.
- **BLE pairing issues:** If bonding information is lost (e.g., after flashing the Pico), the bond must be re-established. On your mobile phone, go to Bluetooth settings, "forget" or "unpair" the Pico device, and then try pairing again.

## License

This library is released under the MIT License. See the [LICENSE](LICENSE) file for details.

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.