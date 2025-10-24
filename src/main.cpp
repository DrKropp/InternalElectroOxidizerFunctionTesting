/*
***Electrooxidizer Version 2 Firmware Alpha 0.03***
Author: Ramsey Kropp
Last Updated: 2025-10-23

Firmware for ESP32-S3 to control electrode voltage direction and timing
during electrooxidative treatment of groundwater.

Features:
- WiFi provisioning with captive portal (WiFiManager)
- Multi-network support (remembers up to 5 networks)
- Multi-reset detection for WiFi credential reset (power cycle or button)
- Web-based control interface with WebSocket communication
- Continuous ADC sampling for current monitoring
- H-Bridge control for voltage polarity switching
- PWM control for voltage regulation
- mDNS support for easy network discovery
- Persistent settings storage in LittleFS

Hardware:
- ESP32-S3 DevKit (16MB Flash, 8MB PSRAM)
- DRV8706H-Q1 H-Bridge motor driver
- RSP-1000-24 DC power supply (24V)
- Current sense circuit on GPIO 2

Required Platform:
- platform-espressif32.zip v51.03.07 (see platformio.ini)
*/

// ============================================================================
// INCLUDES
// ============================================================================

// AsyncTCP configuration for stability with multiple clients
#define CONFIG_ASYNC_TCP_QUEUE_SIZE 256
#define SSE_MAX_QUEUED_MESSAGES 256
#define WS_MAX_QUEUED_MESSAGES 256
#include <AsyncTCP.h>

// Multi-reset detector settings for WiFi credential reset
#define ESP_MRD_USE_LITTLEFS true
#define MULTIRESETDETECTOR_DEBUG true
#define MRD_TIMEOUT 3        // Resets within this time interval triggers multi-reset
#define MRD_TIMES 3          // Number of resets required
#define MRD_ADDRESS 0        // Storage address in LittleFS
#include <ESP_MultiResetDetector.h>

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPAsyncWebServer.h>
#include "LittleFS.h"
#include <ArduinoJson.h>
#include <string>
#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include <ESPmDNS.h>

// Project-specific headers
#include "config.h"
#include "globals.h"
#include "multi_network.h"
#include "button_handler.h"
#include "portal_css.h"
#include "logging.h"

// ============================================================================
// GLOBAL VARIABLE DEFINITIONS
// ============================================================================

// Global Objects
WiFiManager wifiManager;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
MultiResetDetector* mrd;

// WiFiManager custom parameter
WiFiManagerParameter custom_device_name("deviceName", "Device Name", "", 64);

// ADC Handles
adc_continuous_handle_t adc_handle = NULL;
adc_cali_handle_t adc_cali_handle = NULL;
bool adc_calibrated = false;

// Configuration Variables
char hostname[64] = "OrinTechBox01";
char deviceName[64] = "OrinTechBox01";
char ap_ssid[64];
char chip_id_hex[9];

// Control Parameters (persisted to LittleFS)
String FValue1;          // Target output voltage (Volts)
String FValue2;          // Forward polarity time (ms)
String RValue2;          // Reverse polarity time (ms)
uint16_t ForwardTimeInt; // Forward time in milliseconds
uint16_t ReverseTimeInt; // Reverse time in milliseconds

// Runtime State Variables
bool isRunning = true;
bool outputDirection = false;  // false=reverse, true=forward

// Current and Voltage Measurements
float peakPositiveCurrent = 0.0;
float peakNegativeCurrent = 0.0;
float averagePositiveCurrent = 0.0;
float averageNegativeCurrent = 0.0;
float peakPositiveVoltage = 0.0;
float peakNegativeVoltage = 0.0;
float averagePositiveVoltage = 0.0;
float averageNegativeVoltage = 0.0;
float latestCurrent = 0.0;
float latestRaw = 0;

// ADC Accumulation
float positive_adc_sum = 0;
float negative_adc_sum = 0;
uint32_t positive_adc_count = 0;
uint32_t negative_adc_count = 0;

// Timing Variables
uint32_t reversestartTime = 0;
unsigned long lastNotifyTime = 0;
unsigned long lastReconnectAttempt = 0;
unsigned long currentReconnectInterval = 10000;  // Start at 10 seconds
unsigned long lastLogTime = 0;
unsigned long lastTimeSyncAttempt = 0;
unsigned long currentLogStartTime = 0;

// Time Synchronization
bool timeIsSynced = false;
uint16_t currentDayNumber = 1;
String currentLogFilename = "";

// Peak Reset Management
bool hasResetPeakCurrent = false;
unsigned long peakResetStartTime = 0;

// Server State Management
bool asyncServerStarted = false;

// ADC Buffers (moved to heap to prevent stack overflow - 16KB buffer)
uint8_t* adc_buffer = nullptr;

// ============================================================================
// FUNCTION PROTOTYPES
// ============================================================================

// Setup Functions
void initHardware();
void initADC();
void initWiFi();
void initFS();
void initWebSocket();

// ADC Functions
void setup_adc_calibration();
void setup_adc_continuous();
void process_adc_data();

