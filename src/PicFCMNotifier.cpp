 /**
 * PicoFCMNotifier.cpp - Library for WiFi provisioning and FCM notifications.
 *
 * Implementation file for the PicoFCMNotifier library.
 */

#include "PicoFCMNotifier.h"
#include <ArduinoJson.h>

// Define the UUIDs for service and characteristics
static const char *SERVICE_UUID = "5a67d678-6361-4f32-8396-54c6926c8fa1";
static const char *SSID_CHAR_UUID = "5a67d678-6361-4f32-8396-54c6926c8fa2";
static const char *PASSWORD_CHAR_UUID = "5a67d678-6361-4f32-8396-54c6926c8fa3";
static const char *COMMAND_CHAR_UUID = "5a67d678-6361-4f32-8396-54c6926c8fa4";
static const char *PAIRING_STATUS_CHAR_UUID = "5a67d678-6361-4f32-8396-54c6926c8fa5";
static const char *FCM_URL_CHAR_UUID = "5a67d678-6361-4f32-8396-54c6926c8fa6";
static const char *FCM_TOKEN_CHAR_UUID = "5a67d678-6361-4f32-8396-54c6926c8fa7";

// Global instance
PicoFCMNotifierClass PicoFCMNotifier;

// Forward declare the global callbacks for BTstack
void bleDeviceConnected(BLEStatus status, BLEDevice *device);
void bleDeviceDisconnected(BLEDevice *device);
int gattWriteCallback(uint16_t characteristic_id, uint8_t *buffer, uint16_t buffer_size);
uint16_t gattReadCallback(uint16_t characteristic_id, uint8_t *buffer, uint16_t buffer_size);

// Constructor
PicoFCMNotifierClass::PicoFCMNotifierClass() : _status(PROVISION_IDLE),
                                               _networkCount(0),
                                               _statusCallback(nullptr),
                                               _wifiStatusCallback(nullptr),
                                               _bleConnectionStateCallback(nullptr),
                                               _serviceUUID(SERVICE_UUID),
                                               _ssidCharUUID(SSID_CHAR_UUID),
                                               _passwordCharUUID(PASSWORD_CHAR_UUID),
                                               _commandCharUUID(COMMAND_CHAR_UUID),
                                               _pairingStatusCharUUID(PAIRING_STATUS_CHAR_UUID),
                                               _fcmUrlCharUUID(FCM_URL_CHAR_UUID),
                                               _fcmTokenCharUUID(FCM_TOKEN_CHAR_UUID),
                                               _ssidCharHandle(0),
                                               _passwordCharHandle(0),
                                               _commandCharHandle(0),
                                               _pairingStatusCharHandle(0),
                                               _fcmUrlCharHandle(0),
                                               _fcmTokenCharHandle(0),
                                               _allowProvisioningWhenConnected(false),
                                               _connectedDevice(nullptr),
                                               _connectionStartTime(0)
{
    // Initialize string buffers
    memset(_receivedSSID, 0, sizeof(_receivedSSID));
    memset(_receivedPassword, 0, sizeof(_receivedPassword));

    // Initialize networks array
    for (int i = 0; i < MAX_WIFI_NETWORKS; i++)
    {
        memset(_networks[i].ssid, 0, sizeof(_networks[i].ssid));
        memset(_networks[i].password, 0, sizeof(_networks[i].password));
        _networks[i].enabled = false;
    }

    // Initialize FCM buffers and storage
    memset(_receivedFcmUrl, 0, sizeof(_receivedFcmUrl));
    memset(_receivedFcmToken, 0, sizeof(_receivedFcmToken));
    memset(_fcmUrl, 0, sizeof(_fcmUrl));
    memset(_fcmToken, 0, sizeof(_fcmToken));
}

// Initialize the WiFi provisioning and FCM notifier service
bool PicoFCMNotifierClass::begin(const char *deviceName, BLESecurityLevel securityLevel, io_capability_t ioCapability)
{
    if (!LittleFS.begin())
    {
        Serial.println("Failed to initialize LittleFS");
        return false;
    }
    loadConfigFromFlash();
    BLENotify.begin();
    BTstack.setup(deviceName);
    BLESecure.begin(ioCapability);
    BLESecure.setSecurityLevel(securityLevel, true);
    BLESecure.allowReconnectionWithoutDatabaseEntry(true);
    BLESecure.requestPairingOnConnect(true);
    BLESecure.setBLEDeviceConnectedCallback(bleDeviceConnected);
    BLESecure.setBLEDeviceDisconnectedCallback(bleDeviceDisconnected);
    BTstack.setGATTCharacteristicWrite(gattWriteCallback);
    BTstack.setGATTCharacteristicRead(gattReadCallback);
    setupBLEService();
    BTstack.startAdvertising();
    Serial.println("FCM Notifier service started");
    return true;
}

