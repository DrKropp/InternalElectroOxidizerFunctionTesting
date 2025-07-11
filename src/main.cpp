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
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "LittleFS.h"
#include <ArduinoJson.h>
#include <string>
#include <DNSServer.h>

// TK get rid of hard coded security information before release!
// TK use the ESP32 as a wifi access point local network with secure login credentials. User access control?

// Replace with your network credentials
// const char *ssid = "ExcitonClean";
// const char *password = "sunnycarrot023";
const char *ssid = "ekotestbox01";  
const char *password = "myvoiceismypassword"; 
const char *hostname = "OrinTechBox01";
// DNS server
const byte DNS_PORT = 53;
DNSServer dnsServer;

// AP Config (Unused after switching to raspberry pi connect)
// IPAddress apIP(192, 168, 4, 1);
// IPAddress netMsk(255, 255, 255, 0);
// char ap_ssid[32] = "OrinTechE0";
// String ap_password = "EO-3PasswordField";

// Create AsyncWebServer object on port 80
AsyncWebServer server(80); //TK Change to port 443 for secure network
// Create a WebSocket object

AsyncWebSocket ws("/ws");

String message = "";
String runState = "FALSE";

String FValue1 = "14"; // OUTPUT VOLTAGE
String FValue2 = "4000"; // FORWARD TIME
String RValue2 = "4000"; // REVERSE TIME
String FValue3 = "15"; // FOWARD CURRENT
String RValue3 = "15"; // REVERSE CURRENT

String targetVolts = "0.0"; // targetVolts holds target voltage 10.0<TargetVolts<26.0 0.1V resolution
//String RValue2 = "0"; // reverseTime sets the reversal time in mS

// Duty cycles
int dutyCycle1F;
int dutyCycle1R;
int dutyCycle2F;
int dutyCycle2R;
int dutyCycle3F;
int dutyCycle3R;

// Output variables
float outputCurrent = 0.0;  // Amps
float outputVoltage = 0.0;  // Volts

// Current and Voltage readings
float peakPositiveCurrent = 0.0;
float peakNegativeCurrent = 0.0;
float averagePositiveCurrent = 0.0;
float averageNegativeCurrent = 0.0;
float peakPositiveVoltage = 0.0;
float peakNegativeVoltage = 0.0;
float averagePositiveVoltage = 0.0;
float averageNegativeVoltage = 0.0;



//Json Variable to Hold Values
JsonDocument controlValues;

//Get Values
String getValues(){

// controlValues["runState"] = runState;
// controlValues["targetVolts"] = targetVolts;
// controlValues["reverseTime"] = RValue2;
controlValues["FValue1"] = String(FValue1);
controlValues["FValue2"] = String(FValue2);
controlValues["RValue2"] = String(RValue2);
controlValues["FValue3"] = String(FValue3);
controlValues["RValue3"] = String(RValue3);
controlValues["peakPositiveCurrent"] = String(peakPositiveCurrent, 3);
controlValues["peakNegativeCurrent"] = String(peakNegativeCurrent, 3);
controlValues["averagePositiveCurrent"] = String(averagePositiveCurrent, 3);
controlValues["averageNegativeCurrent"] = String(averageNegativeCurrent, 3);
controlValues["peakPositiveVoltage"] = String(peakPositiveVoltage);
controlValues["peakNegativeVoltage"] = String(peakNegativeVoltage);
controlValues["averagePositiveVoltage"] = String(averagePositiveVoltage);
controlValues["averageNegativeVoltage"] = String(averageNegativeVoltage);

String output;

controlValues.shrinkToFit();  // optional
serializeJson(controlValues, output);
return output;
}

// helper variables
uint16_t samplesPerAverage = 50;
float forwardSum = 0.0; // Sum of current readings for averaging
float reverseSum = 0.0;
uint16_t forwardCount = 0; // Count of current readings for averaging
uint16_t reverseCount = 0; // Count of current readings for averaging

// Define some GPIO connections between ESP32-S3 and DRV8706H-Q1
const uint8_t VoltControl_PWM_Pin = 8; // GPIO 8 PWM Output will adjust 24V power supply output, PWM Setting=TargetVolts/TargetVoltsConversionFactor
const uint8_t outputEnablePin = 4;     // In1/EN: Turn on output mosfets in H-Bridge, direction set by PH
const uint8_t nHiZ1Pin = 5;            // Physically connected but unused in mode 2
const uint8_t outputDirectionPin = 6;  // In2/PH: Controls H-Bridge output direction, Low is Reverse, High is Forward
const uint8_t nHiZ2Pin = 7;            // Physically connected but unused in mode 2
const uint8_t nSleepPin = 15;          // Can put DRV8706H-Q1 into sleep mode, High to wake, Low to Sleep
const uint8_t DRVOffPin = 16;          // Disable DRV8706H-Q1 drive output without affecting other subsystems, High disables output
const uint8_t nFaultPin = 17;          // Fault indicator output pulled low to indicate fault condition