// Settings Management
void setDefaultSettings();
bool loadSettings();
bool saveSettings();
bool saveDeviceName();
void backupWiFiCredentials();
void restoreWiFiCredentials();

// Data Functions
String getValues();
void resetPeakValues();

// WebSocket Handlers
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len);
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
             AwsEventType type, void *arg, uint8_t *data, size_t len);
void notifyClients(String values);
void notifyClients();

// Control Functions
void updateOutputVoltage();
void handleDirectionSwitching();
void handleMeasurements();

// Utility Functions
String processor(const String &var);
String toStringIp(IPAddress ip);
bool isIp(String str);

// ============================================================================
// SETUP FUNCTION
// ============================================================================

void setup()
{
  Serial.begin(115200);

  // Generate unique identifiers based on chip ID
  uint32_t chipId = ESP.getEfuseMac();
  snprintf(chip_id_hex, sizeof(chip_id_hex), "%08X", (uint32_t)(chipId & 0xFFFFFFFF));
  snprintf(ap_ssid, sizeof(ap_ssid), "OrinTech EEO %s", chip_id_hex);
  snprintf(hostname, sizeof(hostname), "OrinTech-%s", chip_id_hex);
  snprintf(deviceName, sizeof(deviceName), "OrinTech-%s", chip_id_hex);

  Serial.println("\n\n╔════════════════════════════════════════════╗");
  Serial.println("║  OrinTech EEO Device Starting              ║");
  Serial.println("╚════════════════════════════════════════════╝");
  Serial.printf("Chip ID: %s\n", chip_id_hex);
  Serial.printf("AP SSID: %s\n", ap_ssid);
  Serial.printf("Default Hostname: %s\n\n", hostname);

  // Initialize hardware peripherals
  initHardware();
  initADC();

  // Initialize filesystem (required before MRD and settings)
  initFS();

  // Initialize multi-network storage
  initMultiNetworkStorage();
  loadSavedNetworks();

  // Initialize button multi-reset detection
  initButtonHandler();

  // Initialize multi-reset detector
  mrd = new MultiResetDetector(MRD_TIMEOUT, MRD_ADDRESS);
  Serial.println("Multi-Reset Detector: Active");
  Serial.println("Info: Power cycle 3x within 5s OR press button 3x within 5s to reset WiFi\n");

  // Load saved settings
  if (!loadSettings())
  {
    Serial.println("Warning: Failed to load settings, using defaults");
    setDefaultSettings();
  }

  // Initialize WiFi with captive portal support
  initWiFi();

  // Initialize web server and WebSocket (but don't start yet if in config portal mode)
  initWebSocket();

  // Captive portal detection handlers (reduces error messages during WiFi setup)
  server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/");
  });
  server.on("/fwlink", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/");
  });
  server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/");
  });

  server.onNotFound([](AsyncWebServerRequest *request) {
    // Check for common captive portal detection paths
    String path = request->url();
    if (path.startsWith("/generate_204") ||
        path.startsWith("/gen_204") ||
        path.startsWith("/ncsi.txt") ||
        path.startsWith("/connecttest.txt") ||
        path.startsWith("/redirect") ||
        path.startsWith("/success.txt")) {
      request->send(204);  // No content response for captive portal detection
      return;
    }
    // Default: serve main page
    request->send(LittleFS, "/index.html", "text/html");
  });

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", "text/html");
  });

  // Log download endpoint
  server.on("/downloadLogs", HTTP_GET, [](AsyncWebServerRequest *request) {
    const char* archivePath = "/logs_archive.txt";

    // Ensure log directory exists before trying to access it
    if (!ensureLogDirectory()) {
      request->send(500, "text/plain", "Failed to access log directory");
      return;
    }

    if (createLogArchive(archivePath)) {
      request->send(LittleFS, archivePath, "text/plain", true, nullptr);
      // Note: File cleanup happens on next download to ensure current download completes
    } else {
      request->send(500, "text/plain", "Failed to create log archive");
    }
  });

  // Get log info endpoint (for debugging)
  server.on("/logInfo", HTTP_GET, [](AsyncWebServerRequest *request) {
    String info = "Available logs: " + listLogFiles() + "\n";
    info += "Total size: " + String(getLogsTotalSize()) + " bytes";
    request->send(200, "text/plain", info);
  });

  server.serveStatic("/", LittleFS, "/");

  // FIX: Only start AsyncWebServer if WiFi is connected and portal is not active
  // This prevents bind error when WiFiManager's portal is using port 80
  if (WiFi.status() == WL_CONNECTED && !wifiManager.getConfigPortalActive())
  {
    server.begin();
    asyncServerStarted = true;
    Serial.println("AsyncWebServer: Started on port 80");

    // Start mDNS responder after WiFi is connected
    if (MDNS.begin(hostname))
    {
      Serial.printf("mDNS: http://%s.local\n", hostname);
      MDNS.addService("http", "tcp", 80);
    }
    else
    {
      Serial.println("Error: mDNS failed to start");
    }
  }
  else
  {
    Serial.println("AsyncWebServer: Deferred start (portal active or WiFi not connected)");
  }

  // Initialize timing
  reversestartTime = micros();
  peakResetStartTime = millis();
  resetPeakValues();

  Serial.println("\n╔════════════════════════════════════════════╗");
  Serial.println("║  Setup Complete - Device Ready             ║");
  Serial.println("╚════════════════════════════════════════════╝\n");
}