// Process BLE and WiFi events
void PicoFCMNotifierClass::loop()
{
    BTstack.loop();
    BLENotify.update();

    wl_status_t currentWiFiStatus = (wl_status_t)WiFi.status();
    static wl_status_t lastReportedWiFiStatusToApp = WL_NO_SHIELD;

    if (_status == PROVISION_CONNECTING)
    {
        unsigned long currentTime = millis();
        if (currentWiFiStatus == WL_CONNECTED)
        {
            Serial.println("WiFi connected!");
            setStatus(PROVISION_CONNECTED);
        }
        else if (currentWiFiStatus == WL_CONNECT_FAILED || currentWiFiStatus == WL_NO_SSID_AVAIL)
        {
            Serial.print("WiFi connection failed: ");
            Serial.println(currentWiFiStatus);
            setStatus(PROVISION_FAILED);
        }
        else if (currentTime - _connectionStartTime > WIFI_CONNECT_TIMEOUT_MS)
        {
            Serial.println("WiFi connection timed out.");
            setStatus(PROVISION_FAILED);
            WiFi.disconnect();
        }
    }

    if (currentWiFiStatus != lastReportedWiFiStatusToApp)
    {
        if (_wifiStatusCallback)
        {
            _wifiStatusCallback(currentWiFiStatus);
        }
        lastReportedWiFiStatusToApp = currentWiFiStatus;
    }
}

// Update the pairing status characteristic
void PicoFCMNotifierClass::updatePairingStatusCharacteristic(bool isPaired)
{
    uint8_t pairingStatus = isPaired ? PAIRING_STATUS_PAIRED : PAIRING_STATUS_NOT_PAIRED;
    if (BLENotify.isSubscribed(_pairingStatusCharHandle))
    {
        BLENotify.notify(_pairingStatusCharHandle, &pairingStatus, 1);
        Serial.print("Sent pairing status update: ");
        Serial.println(pairingStatus);
    }
}

// Save a new WiFi network configuration to memory
bool PicoFCMNotifierClass::saveNetwork(const char *ssid, const char *password)
{
    if (!ssid || strlen(ssid) == 0) return false;
    
    int existingIndex = -1;
    for (int i = 0; i < _networkCount; i++)
    {
        if (strcmp(_networks[i].ssid, ssid) == 0)
        {
            existingIndex = i;
            break;
        }
    }
    if (existingIndex >= 0)
    {
        strncpy(_networks[existingIndex].password, password, MAX_PASSWORD_LENGTH);
        _networks[existingIndex].enabled = true;
    }
    else if (_networkCount < MAX_WIFI_NETWORKS)
    {
        strncpy(_networks[_networkCount].ssid, ssid, MAX_SSID_LENGTH);
        strncpy(_networks[_networkCount].password, password, MAX_PASSWORD_LENGTH);
        _networks[_networkCount].enabled = true;
        _networkCount++;
    }
    else
    {
        return false; // No room
    }
    return true;
}

// Connect to stored WiFi networks
bool PicoFCMNotifierClass::connectToStoredNetworks()
{
    if (_status == PROVISION_CONNECTING || _status == PROVISION_CONNECTED) return false;
    if (_networkCount == 0) return false;

    for (int i = 0; i < _networkCount; i++)
    {
        if (_networks[i].enabled)
        {
            Serial.print("Attempting to connect to stored network: ");
            Serial.println(_networks[i].ssid);
            connectToNetwork(_networks[i].ssid, _networks[i].password);
            return _status == PROVISION_CONNECTING;
        }
    }
    return false;
}

// Connect to a specific network
void PicoFCMNotifierClass::connectToNetwork(const char *ssid, const char *password)
{
    if (!ssid || strlen(ssid) == 0)
    {
        Serial.println("SSID is empty, connection aborted.");
        return;
    }

    setStatus(PROVISION_CONNECTING);
    Serial.print("Connecting to WiFi: ");
    Serial.println(ssid);

    BTstack.stopAdvertising();
    if (_connectedDevice != nullptr)
    {
        BTstack.bleDisconnect(_connectedDevice);
    }

    if (WiFi.status() != WL_DISCONNECTED)
    {
        WiFi.disconnect();
    }

    WiFi.begin(ssid, password);
    _connectionStartTime = millis();
}

