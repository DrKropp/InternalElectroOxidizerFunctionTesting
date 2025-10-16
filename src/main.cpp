/*
***Electrooxidizer Version 2 Firmware Alpha 0.02***
Author: Ramsey Kropp
Last Updated: 2025

Firmware for ESP32-S3 to control electrode voltage direction and timing 
during electrooxidative treatment of groundwater.

Features:
- WiFi provisioning with captive portal (WiFiManager)
- Multi-reset detection for WiFi credential reset
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
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPAsyncWebServer.h>
#include "LittleFS.h"
#include <ArduinoJson.h>
#include <string>
#include <DNSServer.h>
#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include <ESPmDNS.h>

//AsyncTCP configuration for stability with multiple clients
#define CONFIG_ASYNC_TCP_QUEUE_SIZE 256
#define SSE_MAX_QUEUED_MESSAGES 256
#define WS_MAX_QUEUED_MESSAGES 256
#include <AsyncTCP.h>

// Multi-reset detector for WiFi credential reset
#define ESP_MRD_USE_LITTLEFS true
#define MULTIRESETDETECTOR_DEBUG true
#include <ESP_MultiResetDetector.h>

// ============================================================================
// CONFIGURATION CONSTANTS
// ============================================================================

// Multi-Reset Detector Settings
#define MRD_TIMEOUT 5        // Seconds between resets to trigger multi-reset
#define MRD_TIMES 3          // Number of resets required
#define MRD_ADDRESS 0        // Storage address in LittleFS

// GPIO Pin Definitions - DRV8706H-Q1 H-Bridge
const uint8_t VoltControl_PWM_Pin = 8;   // PWM output to control 24V supply voltage
const uint8_t outputEnablePin = 4;       // H-Bridge enable (In1/EN)
const uint8_t nHiZ1Pin = 5;              // Not used in mode 2
const uint8_t outputDirectionPin = 6;    // H-Bridge direction control (In2/PH)
const uint8_t nHiZ2Pin = 7;              // Not used in mode 2
const uint8_t nSleepPin = 15;            // Sleep mode control (HIGH=wake, LOW=sleep)
const uint8_t DRVOffPin = 16;            // Disable DRV output (HIGH=disable)
const uint8_t nFaultPin = 17;            // Fault indicator (pulled LOW on fault)

// GPIO Pin Definitions - Other
const uint8_t testButton = 1;            // Test button (active LOW)
const uint8_t RGBLedPin = 48;            // Built-in RGB LED
const int ADC_PIN = 2;                   // Current sense ADC input

// PWM Configuration
const uint8_t outputBits = 10;           // 10-bit PWM resolution (0-1023)
const uint16_t PWMFreq = 25000;          // 25 kHz PWM frequency
const float TargetVoltsConversionFactor = 0.0301686059427937; // Calibrated 16-Jan-2025

// ADC Configuration
const int SAMPLE_RATE = 20000;           // 20 kHz sampling rate
const unsigned long WINDOW_US = 40000;   // 40ms sampling window
const int MAX_SAMPLES_NEW = 1000;        // Max samples per window
const int BUFFER_SIZE = MAX_SAMPLES_NEW * 4;
const uint8_t MAX_SAMPLES = 100;         // Samples for averaging

// ADC Calibration Constants (from calibration 7/5/25)
const float ADC_INTERCEPT = -39.3900104981669f;
const float ADC_SLOPE = 0.0192397497221598f;

// Timing Constants
const unsigned long notifyInterval = 100;        // WebSocket update interval (ms)
const unsigned long reconnectInterval = 10000;   // WiFi reconnect interval (ms)

// ============================================================================
// GLOBAL OBJECTS
// ============================================================================
WiFiManager wifiManager;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
MultiResetDetector* mrd;
WiFiManagerParameter custom_device_name("devicename", "Device Name", "", 32);

// ADC Handles
adc_continuous_handle_t adc_handle = NULL;
adc_cali_handle_t adc_cali_handle = NULL;
bool adc_calibrated = false;

// ============================================================================
// GLOBAL VARIABLES - Configuration
// ============================================================================
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

// ============================================================================
// GLOBAL VARIABLES - Runtime State
// ============================================================================
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

// Peak Reset Management
bool hasResetPeakCurrent = false;

// ADC Buffers
uint8_t adc_buffer[BUFFER_SIZE * sizeof(adc_digi_output_data_t)];

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
// SETUP FUNCTIONS
// ============================================================================

void setup()
{
  Serial.begin(115200);
  delay(100);

  // Generate unique identifiers based on chip ID
  uint32_t chipId = ESP.getEfuseMac();
  snprintf(chip_id_hex, sizeof(chip_id_hex), "%08X", (uint32_t)(chipId & 0xFFFFFFFF));
  snprintf(ap_ssid, sizeof(ap_ssid), "OrinTech EEO %s", chip_id_hex);
  snprintf(hostname, sizeof(hostname), "OrinTech-%s", chip_id_hex);
  snprintf(deviceName, sizeof(deviceName), "OrinTech-%s", chip_id_hex);

  Serial.println("\n\n╔════════════════════════════════════════════╗");
  Serial.println("║  OrinTech EEO Device Starting             ║");
  Serial.println("╚════════════════════════════════════════════╝");
  Serial.printf("Chip ID: %s\n", chip_id_hex);
  Serial.printf("AP SSID: %s\n", ap_ssid);
  Serial.printf("Default Hostname: %s\n\n", hostname);

  // Initialize hardware peripherals
  initHardware();
  initADC();

  // Initialize filesystem (required before MRD and settings)
  initFS();

  // Initialize multi-reset detector
  mrd = new MultiResetDetector(MRD_TIMEOUT, MRD_ADDRESS);
  Serial.println("Multi-Reset Detector: Active");
  Serial.println("Info: Power cycle 3x within 5s to reset WiFi\n");

  // Load saved settings
  if (!loadSettings())
  {
    Serial.println("Warning: Failed to load settings, using defaults");
    setDefaultSettings();
  }

  // Initialize WiFi with captive portal support
  initWiFi();

  // Start mDNS responder
  if (MDNS.begin(hostname))
  {
    Serial.printf("mDNS: http://%s.local\n", hostname);
  }
  else
  {
    Serial.println("Error: mDNS failed to start");
  }

  // Initialize web server and WebSocket
  initWebSocket();
  
  server.onNotFound([](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", "text/html");
  });
  
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", "text/html");
  });
  
  server.serveStatic("/", LittleFS, "/");
  server.begin();

  // Initialize timing
  reversestartTime = micros();
  resetPeakValues();

  Serial.println("\n╔════════════════════════════════════════════╗");
  Serial.println("║  Setup Complete - Device Ready            ║");
  Serial.println("╚════════════════════════════════════════════╝\n");
}

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

void initWiFi()
{
  WiFi.setHostname(hostname);

  // Check for multi-reset condition
  if (mrd->detectMultiReset())
  {
    Serial.println("\n*** MULTI-RESET DETECTED ***");
    Serial.println("Clearing WiFi credentials and device name...");

    // Clear stored credentials
    wifiManager.resetSettings();

    // Clear device name
    if (LittleFS.exists("/devicename.json"))
    {
      LittleFS.remove("/devicename.json");
      Serial.println("Device name: Cleared");
    }

    // Reset to default identifiers
    snprintf(hostname, sizeof(hostname), "OrinTech-%s", chip_id_hex);
    snprintf(deviceName, sizeof(deviceName), "OrinTech-%s", chip_id_hex);

    Serial.println("Configuration portal: Starting");
    Serial.printf("Connect to: %s\n", ap_ssid);

    // Blue LED indicates config mode
    rgbLedWrite(RGBLedPin, 0, 0, 255);
  }
  else
  {
    Serial.println("No multi-reset detected");
  }

  // Configure WiFiManager
  wifiManager.setConfigPortalTimeout(180);  // 3 minute timeout
  wifiManager.setConnectTimeout(20);        // 20 second connection timeout
  wifiManager.addParameter(&custom_device_name);

  // Start WiFi connection or config portal
  if (!wifiManager.autoConnect(ap_ssid))
  {
    Serial.println("Error: WiFi connection failed or timed out");
  }
  else
  {
    Serial.println("WiFi: Connected successfully");
  }

  // Process custom device name from captive portal
  String deviceNameInput = custom_device_name.getValue();
  if (deviceNameInput.length() > 0)
  {
    deviceNameInput.toCharArray(deviceName, sizeof(deviceName));

    // Create hostname-safe version
    String hostnameStr = deviceNameInput;
    hostnameStr.replace(" ", "-");
    hostnameStr.replace("_", "-");
    hostnameStr.toLowerCase();
    hostnameStr.toCharArray(hostname, sizeof(hostname));

    WiFi.setHostname(hostname);
    Serial.printf("Device Name: %s\n", deviceName);
    Serial.printf("Hostname: %s\n", hostname);

    saveDeviceName();
  }

  // Print connection info
  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\n--- WiFi Connection Info ---");
    Serial.printf("SSID: %s\n", WiFi.SSID().c_str());
    Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("Hostname: %s\n", hostname);
    Serial.printf("Access at: http://%s.local\n", hostname);
    Serial.println("----------------------------\n");
  }
  else
  {
    Serial.println("\n*** WiFi Connection Failed ***");
    Serial.println("Possible causes:");
    Serial.println("- Incorrect SSID/password");
    Serial.println("- Network not in 2.4GHz mode");
    Serial.println("- Weak signal strength");
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

  controlValues["FValue1"] = String(FValue1);
  controlValues["FValue2"] = String(FValue2);
  controlValues["RValue2"] = String(RValue2);
  controlValues["peakPositiveCurrent"] = String(peakPositiveCurrent, 3);
  controlValues["peakNegativeCurrent"] = String(peakNegativeCurrent, 3);
  controlValues["averagePositiveCurrent"] = String(averagePositiveCurrent, 3);
  controlValues["averageNegativeCurrent"] = String(averageNegativeCurrent, 3);
  controlValues["peakPositiveVoltage"] = String(peakPositiveVoltage);
  controlValues["peakNegativeVoltage"] = String(peakNegativeVoltage);
  controlValues["averagePositiveVoltage"] = String(averagePositiveVoltage);
  controlValues["averageNegativeVoltage"] = String(averageNegativeVoltage);

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
    data[len] = 0;
    String message = (char *)data;

    if (message.indexOf("toggle") >= 0)
    {
      Serial.println("User toggled run state");
      isRunning = !isRunning;
      notifyClients(getValues());
    }
    else if (message.indexOf("1F") >= 0)
    {
      FValue1 = message.substring(2);
      Serial.printf("Voltage updated: %sV\n", FValue1.c_str());
      resetPeakValues();
      saveSettings();
      notifyClients(getValues());
    }
    else if (message.indexOf("2F") >= 0)
    {
      FValue2 = message.substring(2);
      ForwardTimeInt = FValue2.toInt();
      Serial.printf("Forward time updated: %dms\n", ForwardTimeInt);
      resetPeakValues();
      saveSettings();
      notifyClients(getValues());
    }
    else if (message.indexOf("2R") >= 0)
    {
      RValue2 = message.substring(2);
      ReverseTimeInt = RValue2.toInt();
      Serial.printf("Reverse time updated: %dms\n", ReverseTimeInt);
      resetPeakValues();
      saveSettings();
      notifyClients(getValues());
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
  ws.textAll(values);
}

void notifyClients()
{
  ws.textAll(String(isRunning));
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
  // Update multi-reset detector (must be called every loop)
  mrd->loop();

  // Handle WiFi reconnection
  if (WiFi.status() != WL_CONNECTED)
  {
    unsigned long currentMillis = millis();
    if (currentMillis - lastReconnectAttempt >= reconnectInterval)
    {
      Serial.println("WiFi disconnected, attempting reconnect...");
      WiFi.disconnect();
      wifiManager.autoConnect(ap_ssid);
      lastReconnectAttempt = currentMillis;
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

    // Auto-reset peak values after 60 seconds
    if (millis() >= 60000 && !hasResetPeakCurrent)
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
  }
}