// ============================================================================
// SETUP FUNCTIONS
// ============================================================================

void initHardware()
{
  // Configure PWM for voltage control
  if (!ledcAttach(VoltControl_PWM_Pin, PWMFreq, outputBits))
  {
    Serial.println("Error: PWM initialization failed");
  }

  // Configure GPIO pins
  pinMode(outputEnablePin, OUTPUT);
  pinMode(outputDirectionPin, OUTPUT);
  pinMode(nSleepPin, OUTPUT);
  pinMode(DRVOffPin, OUTPUT);
  pinMode(nFaultPin, INPUT);
  pinMode(testButton, INPUT_PULLUP);

  // Initialize to safe state (outputs disabled)
  digitalWrite(nSleepPin, LOW);        // Put DRV in sleep
  digitalWrite(DRVOffPin, HIGH);       // Disable DRV output
  digitalWrite(outputEnablePin, LOW);  // Disable H-Bridge
  digitalWrite(outputDirectionPin, LOW);

  // Wake up DRV8706
  digitalWrite(nSleepPin, HIGH);
  Serial.println("DRV8706: Waking up");

  digitalWrite(DRVOffPin, LOW);
  Serial.println("DRV8706: Ready (outputs disabled)");

  // Turn off RGB LED
  rgbLedWrite(RGBLedPin, 0, 0, 0);
}

void initADC()
{
  // Allocate ADC buffer on heap (16KB) to prevent stack overflow
  size_t buffer_size = BUFFER_SIZE * sizeof(adc_digi_output_data_t);
  adc_buffer = (uint8_t*)malloc(buffer_size);

  if (adc_buffer == nullptr)
  {
    Serial.println("ERROR: Failed to allocate ADC buffer on heap!");
    Serial.printf("Attempted to allocate %d bytes\n", buffer_size);
    // System cannot function without ADC buffer
    while (1) { delay(1000); }
  }

  Serial.printf("ADC: Buffer allocated on heap (%d bytes)\n", buffer_size);

  setup_adc_calibration();
  setup_adc_continuous();
}

void initFS()
{
  if (!LittleFS.begin())
  {
    Serial.println("Error: LittleFS mount failed");
  }
  else
  {
    Serial.println("LittleFS: Mounted successfully");
  }
}

