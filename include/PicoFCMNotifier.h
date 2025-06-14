/**
 * PicoFCMNotifier.h - Library for WiFi provisioning and FCM notifications.
 *
 * This library provides a secure way to provision WiFi credentials and
 * Firebase Cloud Messaging (FCM) details to a Raspberry Pi Pico W device 
 * using Bluetooth Low Energy (BLE). It can then send FCM notifications
 * via an HTTPS request to a Firebase Function.
 */

#ifndef PICO_FCM_NOTIFIER_H
#define PICO_FCM_NOTIFIER_H

#include <Arduino.h>
#include <WiFi.h>
#include <BTstackLib.h>
#include <BLESecure.h>
#include <BLENotify.h>
#include <LittleFS.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Maximum number of WiFi networks that can be stored
#define MAX_WIFI_NETWORKS 5
// Maximum length for SSID and password
#define MAX_SSID_LENGTH 32
#define MAX_PASSWORD_LENGTH 64
// File used to store WiFi credentials
#define WIFI_CONFIG_FILE "/wifi_config.json"

// Maximum length for FCM URL and Token
#define MAX_FCM_URL_LENGTH 256
#define MAX_FCM_TOKEN_LENGTH 256

// Status of the WiFi provisioning process
typedef enum
{
    PROVISION_IDLE = 0,
    PROVISION_STARTED = 1,
    PROVISION_COMPLETE = 2,
    PROVISION_FAILED = 3,
    PROVISION_CONNECTING = 4,
    PROVISION_CONNECTED = 5
} PicoWiFiProvisioningStatus;

// Structure to hold WiFi network credentials
typedef struct
{
    char ssid[MAX_SSID_LENGTH + 1];
    char password[MAX_PASSWORD_LENGTH + 1];
    bool enabled;
} WiFiNetworkConfig;

class PicoFCMNotifierClass
{
public:
    PicoFCMNotifierClass();

    // Initialize the WiFi provisioning service
    bool begin(const char *deviceName = "PicoFCM", BLESecurityLevel securityLevel = SECURITY_HIGH, io_capability_t ioCapability = IO_CAPABILITY_DISPLAY_YES_NO);

    // Process BLE and WiFi events - call this in your loop
    void loop();

    // Save a new WiFi network configuration
    bool saveNetwork(const char *ssid, const char *password);

    // Connect to stored WiFi networks (try each one until successful)
    bool connectToStoredNetworks();

    // Connect to a specific network
    void connectToNetwork(const char *ssid, const char *password);

    // Erase all stored WiFi networks
    bool clearNetworks();

    // Get the number of stored networks
    uint8_t getNetworkCount();

    // Get the current provisioning status
    PicoWiFiProvisioningStatus getStatus();

    // Set callback for status changes
    void setStatusCallback(void (*callback)(PicoWiFiProvisioningStatus status));

    // Set callback for when WiFi connection status changes
    void setWiFiStatusCallback(void (*callback)(wl_status_t status));

    // Set BLE connection state callback
    void setBLEConnectionStateCallback(void (*callback)(bool isConnected));

    // Set callback for displaying passkey during pairing
    void setPasskeyDisplayCallback(void (*callback)(uint32_t passkey));

    // Set callback for numeric comparison during pairing
    void setNumericComparisonCallback(void (*callback)(uint32_t passkey, BLEDevice *device));

    // Accept or reject numeric comparison
    void acceptNumericComparison(bool accept);

    // Allow BLE connections when already connected to WiFi
    void allowProvisioningWhenConnected(bool allow);

    // Get the RSSI of the current WiFi connection
    int32_t getRSSI();

    // Handle BLE device connection events
    void handleDeviceConnected(BLEStatus status, BLEDevice *device);

    // Handle BLE device disconnection events
    void handleDeviceDisconnected(BLEDevice *device);

    // Handle BLE GATT write events
    int handleGattWrite(uint16_t characteristic_id, uint8_t *buffer, uint16_t buffer_size);

    // Handle BLE GATT read events
    uint16_t handleGattRead(uint16_t characteristic_id, uint8_t *buffer, uint16_t buffer_size);

