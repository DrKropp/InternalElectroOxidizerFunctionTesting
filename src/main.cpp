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

// TK get rid of hard coded security information before release!
// TK use the ESP32 as a wifi access point local network with secure login credentials. User access control?

// Replace with your network credentials
const char *ssid = "ExcitonClean";
const char *password = "sunnycarrot023";
const char *hostname = "ESP32S3WebServer";

// Create AsyncWebServer object on port 80
AsyncWebServer server(80); //TK Change to port 443 for secure network
// Create a WebSocket object

AsyncWebSocket ws("/ws");

String message = "";
String runState = "FALSE";
String outputVoltage = "14";
String forwardTimeMS = "20";
String reverseTimeMS = "40";
String FValue3 = "15";
String RValue3 = "15";
String targetVolts = "0.0"; // targetVolts holds target voltage 10.0<TargetVolts<26.0 0.1V resolution
//String reverseTimeMS = "0"; // reverseTime sets the reversal time in mS

// Duty cycles
int dutyCycle1F;
int dutyCycle1R;
int dutyCycle2F;
int dutyCycle2R;
int dutyCycle3F;
int dutyCycle3R;


//Json Variable to Hold Values
JsonDocument controlValues;

//Get Values
String getValues(){

// controlValues["runState"] = runState;
// controlValues["targetVolts"] = targetVolts;
// controlValues["reverseTime"] = reverseTimeMS;
controlValues["outputVoltage"] = String(outputVoltage);
controlValues["forwardTimeMS"] = String(forwardTimeMS);
controlValues["reverseTimeMS"] = String(reverseTimeMS);
controlValues["FValue3"] = String(FValue3);
controlValues["RValue3"] = String(RValue3);


String output;

controlValues.shrinkToFit();  // optional
serializeJson(controlValues, output);
return output;
}

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
#define CONVERSIONS_PER_PIN 5
uint8_t SO_Pin[] = {2};                   // Analog signal proportional to output current, values below VCC/2 for negative current
volatile bool adc_coversion_done = false; // Flag which will be set in ISR when conversion is done
adc_continuous_data_t *result = NULL;     // Result structure for ADC Continuous reading