// Structures for rapid ADC reading
#define CONVERSIONS_PER_PIN 500
uint8_t SO_Pin[] = {2};                   // Analog signal proportional to output current, values below VCC/2 for negative current
volatile bool adc_conversion_done = false; // Flag which will be set in ISR when conversion is done
adc_continuous_data_t *result = NULL;     // Result structure for ADC Continuous reading

// ISR Function that will be triggered when ADC conversion is done
void ARDUINO_ISR_ATTR adcComplete()
{
  adc_conversion_done = true;
}

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
uint32_t runstartTime = 0;      // Store the start time
uint32_t runTime = 3600000 * 8; // mS time to run the system after button press. Currently 8 hours
uint32_t reversestartTime = 0;  // Store the reversal cycle start time
uint32_t reverseTimeUS = 40000;   // uS time between reversals
uint32_t samplingstartTime = 0; // Store the sampling start time
uint32_t samplingTime = 100;    // uS between taking current measurements

// Variables for storing sensor outputs
float averageoutputCurrent = 0.0;   // Converted average current value
float switchingoutputCurrent = 0.0; // output current measured immediately after changing direction
uint16_t SO_ADC;                    // raw, unscaled current output reading

// Some other constants
const float TargetVoltsConversionFactor = 0.0301686059427937; // Slope Value from calibration 16Jan2025

bool testAttach = false; // Did the forward pwm pin successfully attach?

// ADC Constants
//const float CURRENT_ZERO_POINT = 2019;  
//const float SLOPE = 51.1f;     
const float CURRENT_ZERO_POINT = 2045; // From calibration 7/5/25
const float SLOPE = 52.1f;     // From calibration 7/5/25

// put function declarations here:

// Initialize WiFi
// void initWiFi() {
//   WiFi.mode(WIFI_STA);
//   WiFi.begin(ssid, password);
//   Serial.print("Connecting to WiFi ..");
//   while (WiFi.status() != WL_CONNECTED) {
//     Serial.print('.');
//     delay(1000);
//   }
//   Serial.println(WiFi.localIP());
// }

// void setupAP() {
//     // uint8_t mac[6];
//     // WiFi.macAddress(mac);
//     // snprintf(ap_ssid, sizeof(ap_ssid), "esp_%02X%02X%02X%02X%02X%02X", 
//     //         mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

//     strncpy(ap_ssid, custom_ssid, sizeof(ap_ssid));

//     WiFi.softAPConfig(apIP, apIP, netMsk);
//     WiFi.softAP(ap_ssid, ap_password.c_str());
    
//     dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
//     dnsServer.start(DNS_PORT, "*", apIP);
    
//     Serial.print("Setting up AP: ");
//     Serial.println(ap_ssid);
//     Serial.print("AP IP: ");
//     Serial.println(WiFi.softAPIP());
// }

// void initWiFi() {
//   WiFi.setHostname(hostname);
//   WiFi.mode(WIFI_STA);
//   WiFi.begin(ssid, password);
//   Serial.print("Connecting to WiFi ..");
  
//   int attempts = 0;
//   while (WiFi.status() != WL_CONNECTED && attempts < 20) {
//     Serial.print('.');
//     delay(500);
//     attempts++;
//   }
  
//   if (WiFi.status() == WL_CONNECTED) {
//     Serial.println("\nConnected! Hostname: " + String(hostname));
//     Serial.println("IP address: " + WiFi.localIP().toString());
//   } else {
//     Serial.println("\nFailed to connect to WiFi!");
//   }
// }

// void scanNetworks() {
//   Serial.println("Scanning networks...");
//   int n = WiFi.scanNetworks();
//   for (int i = 0; i < n; i++) {
//     Serial.printf("%s (%d dBm)\n", WiFi.SSID(i).c_str(), WiFi.RSSI(i));
//   }
// }