void initWebSocket()
{
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

// WiFiManager save config callback
void saveConfigCallback()
{
  Serial.println("WiFiManager: Configuration saved");

  // Get device name from parameter
  const char* newDeviceName = custom_device_name.getValue();
  if (strlen(newDeviceName) > 0)
  {
    strncpy(deviceName, newDeviceName, sizeof(deviceName));

    // Create hostname-safe version
    String hostnameStr = String(deviceName);
    hostnameStr.replace(" ", "-");
    hostnameStr.replace("_", "-");
    hostnameStr.toLowerCase();
    hostnameStr.toCharArray(hostname, sizeof(hostname));

    saveDeviceName();
    Serial.printf("Device name updated: %s\n", deviceName);
  }

  // Save the network credentials to multi-network storage
  String connectedSSID = WiFi.SSID();
  String connectedPass = WiFi.psk();

  if (connectedSSID.length() > 0)
  {
    Serial.printf("Saving network to multi-network storage: %s\n", connectedSSID.c_str());
    addOrUpdateNetwork(connectedSSID.c_str(), connectedPass.c_str());
  }
}

void initWiFi()
{
  // Load saved device name
  if (LittleFS.exists("/devicename.json"))
  {
    File file = LittleFS.open("/devicename.json", "r");
    if (file)
    {
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, file);
      file.close();

      if (!error)
      {
        String savedName = doc["deviceName"] | "";
        if (savedName.length() > 0)
        {
          strncpy(deviceName, savedName.c_str(), sizeof(deviceName));
        }
      }
    }
  }

  // Set default device name if empty
  if (strlen(deviceName) == 0)
  {
    snprintf(deviceName, sizeof(deviceName), "OrinTech-%s", chip_id_hex);
  }

  // Update custom parameter with current device name
  custom_device_name.setValue(deviceName, sizeof(deviceName));

  // Create hostname-safe version
  String hostnameStr = String(deviceName);
  hostnameStr.replace(" ", "-");
  hostnameStr.replace("_", "-");
  hostnameStr.toLowerCase();
  hostnameStr.toCharArray(hostname, sizeof(hostname));

  // Check for multi-reset condition
  bool shouldStartConfigPortal = false;
  if (mrd->detectMultiReset())
  {
    Serial.println("\n*** MULTI-RESET DETECTED ***");
    Serial.println("Clearing WiFi credentials and device name...");

    // Clear WiFiManager settings
    wifiManager.resetSettings();

    // Clear all saved networks
    if (LittleFS.exists("/networks.json"))
    {
      LittleFS.remove("/networks.json");
      Serial.println("All saved networks: Cleared");
    }
    initMultiNetworkStorage();

    // Note: Device name is NOT cleared on multi-reset
    // It will only be updated if user sets a new name in the config portal

    Serial.println("Configuration portal: Starting");
    Serial.printf("Connect to: %s\n", ap_ssid);

    // Blue LED indicates config mode
    rgbLedWrite(RGBLedPin, 0, 0, 255);

    shouldStartConfigPortal = true;
  }
  else
  {
    Serial.println("No multi-reset detected");
  }

  // Configure WiFiManager
  wifiManager.setDebugOutput(true);
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.addParameter(&custom_device_name);

  // FIX 1: Disable blocking mode and use non-blocking where possible
  wifiManager.setConfigPortalBlocking(false);
  wifiManager.setConfigPortalTimeout(180); // 3 minute timeout for config portal

  // FIX 2: Set WiFi mode before WiFiManager attempts connection
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);  // Clear any previous connection attempts
  delay(100);

  // FIX 3: Disable auto-reconnect to prevent race condition with multi-network system
  WiFi.setAutoReconnect(false);

  // Apply custom CSS
  wifiManager.setCustomHeadElement(PORTAL_CSS);

  bool connected = false;

  if (shouldStartConfigPortal)
  {
    // Start config portal after reset
    Serial.println("Starting config portal (non-blocking)...");
    wifiManager.startConfigPortal(ap_ssid);
  }
  else
  {
    // FIX 4: Try to connect using saved credentials WITHOUT starting portal
    // This prevents WiFiManager from trying to connect while multi-network system does
    Serial.println("Attempting connection to saved network...");

    // First try WiFiManager's saved credentials
    WiFi.begin();  // Use saved credentials from NVS

    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000)
    {
      delay(100);
    }

    connected = (WiFi.status() == WL_CONNECTED);

    if (!connected)
    {
      Serial.println("WiFiManager saved credentials failed, will try multi-network system");
      WiFi.disconnect(true);
    }
  }

  // Update hostname
  WiFi.setHostname(hostname);

  // Print connection info if connected
  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\n--- WiFi Connection Info ---");
    Serial.printf("SSID: %s\n", WiFi.SSID().c_str());
    Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("Hostname: %s\n", hostname);
    Serial.printf("Access at: http://%s.local\n", hostname);
    Serial.println("----------------------------\n");

    // Backup current WiFi credentials for future reference
    backupWiFiCredentials();

    // Attempt NTP time synchronization
    syncTimeWithNTP();
  }
  else if (!shouldStartConfigPortal)
  {
    Serial.println("\n*** WiFi Connection Failed ***");
    Serial.println("Will try multi-network system...");

    // Check if we have backed up credentials
    restoreWiFiCredentials();
    Serial.println("******************************\n");
  }
}

// ============================================================================
// ADC FUNCTIONS
// ============================================================================

void setup_adc_calibration()
{
  adc_cali_curve_fitting_config_t cali_config = {
    .unit_id = ADC_UNIT_1,
    .atten = ADC_ATTEN_DB_12,
    .bitwidth = ADC_BITWIDTH_12,
  };

  esp_err_t ret = adc_cali_create_scheme_curve_fitting(&cali_config, &adc_cali_handle);
  if (ret == ESP_OK)
  {
    adc_calibrated = true;
    Serial.println("ADC: Calibration initialized");
  }
  else
  {
    adc_calibrated = false;
    Serial.println("Warning: ADC calibration failed");
  }
}

void setup_adc_continuous()
{
  // Configure continuous mode
  adc_continuous_handle_cfg_t adc_config = {
    .max_store_buf_size = BUFFER_SIZE * 4,
    .conv_frame_size = BUFFER_SIZE,
  };

  esp_err_t ret = adc_continuous_new_handle(&adc_config, &adc_handle);
  if (ret != ESP_OK)
  {
    Serial.printf("Error: ADC handle creation failed - %s\n", esp_err_to_name(ret));
    return;
  }

  // Configure ADC pattern
  adc_digi_pattern_config_t adc_pattern = {
    .atten = ADC_ATTEN_DB_12,
    .channel = ADC_CHANNEL_1,  // GPIO2
    .unit = ADC_UNIT_1,
    .bit_width = ADC_BITWIDTH_12,
  };

  adc_continuous_config_t dig_cfg = {
    .pattern_num = 1,
    .adc_pattern = &adc_pattern,
    .sample_freq_hz = SAMPLE_RATE,
    .conv_mode = ADC_CONV_SINGLE_UNIT_1,
    .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
  };

  ret = adc_continuous_config(adc_handle, &dig_cfg);
  if (ret != ESP_OK)
  {
    Serial.printf("Error: ADC configuration failed - %s\n", esp_err_to_name(ret));
    return;
  }

  // Start continuous conversion
  ret = adc_continuous_start(adc_handle);
  if (ret != ESP_OK)
  {
    Serial.printf("Error: ADC start failed - %s\n", esp_err_to_name(ret));
    return;
  }

  Serial.println("ADC: Continuous mode started (20 kHz)");
}