    // Update the pairing status characteristic
    void updatePairingStatusCharacteristic(bool isPaired);
    
    // Send an FCM notification to the connected device
    bool sendNotification(const char *title, const char *body);

private:
    // Track the currently connected device
    BLEDevice *_connectedDevice;

    // Current status of the provisioning process
    PicoWiFiProvisioningStatus _status;

    // Array of stored WiFi networks
    WiFiNetworkConfig _networks[MAX_WIFI_NETWORKS];

    // Number of stored networks
    uint8_t _networkCount;

    // Track connection attempt start time
    unsigned long _connectionStartTime;

    // WiFi connection timeout (15 seconds)
    static const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;

    // Callbacks
    void (*_statusCallback)(PicoWiFiProvisioningStatus status);
    void (*_wifiStatusCallback)(wl_status_t status);
    void (*_bleConnectionStateCallback)(bool isConnected);

    // BLE related handles
    UUID _serviceUUID;
    UUID _ssidCharUUID;
    UUID _passwordCharUUID;
    UUID _commandCharUUID;
    UUID _pairingStatusCharUUID;
    UUID _fcmUrlCharUUID;
    UUID _fcmTokenCharUUID;
    uint16_t _ssidCharHandle;
    uint16_t _passwordCharHandle;
    uint16_t _commandCharHandle;
    uint16_t _pairingStatusCharHandle;
    uint16_t _fcmUrlCharHandle;
    uint16_t _fcmTokenCharHandle;

    // Flag for allowing provisioning when already connected
    bool _allowProvisioningWhenConnected;

    // String buffers for receiving WiFi credentials
    char _receivedSSID[MAX_SSID_LENGTH + 1];
    char _receivedPassword[MAX_PASSWORD_LENGTH + 1];

    // String buffers for receiving FCM credentials
    char _receivedFcmUrl[MAX_FCM_URL_LENGTH + 1];
    char _receivedFcmToken[MAX_FCM_TOKEN_LENGTH + 1];

    // Stored FCM credentials
    char _fcmUrl[MAX_FCM_URL_LENGTH + 1];
    char _fcmToken[MAX_FCM_TOKEN_LENGTH + 1];

    // Load stored WiFi networks from flash
    bool loadNetworksFromFlash();

    // Save WiFi networks to flash
    bool saveNetworksToFlash();

    // Set the current status and call the callback if registered
    void setStatus(PicoWiFiProvisioningStatus status);

    // Setup BLE service and characteristics
    void setupBLEService();

    // Process WiFi commands
    void processCommand(uint8_t command);

    // Load configuration from flash
    bool loadConfigFromFlash();
    // Save configuration to flash
    bool saveConfigToFlash();

};

// Global instance
extern PicoFCMNotifierClass PicoFCMNotifier;

// Commands that can be sent to the command characteristic
enum WiFiCommands
{
    CMD_SAVE_NETWORK = 0x01,
    CMD_CONNECT = 0x02,
    CMD_CLEAR_NETWORKS = 0x03,
    CMD_GET_STATUS = 0x04,
    CMD_DISCONNECT = 0x05,
    CMD_START_SCAN = 0x06,
    CMD_GET_SCAN_RESULTS = 0x07
};

// Status codes for the status characteristic
enum WiFiStatusCodes
{
    STATUS_IDLE = 0x00,
    STATUS_CONNECTING = 0x01,
    STATUS_CONNECTED = 0x02,
    STATUS_FAILED = 0x03,
    STATUS_AP_MODE = 0x04,
    STATUS_SAVING = 0x05,
    STATUS_SAVED = 0x06,
    STATUS_SCANNING = 0x07,
    STATUS_SCAN_COMPLETE = 0x08
};

// Pairing status codes for the pairing status characteristic
enum PairingStatusCodes
{
    PAIRING_STATUS_NOT_PAIRED = 0x00,
    PAIRING_STATUS_PAIRED = 0x01
};

#endif // PICO_FCM_NOTIFIER_H