void initWiFi() {
  WiFi.setHostname(hostname);
  WiFi.mode(WIFI_STA);
  delay(100);

  //scanNetworks(); // Scan for available networks

  WiFi.begin(ssid, password);
  Serial.println("\nConnecting to WiFi...");

  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 20000) { // 20s timeout
    Serial.printf("WiFi Status: %d\n", WiFi.status());
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected!");
    Serial.print("SSID: "); Serial.println(WiFi.SSID());
    Serial.print("IP: "); Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nFailed to connect!");
    Serial.println("Possible causes:");
    Serial.println("- Wrong SSID/password");
    Serial.println("- Hotspot not in 2.4GHz mode");
    Serial.println("- Weak signal");
  }
}

// Initialize LittleFS
void initFS() {
  if (!LittleFS.begin()) {
    Serial.println("An error has occurred while mounting LittleFS");
  }
  else{
   Serial.println("LittleFS mounted successfully");
  }
}

// HELPER FUNCTIONS
// Check if string is an IP address
bool isIp(String str) {
  for (size_t i = 0; i < str.length(); i++) {
    int c = str.charAt(i);
    if (c != '.' && (c < '0' || c > '9')) {
      return false;
    }
  }
  return true;
}

// Convert IPAddress to String
String toStringIp(IPAddress ip) {
  return String(ip[0]) + "." + ip[1] + "." + ip[2] + "." + ip[3];
}

void notifyClients(String values) {
  ws.textAll(values);
}

void resetPeakValues() {
  peakPositiveCurrent = 0.0;
  peakNegativeCurrent = 0.0;
  averagePositiveCurrent = 0.0;
  averageNegativeCurrent = 0.0;
  peakPositiveVoltage = FValue1.toFloat();
  peakNegativeVoltage = FValue1.toFloat();
  averagePositiveVoltage = FValue1.toFloat();
  averageNegativeVoltage = FValue1.toFloat();
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    data[len] = 0;
    message = (char*)data;
    //Serial.println(message);
    if(message.indexOf("toggle") >= 0) {
      Serial.println("Toggled state");
      isRunning = !isRunning;
      notifyClients(getValues());
    }
    if (message.indexOf("1F") >= 0) {
      FValue1 = message.substring(2);
      dutyCycle1F = map(FValue1.toInt(), 0, 100, 0, 255);
      //Serial.println(dutyCycle1F);
      Serial.println(getValues());
      notifyClients(getValues());
      resetPeakValues();
    }
    if (message.indexOf("2F") >= 0) {
      FValue2 = message.substring(2);
      dutyCycle2F = map(FValue2.toInt(), 0, 100, 0, 255);
      //Serial.println(dutyCycle2F);
      Serial.println(getValues());
      notifyClients(getValues());
      resetPeakValues();
    }
    if (message.indexOf("2R") >= 0) {
      RValue2 = message.substring(2);
      dutyCycle2R = map(RValue2.toInt(), 0, 100, 0, 255);
      //Serial.println(dutyCycle2R);
      Serial.println(getValues());
      notifyClients(getValues());
      resetPeakValues();
    }
    if (message.indexOf("3F") >= 0) {
      FValue3 = message.substring(2);
      dutyCycle3F = map(FValue3.toInt(), 0, 100, 0, 255);
      //Serial.println(dutyCycle3F);
      Serial.println(getValues());
      notifyClients(getValues());
      resetPeakValues();
    }
    if (message.indexOf("3R") >= 0) {
      RValue3 = message.substring(2);
      dutyCycle3R = map(RValue3.toInt(), 0, 100, 0, 255);
      //Serial.println(dutyCycle3R);
      Serial.println(getValues());
      notifyClients(getValues());
      resetPeakValues();
    }
    if (strcmp((char*)data, "getValues") == 0) {
      notifyClients(getValues());
    }
  }
}
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
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