void process_adc_data()
{
  uint32_t bytes_read = 0;
  esp_err_t ret = adc_continuous_read(adc_handle, adc_buffer, sizeof(adc_buffer), &bytes_read, 0);

  // Capture current direction for this batch
  bool currentDirection = outputDirection;

  if (ret == ESP_OK && bytes_read > 0)
  {
    adc_digi_output_data_t *p = (adc_digi_output_data_t *)adc_buffer;
    uint32_t num_samples = bytes_read / sizeof(adc_digi_output_data_t);

    for (uint32_t i = 0; i < num_samples; i++)
    {
      if (p[i].type2.channel == ADC_CHANNEL_1 && p[i].type2.unit == ADC_UNIT_1)
      {
        uint32_t adc_raw = p[i].type2.data;

        latestRaw = adc_raw;
        latestCurrent = (adc_raw * ADC_SLOPE) + ADC_INTERCEPT;

        // Accumulate by direction for averaging
        if (currentDirection && latestCurrent > 0.0)
        {
          positive_adc_sum += adc_raw;
          positive_adc_count++;
        }
        else if (!currentDirection && latestCurrent < 0.0)
        {
          negative_adc_sum += adc_raw;
          negative_adc_count++;
        }
      }
    }
  }
}

// ============================================================================
// SETTINGS MANAGEMENT
// ============================================================================

void setDefaultSettings()
{
  FValue1 = "14";   // 14V default
  FValue2 = "100";  // 100ms forward
  RValue2 = "100";  // 100ms reverse
  ForwardTimeInt = FValue2.toInt();
  ReverseTimeInt = RValue2.toInt();
}

bool saveSettings()
{
  JsonDocument doc;
  doc["FValue1"] = FValue1;
  doc["FValue2"] = FValue2;
  doc["RValue2"] = RValue2;
  doc["deviceName"] = deviceName;
  doc["hostname"] = hostname;

  File file = LittleFS.open("/settings.json", "w");
  if (!file)
  {
    Serial.println("Error: Failed to create settings file");
    return false;
  }

  bool success = serializeJson(doc, file);
  file.close();
  return success;
}

bool saveDeviceName()
{
  JsonDocument doc;
  doc["deviceName"] = deviceName;
  doc["hostname"] = hostname;

  File file = LittleFS.open("/devicename.json", "w");
  if (!file)
  {
    Serial.println("Error: Failed to create device name file");
    return false;
  }

  bool success = serializeJson(doc, file);
  file.close();

  if (success)
  {
    Serial.println("Device name: Saved");
  }

  return success;
}

void backupWiFiCredentials()
{
  // Only backup if WiFi is currently connected
  if (WiFi.status() != WL_CONNECTED)
  {
    return;
  }

  JsonDocument doc;
  doc["ssid"] = WiFi.SSID();
  doc["timestamp"] = millis();

  File file = LittleFS.open("/wifi_backup.json", "w");
  if (!file)
  {
    Serial.println("Warning: Failed to backup WiFi credentials");
    return;
  }

  if (serializeJson(doc, file))
  {
    Serial.printf("WiFi credentials backed up: %s\n", WiFi.SSID().c_str());
  }

  file.close();
}

void restoreWiFiCredentials()
{
  if (!LittleFS.exists("/wifi_backup.json"))
  {
    Serial.println("No WiFi backup found");
    return;
  }

  File file = LittleFS.open("/wifi_backup.json", "r");
  if (!file)
  {
    Serial.println("Warning: Failed to read WiFi backup");
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, file) == DeserializationError::Ok)
  {
    String backupSSID = doc["ssid"] | "";
    if (backupSSID.length() > 0)
    {
      Serial.printf("Previous network found: %s\n", backupSSID.c_str());
      Serial.println("Note: Old credentials preserved in backup");
    }
  }

  file.close();
}