// Erase all stored WiFi networks and config
bool PicoFCMNotifierClass::clearNetworks()
{
    for (int i = 0; i < MAX_WIFI_NETWORKS; i++)
    {
        memset(_networks[i].ssid, 0, sizeof(_networks[i].ssid));
        memset(_networks[i].password, 0, sizeof(_networks[i].password));
        _networks[i].enabled = false;
    }
    _networkCount = 0;
    
    // Clear FCM data from memory
    memset(_fcmUrl, 0, sizeof(_fcmUrl));
    memset(_fcmToken, 0, sizeof(_fcmToken));

    if (LittleFS.exists(WIFI_CONFIG_FILE))
    {
        return LittleFS.remove(WIFI_CONFIG_FILE);
    }
    return true;
}

uint8_t PicoFCMNotifierClass::getNetworkCount() { return _networkCount; }
PicoWiFiProvisioningStatus PicoFCMNotifierClass::getStatus() { return _status; }

void PicoFCMNotifierClass::setStatus(PicoWiFiProvisioningStatus newStatus)
{
    if (_status != newStatus)
    {
        _status = newStatus;
        if (_statusCallback)
        {
            _statusCallback(_status);
        }
    }
}

void PicoFCMNotifierClass::setStatusCallback(void (*callback)(PicoWiFiProvisioningStatus status)) { _statusCallback = callback; }
void PicoFCMNotifierClass::setWiFiStatusCallback(void (*callback)(wl_status_t status)) { _wifiStatusCallback = callback; }
void PicoFCMNotifierClass::setBLEConnectionStateCallback(void (*callback)(bool isConnected)) { _bleConnectionStateCallback = callback; }
void PicoFCMNotifierClass::setPasskeyDisplayCallback(void (*callback)(uint32_t passkey)) { BLESecure.setPasskeyDisplayCallback(callback); }
void PicoFCMNotifierClass::setNumericComparisonCallback(void (*callback)(uint32_t passkey, BLEDevice *device)) { BLESecure.setNumericComparisonCallback(callback); }
void PicoFCMNotifierClass::acceptNumericComparison(bool accept) { BLESecure.acceptNumericComparison(accept); }
void PicoFCMNotifierClass::allowProvisioningWhenConnected(bool allow) { _allowProvisioningWhenConnected = allow; }
int32_t PicoFCMNotifierClass::getRSSI() { return WiFi.RSSI(); }

// BTstack global callback Trampolines
void bleDeviceConnected(BLEStatus status, BLEDevice *device) { PicoFCMNotifier.handleDeviceConnected(status, device); }
void bleDeviceDisconnected(BLEDevice *device) { PicoFCMNotifier.handleDeviceDisconnected(device); }
int gattWriteCallback(uint16_t characteristic_id, uint8_t *buffer, uint16_t buffer_size) { return PicoFCMNotifier.handleGattWrite(characteristic_id, buffer, buffer_size); }
uint16_t gattReadCallback(uint16_t characteristic_id, uint8_t *buffer, uint16_t buffer_size) { return PicoFCMNotifier.handleGattRead(characteristic_id, buffer, buffer_size); }

// Class member implementations for BLE events
void PicoFCMNotifierClass::handleDeviceConnected(BLEStatus status, BLEDevice *device)
{
    if (status == BLE_STATUS_OK)
    {
        Serial.println("BLE Device connected");
        _connectedDevice = device;
        if (_bleConnectionStateCallback) _bleConnectionStateCallback(true);
    }
    else
    {
        _connectedDevice = nullptr;
        if (_bleConnectionStateCallback) _bleConnectionStateCallback(false);
    }
}

void PicoFCMNotifierClass::handleDeviceDisconnected(BLEDevice *device)
{
    Serial.println("BLE Device disconnected");
    updatePairingStatusCharacteristic(false);
    _connectedDevice = nullptr;
    BLENotify.handleDisconnection();
    if (_bleConnectionStateCallback)
    {
        _bleConnectionStateCallback(false);
    }
}

