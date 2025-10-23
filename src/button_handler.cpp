/*
 * Button Multi-Reset Handler Implementation
 * OrinTech ElectroOxidizer Device
 */

#include "button_handler.h"
#include "multi_network.h"
#include <WiFiManager.h>
#include <LittleFS.h>

// ============================================================================
// EXTERNAL DECLARATIONS
// ============================================================================

extern WiFiManager wifiManager;

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================

unsigned long buttonPressTimestamps[BUTTON_RESET_COUNT];
uint8_t buttonPressIndex = 0;
bool lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;

// ============================================================================
// FUNCTION IMPLEMENTATIONS
// ============================================================================

void initButtonHandler()
{
  for (uint8_t i = 0; i < BUTTON_RESET_COUNT; i++)
  {
    buttonPressTimestamps[i] = 0;
  }
}

void checkButtonMultiReset()
{
  // Read current button state (active LOW)
  int reading = digitalRead(testButton);
  unsigned long currentTime = millis();

  // Debouncing logic
  if (reading != lastButtonState)
  {
    lastDebounceTime = currentTime;
  }

  // If button state has been stable for debounce period
  if ((currentTime - lastDebounceTime) > BUTTON_DEBOUNCE_MS)
  {
    // If button was pressed (transition from HIGH to LOW)
    if (lastButtonState == HIGH && reading == LOW)
    {
      Serial.println("Button press detected");

      // Record this button press timestamp
      buttonPressTimestamps[buttonPressIndex] = currentTime;
      buttonPressIndex = (buttonPressIndex + 1) % BUTTON_RESET_COUNT;

      // Check if we have enough presses within the time window
      if (detectButtonMultiReset())
      {
        Serial.println("\n*** BUTTON MULTI-RESET DETECTED ***");
        triggerWiFiReset();
      }
    }
  }

  lastButtonState = reading;
}

bool detectButtonMultiReset()
{
  unsigned long currentTime = millis();
  uint8_t validPresses = 0;

  // Count how many presses occurred within the time window
  for (uint8_t i = 0; i < BUTTON_RESET_COUNT; i++)
  {
    if (buttonPressTimestamps[i] > 0 &&
        (currentTime - buttonPressTimestamps[i]) <= BUTTON_RESET_WINDOW_MS)
    {
      validPresses++;
    }
  }

  // If we have enough valid presses, we detected a multi-reset
  if (validPresses >= BUTTON_RESET_COUNT)
  {
    // Clear the timestamps to prevent repeated triggers
    for (uint8_t i = 0; i < BUTTON_RESET_COUNT; i++)
    {
      buttonPressTimestamps[i] = 0;
    }
    return true;
  }

  return false;
}

void triggerWiFiReset()
{
  Serial.println("Triggering WiFi credential reset...");
  Serial.println("Clearing WiFi credentials and device name...");

  // Purple LED indicates reset in progress
  rgbLedWrite(RGBLedPin, 128, 0, 128);

  // Clear stored credentials
  wifiManager.resetSettings();

  // Clear all saved networks
  if (LittleFS.exists("/networks.json"))
  {
    LittleFS.remove("/networks.json");
    Serial.println("All saved networks: Cleared");
  }
  initMultiNetworkStorage();

  // Clear device name
  if (LittleFS.exists("/devicename.json"))
  {
    LittleFS.remove("/devicename.json");
    Serial.println("Device name: Cleared");
  }

  // Reset to default identifiers
  uint32_t chipId = ESP.getEfuseMac();
  snprintf(chip_id_hex, sizeof(chip_id_hex), "%08X", (uint32_t)(chipId & 0xFFFFFFFF));
  snprintf(hostname, sizeof(hostname), "OrinTech-%s", chip_id_hex);
  snprintf(deviceName, sizeof(deviceName), "OrinTech-%s", chip_id_hex);

  Serial.println("Configuration portal: Starting");
  Serial.printf("Connect to: %s\n", ap_ssid);
  Serial.println("Device will restart in 3 seconds...");

  // Blink LED to indicate reset
  for (int i = 0; i < 6; i++)
  {
    rgbLedWrite(RGBLedPin, 255, 0, 0);
    delay(250);
    rgbLedWrite(RGBLedPin, 0, 0, 0);
    delay(250);
  }

  // Restart ESP32 to enter configuration mode
  ESP.restart();
}