bool loadSettings()
{
  // Load device name first
  if (LittleFS.exists("/devicename.json"))
  {
    File nameFile = LittleFS.open("/devicename.json", "r");
    if (nameFile)
    {
      JsonDocument nameDoc;
      if (deserializeJson(nameDoc, nameFile) == DeserializationError::Ok)
      {
        String savedDeviceName = nameDoc["deviceName"] | "";
        String savedHostname = nameDoc["hostname"] | "";

        if (savedDeviceName.length() > 0)
        {
          savedDeviceName.toCharArray(deviceName, sizeof(deviceName));
          Serial.printf("Loaded device name: %s\n", deviceName);
        }
        if (savedHostname.length() > 0)
        {
          savedHostname.toCharArray(hostname, sizeof(hostname));
          Serial.printf("Loaded hostname: %s\n", hostname);
        }
      }
      nameFile.close();
    }
  }

  // Load operational settings
  if (!LittleFS.exists("/settings.json"))
  {
    Serial.println("No settings file found, using defaults");
    setDefaultSettings();
    return saveSettings();
  }

  File file = LittleFS.open("/settings.json", "r");
  if (!file)
  {
    Serial.println("Error: Failed to open settings file");
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, file) != DeserializationError::Ok)
  {
    Serial.println("Error: Failed to parse settings file");
    file.close();
    return false;
  }

  // Load values with defaults
  FValue1 = doc["FValue1"] | "14";
  FValue2 = doc["FValue2"] | "100";
  RValue2 = doc["RValue2"] | "100";

  // Backward compatibility: load device name from settings if not already loaded
  String savedDeviceName = doc["deviceName"] | "";
  String savedHostname = doc["hostname"] | "";

  if (savedDeviceName.length() > 0 && strlen(deviceName) == 0)
  {
    savedDeviceName.toCharArray(deviceName, sizeof(deviceName));
  }
  if (savedHostname.length() > 0 && strlen(hostname) == 0)
  {
    savedHostname.toCharArray(hostname, sizeof(hostname));
  }

  ForwardTimeInt = FValue2.toInt();
  ReverseTimeInt = RValue2.toInt();

  file.close();
  return true;
}

// ============================================================================
// DATA FUNCTIONS
// ============================================================================

String getValues()
{
  JsonDocument controlValues;
  // Use direct values instead of String() conversions to reduce heap fragmentation
  controlValues["isRunning"] = isRunning;
  controlValues["FValue1"] = FValue1.c_str();  // Direct C string (no temp String object)
  controlValues["FValue2"] = FValue2.c_str();
  controlValues["RValue2"] = RValue2.c_str();
  controlValues["peakPositiveCurrent"] = peakPositiveCurrent;  // Direct float (ArduinoJson handles conversion)
  controlValues["peakNegativeCurrent"] = peakNegativeCurrent;
  controlValues["averagePositiveCurrent"] = averagePositiveCurrent;
  controlValues["averageNegativeCurrent"] = averageNegativeCurrent;
  controlValues["peakPositiveVoltage"] = peakPositiveVoltage;
  controlValues["peakNegativeVoltage"] = peakNegativeVoltage;
  controlValues["averagePositiveVoltage"] = averagePositiveVoltage;
  controlValues["averageNegativeVoltage"] = averageNegativeVoltage;

  String output;
  controlValues.shrinkToFit();
  serializeJson(controlValues, output);
  return output;
}

void resetPeakValues()
{
  peakPositiveCurrent = 0.0;
  peakNegativeCurrent = 0.0;
  averagePositiveCurrent = 0.0;
  averageNegativeCurrent = 0.0;

  positive_adc_sum = 0;
  positive_adc_count = 0;
  negative_adc_sum = 0;
  negative_adc_count = 0;

  peakPositiveVoltage = FValue1.toFloat();
  peakNegativeVoltage = FValue1.toFloat();
  averagePositiveVoltage = FValue1.toFloat();
  averageNegativeVoltage = FValue1.toFloat();
}

// ============================================================================
// WEBSOCKET HANDLERS
// ============================================================================

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len)
{
  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)
  {
    // Create String directly from buffer with length (avoid buffer overflow)
    String message = String((char *)data, len);

    if (message.indexOf("toggle") >= 0)
    {
      Serial.println("User toggled run state");
      isRunning = !isRunning;
      notifyClients(getValues());
    }
    else if (message.indexOf("1F") >= 0)
    {
      float newVoltage = message.substring(2).toFloat();
      // Validate voltage range (0-24V for safety)
      if (newVoltage >= 0.0 && newVoltage <= 24.0)
      {
        FValue1 = String(newVoltage);
        Serial.printf("Voltage updated: %sV\n", FValue1.c_str());
        resetPeakValues();
        saveSettings();
        notifyClients(getValues());
      }
      else
      {
        Serial.printf("ERROR: Voltage out of range (0-24V): %.2f\n", newVoltage);
      }
    }
    else if (message.indexOf("2F") >= 0)
    {
      int newForwardTime = message.substring(2).toInt();
      // Validate forward time (10ms - 60000ms = 1 minute)
      if (newForwardTime >= 10 && newForwardTime <= 60000)
      {
        FValue2 = String(newForwardTime);
        ForwardTimeInt = newForwardTime;
        Serial.printf("Forward time updated: %dms\n", ForwardTimeInt);
        resetPeakValues();
        saveSettings();
        notifyClients(getValues());
      }
      else
      {
        Serial.printf("ERROR: Forward time out of range (10-60000ms): %d\n", newForwardTime);
      }
    }
    else if (message.indexOf("2R") >= 0)
    {
      int newReverseTime = message.substring(2).toInt();
      // Validate reverse time (10ms - 60000ms = 1 minute)
      if (newReverseTime >= 10 && newReverseTime <= 60000)
      {
        RValue2 = String(newReverseTime);
        ReverseTimeInt = newReverseTime;
        Serial.printf("Reverse time updated: %dms\n", ReverseTimeInt);
        resetPeakValues();
        saveSettings();
        notifyClients(getValues());
      }
      else
      {
        Serial.printf("ERROR: Reverse time out of range (10-60000ms): %d\n", newReverseTime);
      }
    }
    else if (message.indexOf("resetPeakCurrent") >= 0)
    {
      Serial.println("User reset peak values");
      resetPeakValues();
      notifyClients(getValues());
    }
    else if (strcmp((char *)data, "getValues") == 0)
    {
      notifyClients(getValues());
    }
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
             AwsEventType type, void *arg, uint8_t *data, size_t len)
{
  switch (type)
  {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket: Client #%u connected from %s\n",
                    client->id(), client->remoteIP().toString().c_str());
      // Send initial values to newly connected client
      client->text(getValues());
      break;

    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket: Client #%u disconnected\n", client->id());
      break;

    case WS_EVT_DATA:
      handleWebSocketMessage(arg, data, len);
      break;

    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}