// ISR Function that will be triggered when ADC conversion is done
void ARDUINO_ISR_ATTR adcComplete()
{
  adc_coversion_done = true;
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

// put function declarations here:

// Initialize WiFi
void initWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
  Serial.println(WiFi.localIP());
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

void notifyClients(String values) {
  ws.textAll(values);
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
      outputVoltage = message.substring(2);
      dutyCycle1F = map(outputVoltage.toInt(), 0, 100, 0, 255);
      Serial.println(dutyCycle1F);
      //Serial.print(getValues());
      notifyClients(getValues());
    }
    if (message.indexOf("2F") >= 0) {
      forwardTimeMS = message.substring(2);
      dutyCycle2F = map(forwardTimeMS.toInt(), 0, 100, 0, 255);
      Serial.println(dutyCycle2F);
      Serial.print(getValues());
      notifyClients(getValues());
    }
    if (message.indexOf("2R") >= 0) {
      reverseTimeMS = message.substring(2);
      dutyCycle2R = map(reverseTimeMS.toInt(), 0, 100, 0, 255);
      Serial.println(dutyCycle2R);
      Serial.print(getValues());
      notifyClients(getValues());
    }
    if (message.indexOf("3F") >= 0) {
      FValue3 = message.substring(2);
      dutyCycle3F = map(FValue3.toInt(), 0, 100, 0, 255);
      Serial.println(dutyCycle3F);
      Serial.print(getValues());
      notifyClients(getValues());
    }
    if (message.indexOf("3R") >= 0) {
      RValue3 = message.substring(2);
      dutyCycle3R = map(RValue3.toInt(), 0, 100, 0, 255);
      Serial.println(dutyCycle3R);
      Serial.print(getValues());
      notifyClients(getValues());
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

void setup() // Runs once after reset
{
  // Initialize Serial communication
  Serial.begin(460800);
  delay(5000); // Wait for serial monitor to be ready
  // Serial.println("startup OK");
  rgbLedWrite(RGBLedPin, 0, 55, 0);

  // Enable input and output pins
  testAttach = ledcAttach(VoltControl_PWM_Pin, PWMFreq, outputBits); // Pin 8 to output PWM and control output voltage
  if (testAttach == false)
  {
    Serial.println("Error in RSP1000-24 Control");
  }

  pinMode(outputEnablePin, OUTPUT);
  pinMode(outputDirectionPin, OUTPUT);
  pinMode(nSleepPin, OUTPUT);
  pinMode(DRVOffPin, OUTPUT);
  pinMode(nFaultPin, INPUT); // Fault indicator output pulled low to indicate fault condition, requires pullup resistor

  analogContinuousSetWidth(12);                                          // Set the resolution to 9-12 bits (default is 12 bits)
  analogContinuousSetAtten(ADC_11db);                                    // Optional: Set different attenaution (default is ADC_11db)
  analogContinuous(SO_Pin, 1, CONVERSIONS_PER_PIN, 20000, &adcComplete); // Setup ADC Continuous, how many conversions to average, sampling frequency, callback function
  analogContinuousStart();                                               // Start ADC Continuous conversions

  pinMode(testButton, INPUT_PULLUP);

  // Assert all pins to safe starting values
  digitalWrite(nSleepPin, LOW);          // Send sleep signal
  digitalWrite(DRVOffPin, HIGH);         // Send output OFF signal
  digitalWrite(outputEnablePin, LOW);    // Initialize EN signal
  digitalWrite(outputDirectionPin, LOW); // Initialize PH signal

  // At this point the DRV8706H-Q1 Should be asleep with no activity on the H-Bridge mosfets

  ledcWrite(VoltControl_PWM_Pin, VoltControl_PWM); // Set the RSP-1000-24 to a low but stable output voltage, about 9V
  Serial.print("RSP1000-24 Voltage Set to ");
  Serial.print(VoltControl_PWM * TargetVoltsConversionFactor);
  Serial.println("V");
  rgbLedWrite(RGBLedPin, 0, 23, 10); // Blue to show that RSP-1000-24 voltage is being set
  delay(100);                        // wait 1 second for power supply to stabilize

  digitalWrite(nSleepPin, HIGH); // Wake the DRV8706 but don't output any drive signals to the H-Bridge
  Serial.println("DRV8706 Waking Up!");
  rgbLedWrite(RGBLedPin, 0, 23, 0); // Green for 1 second to show that DRV8706 is awake
  delay(100);

  digitalWrite(DRVOffPin, LOW); // Enable outputs but don't activate them
  Serial.println("DRV8706 Output Enabled! Outputs off...");
  rgbLedWrite(48, 23, 23, 23); // White for 1 second to show that DRV8706 outputs are enabled
  delay(100);

  rgbLedWrite(RGBLedPin, 0, 0, 0); // LED off when setup completed

  initWiFi();

    // Print ESP Local IP Address
    Serial.println(WiFi.localIP());

    initFS();
    initWebSocket();

    // Web Server Root URL
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(LittleFS, "/index.html", "text/html");
    });

    server.serveStatic("/", LittleFS, "/");

    // Start server
    server.begin(); 

    runstartTime = millis();
    reversestartTime = micros();
    samplingstartTime = micros(); // Add a small 17uS offset to sampling start time to prevent interference with other operations
}

void loop()
{
  ws.cleanupClients();
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
    //   Serial.print("OutputVoltage = ");
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
    VoltControl_PWM = round(outputVoltage.toFloat() / TargetVoltsConversionFactor);
    ledcWrite(VoltControl_PWM_Pin, VoltControl_PWM); // Set the RSP1000-24 output voltage to the target value

    if (digitalRead(testButton) == LOW) // Watch for another button press, disable the output
    {
      digitalWrite(outputEnablePin, LOW);
      isRunning = false;
      delay(250);
    }

    if(outputDirection == false){ // Runs when output direction is forward
      if (currentTime - reversestartTime >= forwardTimeMS.toInt() * 1000) // Non-Blocking time based control loop for reversing current direction
      {
        reversestartTime = currentTime;
        outputDirection = !outputDirection;                // Reverse the output direction variable
        digitalWrite(outputDirectionPin, outputDirection); // Change the output direction
      }
    } else { // Runs when output direction is reverse
      if (currentTime - reversestartTime >= reverseTimeMS.toInt() * 1000) // Non-Blocking time based control loop for reversing current direction
      {
        reversestartTime = currentTime;
        outputDirection = !outputDirection;                // Reverse the output direction variable
        digitalWrite(outputDirectionPin, outputDirection); // Change the output direction
      }
    }

    if (currentTime - samplingstartTime >= samplingTime)
    {
      samplingstartTime = currentTime;
      if (adc_coversion_done == true)
      {
        // Set ISR flag back to false
        adc_coversion_done = false;
        // Read data from ADC
        if (analogContinuousRead(&result, 0))
        {
          analogContinuousStop(); // Stop ADC Continuous conversions to have more time to process (print) the data
          // Serial.print(">SOADC:"); // Send formatted serial output to Teleplot serial data plotter
          // Serial.println(result[0].avg_read_mvolts);
          analogContinuousStart(); // Start ADC conversions and wait for callback function to set adc_coversion_done flag to true
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