void initWebSocket() {
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

void setup() {
  Serial.begin(460800);
  delay(5000);
  rgbLedWrite(RGBLedPin, 0, 55, 0);

  testAttach = ledcAttach(VoltControl_PWM_Pin, PWMFreq, outputBits);
  if (!testAttach) Serial.println("Error in RSP1000-24 Control");

  pinMode(outputEnablePin, OUTPUT);
  pinMode(outputDirectionPin, OUTPUT);
  pinMode(nSleepPin, OUTPUT);
  pinMode(DRVOffPin, OUTPUT);
  pinMode(nFaultPin, INPUT);

  analogContinuousSetWidth(12);
  analogContinuousSetAtten(ADC_11db);
  analogContinuous(SO_Pin, 1, CONVERSIONS_PER_PIN, 20000, &adcComplete);
  analogContinuousStart();

  pinMode(testButton, INPUT_PULLUP);

  // Initialize to safe state
  digitalWrite(nSleepPin, LOW);
  digitalWrite(DRVOffPin, HIGH);
  digitalWrite(outputEnablePin, LOW);
  digitalWrite(outputDirectionPin, LOW);

  ledcWrite(VoltControl_PWM_Pin, VoltControl_PWM);
  rgbLedWrite(RGBLedPin, 0, 23, 10);
  delay(100);

  digitalWrite(nSleepPin, HIGH);
  Serial.println("DRV8706 Waking Up!");
  rgbLedWrite(RGBLedPin, 0, 23, 0);
  delay(100);

  digitalWrite(DRVOffPin, LOW);
  Serial.println("DRV8706 Output Enabled! Outputs off...");
  rgbLedWrite(48, 23, 23, 23);
  delay(100);

  rgbLedWrite(RGBLedPin, 0, 0, 0);

  // Initialize WiFi and filesystem
  initWiFi();
  initFS();
  initWebSocket();

  server.onNotFound([](AsyncWebServerRequest *request){
    request->send(LittleFS, "/index.html", "text/html");
  });

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/index.html", "text/html");
  });

  server.serveStatic("/", LittleFS, "/");
  server.begin();

  runstartTime = millis();
  reversestartTime = micros();
  samplingstartTime = micros();
  resetPeakValues(); // Reset peak values at startup
}

unsigned long lastReconnectAttempt = 0;
const unsigned long reconnectInterval = 10000; // 10s