void notifyClients(String values)
{
  // Check if any clients are connected before sending
  if (ws.count() == 0) return;

  // Send to each client individually with queue check
  for (size_t i = 0; i < ws.count(); i++)
  {
    AsyncWebSocketClient *client = ws.client(i);
    if (client && client->status() == WS_CONNECTED)
    {
      // Check if client queue has room (prevent overflow)
      if (client->queueIsFull())
      {
        Serial.printf("Client #%u queue full, skipping update\n", client->id());
        continue;
      }
      client->text(values);
    }
  }
}

void notifyClients()
{
  // Check if any clients are connected before sending
  if (ws.count() == 0) return;

  String message = String(isRunning);

  // Send to each client individually with queue check
  for (size_t i = 0; i < ws.count(); i++)
  {
    AsyncWebSocketClient *client = ws.client(i);
    if (client && client->status() == WS_CONNECTED)
    {
      // Check if client queue has room (prevent overflow)
      if (client->queueIsFull())
      {
        Serial.printf("Client #%u queue full, skipping update\n", client->id());
        continue;
      }
      client->text(message);
    }
  }
}

// ============================================================================
// CONTROL FUNCTIONS
// ============================================================================

void updateOutputVoltage()
{
  uint32_t pwmValue = round(FValue1.toFloat() / TargetVoltsConversionFactor);
  ledcWrite(VoltControl_PWM_Pin, pwmValue);
}

void handleDirectionSwitching()
{
  uint32_t currentTime = micros();

  if (outputDirection)
  {
    // Currently in forward direction
    if (currentTime - reversestartTime >= ForwardTimeInt * 1000)
    {
      reversestartTime = currentTime;
      outputDirection = false;
      digitalWrite(outputDirectionPin, LOW);
    }
  }
  else
  {
    // Currently in reverse direction
    if (currentTime - reversestartTime >= ReverseTimeInt * 1000)
    {
      reversestartTime = currentTime;
      outputDirection = true;
      digitalWrite(outputDirectionPin, HIGH);
    }
  }
}

