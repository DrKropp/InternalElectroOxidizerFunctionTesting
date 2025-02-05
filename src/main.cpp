#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "LittleFS.h"
#include <ArduinoJson.h>

//Adding a comment to see if this lets me upload to git

// Replace with your network credentials
const char *ssid = "ExcitonClean";
const char *password = "sunnycarrot023";
const char *hostname = "ESP32S3WebServer";

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
// Create a WebSocket object

AsyncWebSocket ws("/ws");

String message = "";
String sliderValue1 = "0";
String sliderValue2 = "0";
String sliderValue3 = "0";

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
bool isRunning = false;

// RSP-1000-24 Control Variables
const uint8_t outputBits = 10;  // 10 bit PWM resolution
const uint16_t PWMFreq = 25000; // 25kHz PWM Frequency
uint16_t VoltControl_PWM = 300; // PWM Setting=TargetVolts/TargetVoltsConversionFactor, Values outside range of 340 to 900 cause 24V supply fault conditions
float TargetVolts = 18.0;

// Variables used for timing
uint32_t currentTime = 0;       // Store the current time in uS
uint32_t currentTimeMillis = 0; //Store the current time in mS
uint32_t runstartTime = 0;      // Store the start time
uint32_t runTime = 3600000*4;  // mS time to run the system after button press.
uint32_t reversestartTime = 0;  // Store the reversal cycle start time
uint32_t reverseTime = 40000;   // uS time between reversals
uint32_t samplingstartTime = 0; // Store the sampling start time
uint32_t samplingTime = 100;    // uS between taking current measurements

// Variables for storing sensor outputs
float averageoutputCurrent = 0.0;   // Converted average current value
float switchingoutputCurrent = 0.0; // output current measured immediately after changing direction
uint16_t SO_ADC;                    // raw, unscaled current output reading

// Some other constants
const float TargetVoltsConversionFactor = 0.0301686; // Slope Value from calibration 16Jan2025

bool testAttach = false; // Did the forward pwm pin successfully attach?

// put function declarations here:

void setup() // Runs once after reset
{
  // Initialize Serial communication
  Serial.begin(460800);
  delay(100); // Wait for serial monitor to be ready
  // Serial.println("startup OK");
  rgbLedWrite(RGBLedPin, 0, 55, 0);

  // Enable input and output pins
  testAttach = ledcAttach(VoltControl_PWM_Pin, PWMFreq, outputBits); // Pin 8 to output PWM and control output voltage
  pinMode(outputEnablePin, OUTPUT);
  pinMode(outputDirectionPin, OUTPUT);
  pinMode(nSleepPin, OUTPUT);
  pinMode(DRVOffPin, OUTPUT);
  pinMode(nFaultPin, INPUT); // Fault indicator output pulled low to indicate fault condition, requires pullup resistor

  /* analogReadResolution(12);       // ESP32-S3 has a default of 13 bits, so setting to 12 for range of 0-4095
  analogSetAttenuation(ADC_11db); // Read up to 3.1V
  pinMode(SO_Pin, ANALOG); */

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
  Serial.print(TargetVolts);
  Serial.println("V");
  rgbLedWrite(RGBLedPin, 0, 23, 10); // Blue to show that RSP-1000-24 voltage is being set
  delay(100);                        // wait 1 second for power supply to stabilize

  digitalWrite(nSleepPin, HIGH); // Wake the DRV8706 but don't output any drive signals to the H-Bridge
  Serial.println("DRV8706 Waking Up!");
  rgbLedWrite(RGBLedPin, 0, 23, 0); // Green for 1 second to show that DRV8706 is awake
  delay(100);

  digitalWrite(DRVOffPin, LOW); // Enable outputs but don't activate them
  Serial.println("DRV8706 Output Enabled! Outputs off...");
  rgbLedWrite(48, 23, 23, 23);  // White for 1 second to show that DRV8706 outputs are enabled
  delay(100);

  rgbLedWrite(RGBLedPin, 0, 0, 0); // LED off when setup completed
}

void loop()
{

  currentTime = micros();
  currentTimeMillis = millis(); 

  if (digitalRead(testButton) == LOW) // Watch for the button press, then wake the DRV8706, enable the outputs, then drive the output
  {

    VoltControl_PWM = round(TargetVolts / TargetVoltsConversionFactor);
    ledcWrite(VoltControl_PWM_Pin, VoltControl_PWM); // Set the RSP1000-24 output voltage to the target value
    // Serial.println("Pumping out the POWER!");
    digitalWrite(outputEnablePin, HIGH); // Activate Outputs !Possible Danger! Should see PVDD on output!
    rgbLedWrite(48, 128, 0, 0);          // Bright red to show outputs are active

    runstartTime = currentTimeMillis;
    reversestartTime = currentTime;
    samplingstartTime = currentTime; // Add a small 17uS offset to sampling start time to prevent interference with other operations
    isRunning = true;
    delay(100); //Need to implement a more robust solution for user holding the button down.
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
    if (currentTime - reversestartTime >= reverseTime) // Non-Blocking time based control loop for reversing current direction
    {
      reversestartTime = currentTime;
      outputDirection = !outputDirection;                // Reverse the output direction variable
      digitalWrite(outputDirectionPin, outputDirection); // Change the output direction
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
          analogContinuousStop();  // Stop ADC Continuous conversions to have more time to process (print) the data
          Serial.print(">SOADC:"); // Send formatted serial output to Teleplot serial data plotter
          Serial.println(result[0].avg_read_mvolts);
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
