/*
***Electrooxidizer Version 2 Firmware Alpha 0.01***
Author: Ramsey Kropp
20240214

Firmware for an ESP32-S3 (ESP32-WROOM-1 N16R8 with WiFi, BT, Dual Core, 240MHz, VRef and ADC calibration in efuse) to control the electrode voltage direction and timing applied during electrooxidative treatment of groundwater

NOTE: Requires "platform = https://github.com/pioarduino/platform-espressif32/releases/download/51.03.07/platform-espressif32.zip" in the platformio.ini file to utilize the latest version of the ESP-Arduino framework

NOTE: Search for "TK" to find important notes and tasks to complete.

Uses a filtered PWM to control the output voltage of an RSP1000-24 DC power supply
Uses a full H-Bridgeto control the direction of the output voltage, switching directions between forward and reverse based on user defined timing.

Software To Do (TK):
  1: Implement a local web page and/or MQTT to
    a: Allow a user to adjust Forward and Reverse voltage
    b: Allow a user to adjust Forward and Reverse treatment times
    c: Show/log the current setpoints and operational values (Output voltage and current)
    d: Allow user enrollment and access control
  2: Implement output current measurement (ADC read of GPIO 2, still needs calibration after changing current sense resistor! TK)
    a: Average output over long term operation
    b: Peak output current during first few milliseconds after changing voltage direction
  6: Implement PID control to automatically adjust PWM duty cycle to match output voltage set-points
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "LittleFS.h"
#include <ArduinoJson.h>
#include <string>
#include <DNSServer.h>
#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include <ESPmDNS.h>

// TK get rid of hard coded security information before release!
// WiFiManager now handles AP and captive portal automatically
// Remove old AP Config code and use WiFiManager instead

char hostname[64] = "OrinTechBox01";
char deviceName[64] = "OrinTechBox01";
char ap_ssid[64];
char chip_id_hex[9];

WiFiManager wifiManager;
WiFiManagerParameter custom_device_name("devicename", "Device Name", "", 32);

// Wifi reconnection helper variables
unsigned long previousMillis = 0;
unsigned long interval = 30000;

// Create AsyncWebServer object on port 80
AsyncWebServer server(80); // TK Change to port 443 for secure network
// Create a WebSocket object

AsyncWebSocket ws("/ws");

String message = "";
String runState = "FALSE";

String FValue1;          // OUTPUT VOLTAGE
String FValue2;          // FORWARD TIME
uint16_t ForwardTimeInt; // FORWARD TIME in mS
String RValue2;          // REVERSE TIME
uint16_t ReverseTimeInt; // REVERSE TIME in mS

String targetVolts = "0.0"; // targetVolts holds target voltage 10.0<TargetVolts<26.0 0.1V resolution
// String RValue2 = "0"; // reverseTime sets the reversal time in mS

// Duty cycles
int dutyCycle1F;
int dutyCycle1R;
int dutyCycle2F;
int dutyCycle2R;
int dutyCycle3F;
int dutyCycle3R;

// Output variables
float outputCurrent = 0.0; // Amps
float outputVoltage = 0.0; // Volts

// Current and Voltage readings
float peakPositiveCurrent = 0.0;
float peakNegativeCurrent = 0.0;
float averagePositiveCurrent = 0.0;
float averageNegativeCurrent = 0.0;
float peakPositiveVoltage = 0.0;
float peakNegativeVoltage = 0.0;
float averagePositiveVoltage = 0.0;
float averageNegativeVoltage = 0.0;

// Get Values
String getValues()
{
  JsonDocument controlValues;

  controlValues["FValue1"] = String(FValue1);
  controlValues["FValue2"] = String(FValue2);
  controlValues["RValue2"] = String(RValue2);
  controlValues["peakPositiveCurrent"] = String(peakPositiveCurrent, 3);
  controlValues["peakNegativeCurrent"] = String(peakNegativeCurrent, 3);
  controlValues["averagePositiveCurrent"] = String(averagePositiveCurrent, 3); // Use display variable
  controlValues["averageNegativeCurrent"] = String(averageNegativeCurrent, 3); // Use display variable
  controlValues["peakPositiveVoltage"] = String(peakPositiveVoltage);
  controlValues["peakNegativeVoltage"] = String(peakNegativeVoltage);
  controlValues["averagePositiveVoltage"] = String(averagePositiveVoltage);
  controlValues["averageNegativeVoltage"] = String(averageNegativeVoltage);

  String output;

  controlValues.shrinkToFit(); // optional
  serializeJson(controlValues, output);
  return output;
}

// helper variables for averaging
const uint8_t MAX_SAMPLES = 100;
float forwardSum = 0.0; // Sum of current readings for averaging
float reverseSum = 0.0;
uint16_t forwardIndex = 0; // Count of current readings for averaging
uint16_t reverseIndex = 0; // Count of current readings for averaging

float forwardCurrentReadings[MAX_SAMPLES];
float reverseCurrentReadings[MAX_SAMPLES];

float alpha = 0.05; // smoothing factor for exponential weighted average
float previousNegativeValue = 0.0;
float previousPositiveValue = 0.0;
bool isFirstPositiveSample = true;
bool isFirstNegativeSample = true;
bool hasResetPeakCurrent = false;

// Define some GPIO connections between ESP32-S3 and DRV8706H-Q1
const uint8_t VoltControl_PWM_Pin = 8; // GPIO 8 PWM Output will adjust 24V power supply output, PWM Setting=TargetVolts/TargetVoltsConversionFactor
const uint8_t outputEnablePin = 4;     // In1/EN: Turn on output mosfets in H-Bridge, direction set by PH
const uint8_t nHiZ1Pin = 5;            // Physically connected but unused in mode 2
const uint8_t outputDirectionPin = 6;  // In2/PH: Controls H-Bridge output direction, Low is Reverse, High is Forward
const uint8_t nHiZ2Pin = 7;            // Physically connected but unused in mode 2
const uint8_t nSleepPin = 15;          // Can put DRV8706H-Q1 into sleep mode, High to wake, Low to Sleep
const uint8_t DRVOffPin = 16;          // Disable DRV8706H-Q1 drive output without affecting other subsystems, High disables output
const uint8_t nFaultPin = 17;          // Fault indicator output pulled low to indicate fault condition

// New ADC continuous mode variables
adc_continuous_handle_t adc_handle = NULL;
adc_cali_handle_t adc_cali_handle = NULL;
bool adc_calibrated = false;
const int ADC_PIN = 2;                       // GPIO pin 2
const int SAMPLE_RATE = 20000;               // 20 kHz sampling rate
const unsigned long WINDOW_US = 40000;       // 40ms = 40,000 microseconds
const int MAX_SAMPLES_NEW = 1000;            // Maximum samples to store per window
const int BUFFER_SIZE = MAX_SAMPLES_NEW * 4; // Larger buffer for continuous mode

// Buffers and variables for ADC
uint8_t adc_buffer[BUFFER_SIZE * sizeof(adc_digi_output_data_t)];
float voltage_samples[MAX_SAMPLES_NEW];
int sample_count = 0;
float latestCurrent = 0.0;
uint32_t adc_sum = 0;
uint32_t adc_count = 0;
float positive_adc_sum = 0;
float negative_adc_sum = 0;
uint32_t positive_adc_count = 0;
uint32_t negative_adc_count = 0;
unsigned long last_adc_reset = 0;
unsigned long last_calculation = 0;
float latestRaw = 0; // Latest raw ADC value

// Define other ESP32-S3 GPIO connections
const uint8_t testButton = 1; // Button pulls GPIO 01 to ground when pressed, currently used for testing
const uint8_t RGBLedPin = 48; // ESP32-S3 built in RGB LED for test/debug, use rgbLedWrite function to control color and brightness
// Set up some global variables:

// DRV8706H-Q1 Control Variables
bool outputEnable;
bool outputDirection;
bool nSleep;
bool DRVOff;
bool nFault;
bool isRunning = true;

// RSP-1000-24 Control Variables
const uint8_t outputBits = 10;  // 10 bit PWM resolution
const uint16_t PWMFreq = 25000; // 25kHz PWM Frequency
uint32_t VoltControl_PWM = 350; // PWM Setting=TargetVolts/TargetVoltsConversionFactor, Values outside range of 300 to 900 (10bit) cause 24V supply fault conditions
float TargetVolts = 18.0;

// Variables used for timing
uint32_t currentTime = 0;       // Store the current time in uS
uint32_t currentTimeMillis = 0; // Store the current time in mS
uint32_t reversestartTime = 0;  // Store the reversal cycle start time
uint32_t reverseTimeUS = 40000; // uS time between reversals
uint32_t samplingstartTime = 0; // Store the sampling start time
uint32_t samplingTime = 1000;   // uS between taking current measurements

// Variables for storing sensor outputs
float averageoutputCurrent = 0.0;   // Converted average current value
float switchingoutputCurrent = 0.0; // output current measured immediately after changing direction
uint16_t SO_ADC;                    // raw, unscaled current output reading

// Some other constants
const float TargetVoltsConversionFactor = 0.0301686059427937; // Slope Value from calibration 16Jan2025

// temp
unsigned long lastNotifyTime = 0;
const unsigned long notifyInterval = 500; // Notify clients every 500ms, TK might be too fast for some clients
// ADC Constants
const float INTERCEPT = -39.3900104981669f; // From calibration 7/5/25
const float SLOPE = 0.0192397497221598f;    // From calibration 7/5/25
// const float INTERCEPT = -7.11166481117379f; // From calibration 9/12/25
// const float SLOPE = 0.00353825655865396f;   // From calibration 9/12/25

// New ADC functions
void setup_adc_calibration()
{
  // Initialize ADC calibration
  adc_cali_curve_fitting_config_t cali_config = {
      .unit_id = ADC_UNIT_1,
      .atten = ADC_ATTEN_DB_12,
      .bitwidth = ADC_BITWIDTH_12,
  };

  esp_err_t ret = adc_cali_create_scheme_curve_fitting(&cali_config, &adc_cali_handle);
  if (ret == ESP_OK)
  {
    adc_calibrated = true;
    Serial.println("ADC calibration initialized successfully");
  }
  else
  {
    Serial.println("ADC calibration failed, using raw values");
    adc_calibrated = false;
  }
}

void setup_adc_continuous()
{
  // Configure ADC continuous mode
  adc_continuous_handle_cfg_t adc_config = {
      .max_store_buf_size = BUFFER_SIZE * 4,
      .conv_frame_size = BUFFER_SIZE,
  };

  esp_err_t ret = adc_continuous_new_handle(&adc_config, &adc_handle);
  if (ret != ESP_OK)
  {
    Serial.printf("Failed to create ADC handle: %s\n", esp_err_to_name(ret));
    return;
  }

  // Configure ADC pattern
  adc_digi_pattern_config_t adc_pattern = {
      .atten = ADC_ATTEN_DB_12,
      .channel = ADC_CHANNEL_1, // GPIO2 is ADC_CHANNEL_1
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
    Serial.printf("Failed to configure ADC: %s\n", esp_err_to_name(ret));
    return;
  }

  // Start continuous conversion
  ret = adc_continuous_start(adc_handle);
  if (ret != ESP_OK)
  {
    Serial.printf("Failed to start ADC: %s\n", esp_err_to_name(ret));
    return;
  }

  Serial.println("ADC continuous mode started successfully");
}

void process_adc_data()
{
  uint32_t bytes_read = 0;
  esp_err_t ret = adc_continuous_read(adc_handle, adc_buffer, sizeof(adc_buffer), &bytes_read, 0);

  // Capture the current direction at the start of processing this batch
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
        latestCurrent = (adc_raw * SLOPE) + INTERCEPT;

        // Accumulate sums separately by direction
        if (currentDirection)
        {
          if (latestCurrent > 0.0)
          {
            positive_adc_sum += adc_raw;
            positive_adc_count++;
          }
        }
        else
        {
          if (latestCurrent < 0.0)
          {
            negative_adc_sum += adc_raw;
            negative_adc_count++;
          }
        }
      }
    }
  }
}

void initWiFi()
{
  WiFi.setHostname(hostname);
  
  // Configure WiFiManager
  wifiManager.setConfigPortalTimeout(180); // 3 minute timeout for config portal
  wifiManager.setConnectTimeout(20);       // 20 second timeout for connection attempts
  
  // Add custom parameter for device name
  wifiManager.addParameter(&custom_device_name);
  
  // Enable WiFi portal with custom SSID
  if (!wifiManager.autoConnect(ap_ssid))
  {
    Serial.println("Failed to connect and/or hit timeout");
    // Device will restart automatically after timeout
  }
  else
  {
    Serial.println("WiFi connected successfully");
  }

  // Retrieve custom device name from the captive portal
  String deviceNameInput = custom_device_name.getValue();
  if (deviceNameInput.length() > 0)
  {
    deviceNameInput.toCharArray(deviceName, sizeof(deviceName));
    snprintf(hostname, sizeof(hostname), "%s", deviceName);
    WiFi.setHostname(hostname);
    Serial.print("Device Name set to: ");
    Serial.println(deviceName);
  }

  // Print connection info
  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\nConnected!");
    Serial.print("SSID: ");
    Serial.println(WiFi.SSID());
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("Hostname: ");
    Serial.println(hostname);
  }
  else
  {
    Serial.println("\nFailed to connect!");
    Serial.println("Possible causes:");
    Serial.println("- Wrong SSID/password");
    Serial.println("- Hotspot not in 2.4GHz mode");
    Serial.println("- Weak signal");
  }
}

// Initialize LittleFS
void initFS()
{
  if (!LittleFS.begin())
  {
    Serial.println("An error has occurred while mounting LittleFS");
  }
  else
  {
    Serial.println("LittleFS mounted successfully");
  }
}

// HELPER FUNCTIONS

bool isIp(String str)
{ // Check if string is an IP address
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

// Convert IPAddress to String
String toStringIp(IPAddress ip)
{
  return String(ip[0]) + "." + ip[1] + "." + ip[2] + "." + ip[3];
}

void notifyClients(String values)
{
  ws.textAll(values);
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

  previousPositiveValue = 0.0;
  previousNegativeValue = 0.0;
  isFirstPositiveSample = true;
  isFirstNegativeSample = true;

  forwardIndex = 0;
  reverseIndex = 0;
}

void setDefaultSettings()
{
  FValue1 = "14";
  FValue2 = "100";
  RValue2 = "100";
  ForwardTimeInt = FValue2.toInt();
  ReverseTimeInt = RValue2.toInt();
}

bool saveSettings()
{
  JsonDocument doc;
  doc["FValue1"] = FValue1;
  doc["FValue2"] = FValue2;
  doc["RValue2"] = RValue2;

  File file = LittleFS.open("/settings.json", "w");
  if (!file)
  {
    Serial.println("Failed to create settings file");
    return false;
  }

  if (serializeJson(doc, file))
  {
    file.close();
    return true;
  }

  file.close();
  return false;
}

bool loadSettings()
{
  if (!LittleFS.exists("/settings.json"))
  {
    Serial.println("No settings file found. Creating with defaults.");
    setDefaultSettings();
    return saveSettings();
  }

  File file = LittleFS.open("/settings.json", "r");
  if (!file)
  {
    Serial.println("Failed to open settings file");
    return false;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  if (error)
  {
    Serial.println("Failed to parse settings file");
    file.close();
    return false;
  }

  // load values or use defaults if missing
  FValue1 = doc["FValue1"] | "14";
  FValue2 = doc["FValue2"] | "100";
  RValue2 = doc["RValue2"] | "100";

  ForwardTimeInt = FValue2.toInt();
  ReverseTimeInt = RValue2.toInt();

  file.close();
  return true;
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len)
{
  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)
  {
    data[len] = 0;
    message = (char *)data;
    // Serial.println(message);
    if (message.indexOf("toggle") >= 0)
    {
      Serial.println("Toggled state");
      isRunning = !isRunning;
      notifyClients(getValues());
    }
    if (message.indexOf("1F") >= 0)
    {
      FValue1 = message.substring(2);
      dutyCycle1F = map(FValue1.toInt(), 0, 100, 0, 255);
      // Serial.println(dutyCycle1F);
      Serial.println(getValues());
      notifyClients(getValues());
      resetPeakValues();
      saveSettings();
    }
    if (message.indexOf("2F") >= 0)
    {
      FValue2 = message.substring(2);
      ForwardTimeInt = FValue2.toInt();
      dutyCycle2F = map(ForwardTimeInt, 0, 100, 0, 255);
      // Serial.println(dutyCycle2F);
      Serial.println(getValues());
      notifyClients(getValues());
      resetPeakValues();
      saveSettings();
    }
    if (message.indexOf("2R") >= 0)
    {
      RValue2 = message.substring(2);
      ReverseTimeInt = RValue2.toInt();
      dutyCycle2R = map(ReverseTimeInt, 0, 100, 0, 255);
      // Serial.println(dutyCycle2R);
      Serial.println(getValues());
      notifyClients(getValues());
      resetPeakValues();
      saveSettings();
    }
    if (message.indexOf("resetPeakCurrent") >= 0)
    {
      Serial.println("Resetting peak current values");
      resetPeakValues();
      notifyClients(getValues());
    }
    if (strcmp((char *)data, "getValues") == 0)
    {
      notifyClients(getValues());
    }
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
  switch (type)
  {
  case WS_EVT_CONNECT:
    Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
    break;
  case WS_EVT_DISCONNECT:
    Serial.printf("WebSocket client #%u disconnected\n", client->id());
    break;
  case WS_EVT_DATA:
    handleWebSocketMessage(arg, data, len);
    break;
  case WS_EVT_PONG:
  case WS_EVT_ERROR:
    break;
  }
}

void initWebSocket()
{
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

void notifyClients()
{
  ws.textAll(String(isRunning));
}

String processor(const String &var)
{
  Serial.println(var);
  if (var == "STATE")
  {
    if (isRunning)
    {
      return "ON";
    }
    else
    {
      return "OFF";
    }
  }
  return String();
}

void setup()
{
  Serial.begin(460800);
  delay(100);

  // Get the chip ID and create unique AP SSID and hostname
  uint32_t chipId = ESP.getEfuseMac();
  snprintf(chip_id_hex, sizeof(chip_id_hex), "%08X", (uint32_t)(chipId & 0xFFFFFFFF));
  snprintf(ap_ssid, sizeof(ap_ssid), "OrinTech EEO %s", chip_id_hex);
  snprintf(hostname, sizeof(hostname), "OrinTech-%s", chip_id_hex);
  snprintf(deviceName, sizeof(deviceName), "OrinTech-%s", chip_id_hex);

  Serial.print("Chip ID (Hex): ");
  Serial.println(chip_id_hex);
  Serial.print("AP SSID: ");
  Serial.println(ap_ssid);
  Serial.print("Default Hostname: ");
  Serial.println(hostname);

  bool testAttach = ledcAttach(VoltControl_PWM_Pin, PWMFreq, outputBits);
  if (!testAttach)
    Serial.println("Error in RSP1000-24 Control");

  pinMode(outputEnablePin, OUTPUT);
  pinMode(outputDirectionPin, OUTPUT);
  pinMode(nSleepPin, OUTPUT);
  pinMode(DRVOffPin, OUTPUT);
  pinMode(nFaultPin, INPUT);
  pinMode(testButton, INPUT_PULLUP);

  // Initialize new ADC continuous mode
  setup_adc_calibration();
  setup_adc_continuous();

  // Initialize to safe state
  digitalWrite(nSleepPin, LOW);
  digitalWrite(DRVOffPin, HIGH);
  digitalWrite(outputEnablePin, LOW);
  digitalWrite(outputDirectionPin, LOW);

  digitalWrite(nSleepPin, HIGH);
  Serial.println("DRV8706 Waking Up!");
  // delay(100);

  digitalWrite(DRVOffPin, LOW);
  Serial.println("DRV8706 Output Enabled! Outputs off...");
  // delay(100);

  rgbLedWrite(RGBLedPin, 0, 0, 0);

  // Initialize WiFi with WiFiManager (captive portal support)
  initWiFi();

  if (MDNS.begin("orintechbox")) { // go to -> http://orintechbox.local
      Serial.println("mDNS responder started");
    } else {
      Serial.println("Error setting up MDNS responder!");
    }

  initFS();

  if (!loadSettings())
  {
    Serial.println("Failed to load settings. Using defaults.");
    setDefaultSettings();
  }

  initWebSocket();

  server.onNotFound([](AsyncWebServerRequest *request)
                    { request->send(LittleFS, "/index.html", "text/html"); });

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(LittleFS, "/index.html", "text/html"); });

  server.serveStatic("/", LittleFS, "/");
  server.begin();

  reversestartTime = micros();
  samplingstartTime = micros();
  last_calculation = millis();
  resetPeakValues();
}

unsigned long lastReconnectAttempt = 0;
const unsigned long reconnectInterval = 10000; // 10s

void loop()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    unsigned long currentMillis = millis();
    if (currentMillis - lastReconnectAttempt >= reconnectInterval)
    {
      Serial.println("Reconnecting to WiFi...");
      WiFi.disconnect();
      wifiManager.autoConnect(ap_ssid);
      lastReconnectAttempt = currentMillis;
    }
  }

  if (peakPositiveVoltage == 0.0)
  {
    peakPositiveVoltage = FValue1.toFloat();
    peakNegativeVoltage = FValue1.toFloat();
    averagePositiveVoltage = FValue1.toFloat();
    averageNegativeVoltage = FValue1.toFloat();
  }

  ws.cleanupClients();

  currentTime = micros();
  currentTimeMillis = millis();

  if (isRunning == false)
  {
    rgbLedWrite(48, 0, 0, 0);           // led off
    digitalWrite(outputEnablePin, LOW); // Deactivate outputs
  }

  if (isRunning == true)
  {
    process_adc_data();                  // Process ADC data (this updates latestCurrent and latestRaw)
    rgbLedWrite(48, 128, 0, 0);          // Bright red to show outputs are active
    digitalWrite(outputEnablePin, HIGH); // Activate Outputs !Possible Danger! Should see PVDD on output!

    // Get the output voltage
    VoltControl_PWM = round((FValue1.toFloat()) / TargetVoltsConversionFactor);
    ledcWrite(VoltControl_PWM_Pin, VoltControl_PWM);

    if (positive_adc_count >= MAX_SAMPLES)
    {
      averagePositiveCurrent = ((positive_adc_sum / positive_adc_count) * SLOPE) + INTERCEPT;
      positive_adc_sum = 0;
      positive_adc_count = 0;
    }
    if (negative_adc_count >= MAX_SAMPLES)
    {
      averageNegativeCurrent = ((negative_adc_sum / negative_adc_count) * SLOPE) + INTERCEPT;
      if (fabs(averageNegativeCurrent) >= 1.1 * fabs(averagePositiveCurrent))
      {
        averageNegativeCurrent = -averagePositiveCurrent;
      }
      negative_adc_sum = 0;
      negative_adc_count = 0;
    }

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
        // Apply saturation fix for peak current as well
        if (fabs(peakNegativeCurrent) >= 1.1 * fabs(peakPositiveCurrent))
        {
          peakNegativeCurrent = -peakPositiveCurrent;
        }
      }
    }

    if (outputDirection == true)
    { // Currently in FORWARD direction
      if (currentTime - reversestartTime >= ForwardTimeInt * 1000)
      {
        reversestartTime = currentTime;
        outputDirection = false; // Switch to reverse
        digitalWrite(outputDirectionPin, outputDirection);
      }
    }
    else
    { // Currently in REVERSE direction
      if (currentTime - reversestartTime >= ReverseTimeInt * 1000)
      {
        reversestartTime = currentTime;
        outputDirection = true; // Switch to forward
        digitalWrite(outputDirectionPin, outputDirection);
      }
    }

    if (currentTimeMillis >= 60000 && !hasResetPeakCurrent)
    {
      hasResetPeakCurrent = true;
      resetPeakValues();
      notifyClients(getValues());
    }

    if (millis() - lastNotifyTime >= notifyInterval)
    {
      lastNotifyTime = millis();
      notifyClients(getValues());
      // Serial.print(">AveragePosCurrent:");
      // Serial.println(averagePositiveCurrent);
      // Serial.print(">AverageNegCurrent:");
      // Serial.println(averageNegativeCurrent);
    }
  }
}