void loop()
{
 if (WiFi.status() != WL_CONNECTED) {
    unsigned long currentMillis = millis();
    if (currentMillis - lastReconnectAttempt >= reconnectInterval) {
      Serial.println("Reconnecting to WiFi...");
      WiFi.disconnect();
      WiFi.reconnect();
      lastReconnectAttempt = currentMillis;
    }
  }
  if(peakPositiveVoltage == 0.0){
    float peakPositiveVoltage = FValue1.toFloat();
    float peakNegativeVoltage = FValue1.toFloat();
    float averagePositiveVoltage = FValue1.toFloat();
    float averageNegativeVoltage = FValue1.toFloat();
  }
  //dnsServer.processNextRequest(); // Handle DNS queries
  ws.cleanupClients();

  // static unsigned long lastDnsProcess = 0;
  // if (millis() - lastDnsProcess > 10) {
  //     dnsServer.processNextRequest();
  //     lastDnsProcess = millis();
  // }

  currentTime = micros();
  currentTimeMillis = millis();



  if (isRunning == false)
  {
    rgbLedWrite(48, 0, 0, 0); // led off
    digitalWrite(outputEnablePin, LOW); // Deactivate outputs
    // if (digitalRead(testButton) == LOW) // Watch for the button press, then wake the DRV8706, enable the outputs, then drive the output
    // {
    //   VoltControl_PWM = round(TargetVolts / TargetVoltsConversionFactor);
    //   ledcWrite(VoltControl_PWM_Pin, VoltControl_PWM); // Set the RSP1000-24 output voltage to the target value
    //   Serial.print("FValue1 = ");
    //   Serial.println(VoltControl_PWM * TargetVoltsConversionFactor);
    //   digitalWrite(outputEnablePin, HIGH); // Activate Outputs !Possible Danger! Should see PVDD on output!
      

      
    //   isRunning = true;
    //   delay(250); // Need to implement a more robust solution for user holding the button down.
    // }
  }

  if (currentTimeMillis - runstartTime >= runTime) // Turn off the output after the run is over
  {
    digitalWrite(outputEnablePin, LOW);
    // Serial.println("Test is over, deactivating outputs");
    rgbLedWrite(48, 0, 0, 0); // led off
    isRunning = false;
  }

  if (isRunning == true)
  {
    rgbLedWrite(48, 128, 0, 0);          // Bright red to show outputs are active
    digitalWrite(outputEnablePin, HIGH); // Activate Outputs !Possible Danger! Should see PVDD on output!

    // Get the output voltage
    VoltControl_PWM = round((FValue1.toFloat() - 0.3) / TargetVoltsConversionFactor); // TEMP 0.3 VALUE FOR OFFSET UNTIL NEW CALIBRATION
    ledcWrite(VoltControl_PWM_Pin, VoltControl_PWM); // Set the RSP1000-24 output voltage to the target value

    if (digitalRead(testButton) == LOW) // Watch for another button press, disable the output
    {
      digitalWrite(outputEnablePin, LOW);
      isRunning = false;
      delay(250);
    }

    if(outputDirection == false){ // Runs when output direction is forward
      if (currentTime - reversestartTime >= FValue2.toInt() * 1000) // Non-Blocking time based control loop for reversing current direction
      {
        reversestartTime = currentTime;
        outputDirection = !outputDirection;                // Reverse the output direction variable
        digitalWrite(outputDirectionPin, outputDirection); // Change the output direction
      }
    } else { // Runs when output direction is reverse
      if (currentTime - reversestartTime >= RValue2.toInt() * 1000) // Non-Blocking time based control loop for reversing current direction
      {
        reversestartTime = currentTime;
        outputDirection = !outputDirection;                // Reverse the output direction variable
        digitalWrite(outputDirectionPin, outputDirection); // Change the output direction
      }
    }

    if (currentTime - samplingstartTime >= samplingTime && currentTime >= 100000) // currentTime >= 100000 to avoid sampling at startup
    {
      samplingstartTime = currentTime;
      if (adc_conversion_done == true)
      {
        // Set ISR flag back to false
        adc_conversion_done = false;
        // Read data from ADC
        if (analogContinuousRead(&result, 0))
        {
          analogContinuousStop(); // Stop ADC Continuous conversions to have more time to process (print) the data

          uint32_t current_mV = result[0].avg_read_mvolts;
          
         //outputCurrent = (((result[0].avg_read_mvolts/1000.0f)*(VOLTSTOCOUNTS))-CURRENT_ZERO_POINT)/SLOPE; // Convert mV to amps -> Amps = (ADCcount - VCC/2)/slope
          outputCurrent = ((result[0].avg_read_raw)-CURRENT_ZERO_POINT)/SLOPE;

          if(outputCurrent > peakPositiveCurrent && result[0].avg_read_raw > 100){ // Only update peak current if the reading is above a threshold to avoid noise
            peakPositiveCurrent = outputCurrent;
            notifyClients(getValues());
          }
          if(outputCurrent < peakNegativeCurrent && result[0].avg_read_raw > 100){
            peakNegativeCurrent = outputCurrent;
            notifyClients(getValues());
          }

          if(outputDirection == true && result[0].avg_read_raw > 100){ // Forward direction
            if(forwardCount < samplesPerAverage){
              forwardSum += outputCurrent; 
              forwardCount++;
            } else {
              averagePositiveCurrent = forwardSum / samplesPerAverage; 
              forwardSum = 0.0;
              forwardCount = 0;
            }
          } else { // Reverse direction
            if(reverseCount < samplesPerAverage && result[0].avg_read_raw > 100){
              reverseSum += outputCurrent; 
              reverseCount++;
            } else {
              averageNegativeCurrent = reverseSum / samplesPerAverage; 
              reverseSum = 0.0;
              reverseCount = 0;
            }
          }
          // Serial.printf("\nADC PIN %d data:", result[0].pin);
          // Serial.printf("\n   Avg raw value = %d", result[0].avg_read_raw);
          // Serial.printf("\n   Avg millivolts value = %d", result[0].avg_read_mvolts);
          // Serial.printf("\n   Avg Counts value = %f", ((result[0].avg_read_mvolts/1000.0f)*(VOLTSTOCOUNTS)));
          // Serial.printf("\n   Avg Amps value = %f", outputCurrent);

          
          //float rawCounts = (result[0].avg_read_mvolts / 1000.0f) * (ADC_MAX_COUNTS / ADC_REF_VOLTAGE);

          //outputCurrent = (rawCounts - CURRENT_ZERO_POINT) / SLOPE; // Convert raw counts to amps

          // Serial.print(">SOADC:"); // Send formatted serial output to Teleplot serial data plotter
          // Serial.println(outputCurrent);

          // Serial.print(">SOADC2:");
          // Serial.println(result[0].avg_read_raw);

          // Serial.print(">SOADC3:");
          // Serial.println(result[0].avg_read_mvolts);

          //Serial.print(">SOADC:"); // Send formatted serial output to Teleplot serial data plotter
          //Serial.println(result[0].avg_read_mvolts);
          analogContinuousStart(); // Start ADC conversions and wait for callback function to set adc_conversion_done flag to true
        }
        else
        {
          Serial.println("Error occurred during reading data. Set Core Debug Level to error or lower for more information.");
        }
      }
      // SO_ADC = analogRead(SO_Pin);
      // Serial.print(">SOADC:");
      // Serial.println(SO_ADC);
    }
  }
}