void handleMeasurements()
{
  // Calculate averages when enough samples collected
  if (positive_adc_count >= MAX_SAMPLES)
  {
    averagePositiveCurrent = ((positive_adc_sum / positive_adc_count) * ADC_SLOPE) + ADC_INTERCEPT;
    positive_adc_sum = 0;
    positive_adc_count = 0;
  }

  if (negative_adc_count >= MAX_SAMPLES)
  {
    averageNegativeCurrent = ((negative_adc_sum / negative_adc_count) * ADC_SLOPE) + ADC_INTERCEPT;

    // Apply saturation fix (clamp if significantly higher than positive)
    if (fabs(averageNegativeCurrent) >= 1.1 * fabs(averagePositiveCurrent))
    {
      averageNegativeCurrent = -averagePositiveCurrent;
    }

    negative_adc_sum = 0;
    negative_adc_count = 0;
  }

  // Update peak values
  if (outputDirection)
  {
    if (latestCurrent > peakPositiveCurrent)
    {
      peakPositiveCurrent = latestCurrent;
    }
  }
  else
  {
    if (latestCurrent < peakNegativeCurrent)
    {
      peakNegativeCurrent = latestCurrent;

      // Apply saturation fix to peak as well
      if (fabs(peakNegativeCurrent) >= 1.1 * fabs(peakPositiveCurrent))
      {
        peakNegativeCurrent = -peakPositiveCurrent;
      }
    }
  }
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

String processor(const String &var)
{
  if (var == "STATE")
  {
    return isRunning ? "ON" : "OFF";
  }
  return String();
}

bool isIp(String str)
{
  for (size_t i = 0; i < str.length(); i++)
  {
    int c = str.charAt(i);
    if (c != '.' && (c < '0' || c > '9'))
    {
      return false;
    }
  }
  return true;
}

String toStringIp(IPAddress ip)
{
  return String(ip[0]) + "." + ip[1] + "." + ip[2] + "." + ip[3];
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop()
{
  // Process WiFiManager (handles non-blocking config portal)
  wifiManager.process();

  // Update multi-reset detector (must be called every loop)
  mrd->loop();

  // Check for button-based multi-reset (3 presses within 5 seconds)
  checkButtonMultiReset();

  // FIX: Start AsyncWebServer when WiFi connects and portal closes
  if (!asyncServerStarted && WiFi.status() == WL_CONNECTED && !wifiManager.getConfigPortalActive())
  {
    server.begin();
    asyncServerStarted = true;
    Serial.println("AsyncWebServer: Started on port 80");

    // Update WiFi hostname
    WiFi.setHostname(hostname);

    // Start or restart mDNS
    MDNS.end();
    if (MDNS.begin(hostname))
    {
      Serial.printf("mDNS: http://%s.local\n", hostname);
      MDNS.addService("http", "tcp", 80);
    }

    // Reset reconnect timer to prevent immediate reconnect attempts
    lastReconnectAttempt = millis();
    currentReconnectInterval = reconnectInterval;
  }

  // Handle WiFi reconnection with multi-network support and exponential backoff
  // Wait at least 30 seconds after boot before attempting reconnection (allow initial connection to stabilize)
  if (WiFi.status() != WL_CONNECTED && millis() > 30000 && !wifiManager.getConfigPortalActive())
  {
    unsigned long currentMillis = millis();
    if (currentMillis - lastReconnectAttempt >= currentReconnectInterval)
    {
      Serial.printf("WiFi disconnected, attempting reconnect (interval: %lums)...\n", currentReconnectInterval);

      // Try to reconnect to saved networks first
      if (connectToSavedNetworks())
      {
        Serial.println("Reconnected to saved network");
        lastReconnectAttempt = currentMillis;
        currentReconnectInterval = reconnectInterval;  // Reset backoff on success
      }
      else
      {
        Serial.println("All saved networks failed, starting config portal...");
        // FIX 5: Start WiFiManager config portal in non-blocking mode
        // This allows multi-network system to keep trying while portal is available
        if (!wifiManager.getConfigPortalActive())
        {
          wifiManager.startConfigPortal(ap_ssid);
          Serial.printf("Config portal started at: %s\n", ap_ssid);
        }

        // Exponential backoff: double the interval, cap at max
        unsigned long newInterval = currentReconnectInterval * 2;
        currentReconnectInterval = (newInterval < maxReconnectInterval) ? newInterval : maxReconnectInterval;

        lastReconnectAttempt = currentMillis;
      }
    }
  }
  else
  {
    // WiFi connected - ensure backoff is reset
    if (currentReconnectInterval != reconnectInterval)
    {
      currentReconnectInterval = reconnectInterval;
    }
  }

  // Initialize voltage peaks if not set
  if (peakPositiveVoltage == 0.0)
  {
    peakPositiveVoltage = FValue1.toFloat();
    peakNegativeVoltage = FValue1.toFloat();
    averagePositiveVoltage = FValue1.toFloat();
    averageNegativeVoltage = FValue1.toFloat();
  }

  // Clean up WebSocket connections
  ws.cleanupClients();

  // Handle device state
  if (!isRunning)
  {
    // Device stopped - disable outputs and turn off LED
    rgbLedWrite(RGBLedPin, 0, 0, 0);
    digitalWrite(outputEnablePin, LOW);
  }
  else
  {
    // Device running - enable outputs and indicate with red LED
    rgbLedWrite(RGBLedPin, 128, 0, 0);
    digitalWrite(outputEnablePin, HIGH);

    // Process ADC data
    process_adc_data();

    // Update output voltage
    updateOutputVoltage();

    // Handle polarity switching
    handleDirectionSwitching();

    // Handle measurements and peak detection
    handleMeasurements();

    // Auto-reset peak values after 60 seconds (millis() rollover safe)
    if (!hasResetPeakCurrent && (millis() - peakResetStartTime) >= 60000)
    {
      hasResetPeakCurrent = true;
      resetPeakValues();
      notifyClients(getValues());
    }

    // Send updates to WebSocket clients
    if (millis() - lastNotifyTime >= notifyInterval)
    {
      lastNotifyTime = millis();
      notifyClients(getValues());
    }

    // Check for log file rollover (midnight or 24h)
    if (shouldRolloverLog())
    {
      handleLogRollover();
    }

    // Log data every 5 minutes
    if (millis() - lastLogTime >= logInterval)
    {
      lastLogTime = millis();

      logData(averagePositiveCurrent, averageNegativeCurrent,
              peakPositiveCurrent, peakNegativeCurrent,
              averagePositiveVoltage, averageNegativeVoltage,
              peakPositiveVoltage, peakNegativeVoltage,
              ForwardTimeInt, ReverseTimeInt);
    }
  }

  // Periodic NTP time synchronization (every 1 hour, only if WiFi connected)
  if (WiFi.status() == WL_CONNECTED)
  {
    if (millis() - lastTimeSyncAttempt >= NTP_SYNC_INTERVAL)
    {
      syncTimeWithNTP();
    }
  }
}