int PicoFCMNotifierClass::handleGattWrite(uint16_t characteristic_id, uint8_t *buffer, uint16_t buffer_size)
{
    if (characteristic_id == _ssidCharHandle)
    {
        memset(_receivedSSID, 0, sizeof(_receivedSSID));
        size_t copyLen = min((size_t)buffer_size, (size_t)MAX_SSID_LENGTH);
        memcpy(_receivedSSID, buffer, copyLen);
        Serial.print("Received SSID: ");
        Serial.println(_receivedSSID);
    }
    else if (characteristic_id == _passwordCharHandle)
    {
        memset(_receivedPassword, 0, sizeof(_receivedPassword));
        size_t copyLen = min((size_t)buffer_size, (size_t)MAX_PASSWORD_LENGTH);
        memcpy(_receivedPassword, buffer, copyLen);
        Serial.println("Received password");
    }
    else if (characteristic_id == _commandCharHandle && buffer_size >= 1)
    {
        uint8_t command = buffer[0];
        processCommand(command);
    }
    else if (characteristic_id == _fcmUrlCharHandle)
    {
        memset(_receivedFcmUrl, 0, sizeof(_receivedFcmUrl));
        size_t copyLen = min((size_t)buffer_size, (size_t)MAX_FCM_URL_LENGTH);
        memcpy(_receivedFcmUrl, buffer, copyLen);
        Serial.println("Received FCM URL");
    }
    else if (characteristic_id == _fcmTokenCharHandle)
    {
        memset(_receivedFcmToken, 0, sizeof(_receivedFcmToken));
        size_t copyLen = min((size_t)buffer_size, (size_t)MAX_FCM_TOKEN_LENGTH);
        memcpy(_receivedFcmToken, buffer, copyLen);
        Serial.println("Received FCM Token");
    }

    if (buffer_size == 2)
    {
        uint16_t char_value_handle = characteristic_id - 1;
        uint16_t cccd_value = (buffer[1] << 8) | buffer[0];
        if (char_value_handle == _pairingStatusCharHandle)
        {
            if (cccd_value == 0x0001)
            {
                BLENotify.handleSubscriptionChange(_pairingStatusCharHandle, true);
                bool isPaired = (_connectedDevice && BLESecure.getPairingStatus() == PAIRING_COMPLETE);
                updatePairingStatusCharacteristic(isPaired);
            }
            else if (cccd_value == 0x0000)
            {
                BLENotify.handleSubscriptionChange(_pairingStatusCharHandle, false);
            }
        }
    }
    return 0;
}

uint16_t PicoFCMNotifierClass::handleGattRead(uint16_t characteristic_id, uint8_t *buffer, uint16_t buffer_size)
{
    if (characteristic_id == _pairingStatusCharHandle)
    {
        uint8_t pairingStatusValue = (_connectedDevice && BLESecure.getPairingStatus() == PAIRING_COMPLETE) ? PAIRING_STATUS_PAIRED : PAIRING_STATUS_NOT_PAIRED;
        if (buffer == NULL) return sizeof(pairingStatusValue);
        if (buffer_size < sizeof(pairingStatusValue)) return 0;
        buffer[0] = pairingStatusValue;
        return sizeof(pairingStatusValue);
    }
    return 0;
}

// Load configuration from flash
bool PicoFCMNotifierClass::loadConfigFromFlash()
{
    if (!LittleFS.exists(WIFI_CONFIG_FILE)) return false;
    
    File configFile = LittleFS.open(WIFI_CONFIG_FILE, "r");
    if (!configFile) return false;

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, configFile);
    configFile.close();

    if (error)
    {
        Serial.print("Failed to parse config: ");
        Serial.println(error.c_str());
        return false;
    }

    const char* fcm_url = doc["fcm_url"];
    if (fcm_url) strncpy(_fcmUrl, fcm_url, MAX_FCM_URL_LENGTH);

    const char* fcm_token = doc["fcm_token"];
    if (fcm_token) strncpy(_fcmToken, fcm_token, MAX_FCM_TOKEN_LENGTH);

    JsonArray networksArray = doc["networks"].as<JsonArray>();
    _networkCount = 0;
    for (JsonObject network : networksArray)
    {
        if (_networkCount >= MAX_WIFI_NETWORKS) break;
        const char *ssid = network["ssid"];
        if (ssid)
        {
            strncpy(_networks[_networkCount].ssid, ssid, MAX_SSID_LENGTH);
            const char *password = network["password"];
            if (password) strncpy(_networks[_networkCount].password, password, MAX_PASSWORD_LENGTH);
            else _networks[_networkCount].password[0] = '\0';
            _networks[_networkCount].enabled = network["enabled"] | true;
            _networkCount++;
        }
    }
    Serial.print("Loaded "); Serial.print(_networkCount); Serial.println(" networks from flash.");
    return true;
}

// Save configuration to flash
bool PicoFCMNotifierClass::saveConfigToFlash()
{
    JsonDocument doc;

    // Save received credentials to the JSON document
    if (strlen(_receivedFcmUrl) > 0) doc["fcm_url"] = _receivedFcmUrl;
    if (strlen(_receivedFcmToken) > 0) doc["fcm_token"] = _receivedFcmToken;
    
    JsonArray networksArray = doc["networks"].to<JsonArray>();
    for (int i = 0; i < _networkCount; i++)
    {
        JsonObject network = networksArray.add<JsonObject>();
        network["ssid"] = _networks[i].ssid;
        network["password"] = _networks[i].password;
        network["enabled"] = _networks[i].enabled;
    }

    File configFile = LittleFS.open(WIFI_CONFIG_FILE, "w");
    if (!configFile) return false;

    if (serializeJson(doc, configFile) == 0)
    {
        configFile.close();
        return false;
    }
    
    configFile.close();
    Serial.println("Configuration saved to flash.");

    // Update in-memory storage after saving
    if (strlen(_receivedFcmUrl) > 0) strncpy(_fcmUrl, _receivedFcmUrl, MAX_FCM_URL_LENGTH);
    if (strlen(_receivedFcmToken) > 0) strncpy(_fcmToken, _receivedFcmToken, MAX_FCM_TOKEN_LENGTH);
    
    return true;
}

// Setup BLE service and characteristics
void PicoFCMNotifierClass::setupBLEService()
{
    BTstack.addGATTService(&_serviceUUID);
    _ssidCharHandle = BLENotify.addNotifyCharacteristic(&_ssidCharUUID, ATT_PROPERTY_READ | ATT_PROPERTY_WRITE);
    _passwordCharHandle = BLENotify.addNotifyCharacteristic(&_passwordCharUUID, ATT_PROPERTY_WRITE);
    _commandCharHandle = BLENotify.addNotifyCharacteristic(&_commandCharUUID, ATT_PROPERTY_WRITE);
    _pairingStatusCharHandle = BLENotify.addNotifyCharacteristic(&_pairingStatusCharUUID, ATT_PROPERTY_READ | ATT_PROPERTY_NOTIFY);
    _fcmUrlCharHandle = BLENotify.addNotifyCharacteristic(&_fcmUrlCharUUID, ATT_PROPERTY_WRITE);
    _fcmTokenCharHandle = BLENotify.addNotifyCharacteristic(&_fcmTokenCharUUID, ATT_PROPERTY_WRITE);

    updatePairingStatusCharacteristic(false);
    Serial.println("BLE service and characteristics set up");
}

// Process commands received via BLE
void PicoFCMNotifierClass::processCommand(uint8_t command)
{
    Serial.print("Received command: 0x");
    Serial.println(command, HEX);
    switch (command)
    {
    case CMD_SAVE_NETWORK:
        if (strlen(_receivedSSID) > 0)
        {
            saveNetwork(_receivedSSID, _receivedPassword);
            saveConfigToFlash();
        }
        break;
    case CMD_CONNECT:
        if (strlen(_receivedSSID) > 0)
        {
            connectToNetwork(_receivedSSID, _receivedPassword);
        }
        else
        {
            connectToStoredNetworks();
        }
        break;
    case CMD_CLEAR_NETWORKS:
        clearNetworks();
        Serial.println("All config cleared.");
        break;
    case CMD_DISCONNECT:
        WiFi.disconnect();
        setStatus(PROVISION_IDLE);
        Serial.println("WiFi disconnect command processed.");
        break;
    default:
        Serial.println("Unknown command.");
        break;
    }
}

// Send an FCM notification
bool PicoFCMNotifierClass::sendNotification(const char *title, const char *body)
{
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("Error: WiFi not connected.");
        return false;
    }
    if (strlen(_fcmUrl) == 0 || strlen(_fcmToken) == 0)
    {
        Serial.println("Error: FCM URL or Token not configured.");
        return false;
    }

    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure(); // For simplicity, don't validate server cert

    http.begin(client, _fcmUrl);
    http.addHeader("Content-Type", "application/json");

    JsonDocument payload;
    payload["token"] = _fcmToken;
    payload["title"] = title;
    payload["body"] = body;

    String jsonPayload;
    serializeJson(payload, jsonPayload);

    int httpResponseCode = http.POST(jsonPayload);

    if (httpResponseCode > 0)
    {
        String response = http.getString();
        Serial.print("HTTP Response code: ");
        Serial.println(httpResponseCode);
        Serial.println(response);
    }
    else
    {
        Serial.print("Error on sending POST: ");
        Serial.println(http.errorToString(httpResponseCode).c_str());
    }

    http.end();
    return (httpResponseCode == 200);
}