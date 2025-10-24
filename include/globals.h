/*
 * Global Variables and Objects
 * OrinTech ElectroOxidizer Device
 *
 * Centralized declaration of all global variables used across modules
 */

#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include <WiFiManager.h>
#include <ESPAsyncWebServer.h>
#include <ESP_MultiResetDetector.h>
#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_cali.h"
#include "config.h"

// ============================================================================
// GLOBAL OBJECTS
// ============================================================================

extern WiFiManager wifiManager;
extern WiFiManagerParameter custom_device_name;
extern AsyncWebServer server;
extern AsyncWebSocket ws;
extern MultiResetDetector* mrd;

// ADC Handles
extern adc_continuous_handle_t adc_handle;
extern adc_cali_handle_t adc_cali_handle;
extern bool adc_calibrated;

// ============================================================================
// CONFIGURATION VARIABLES
// ============================================================================

extern char hostname[64];
extern char deviceName[64];
extern char ap_ssid[64];
extern char chip_id_hex[9];

// Control Parameters (persisted to LittleFS)
extern String FValue1;          // Target output voltage (Volts)
extern String FValue2;          // Forward polarity time (ms)
extern String RValue2;          // Reverse polarity time (ms)
extern uint16_t ForwardTimeInt; // Forward time in milliseconds
extern uint16_t ReverseTimeInt; // Reverse time in milliseconds

// ============================================================================
// RUNTIME STATE VARIABLES
// ============================================================================

extern bool isRunning;
extern bool outputDirection;    // false=reverse, true=forward

// Current and Voltage Measurements
extern float peakPositiveCurrent;
extern float peakNegativeCurrent;
extern float averagePositiveCurrent;
extern float averageNegativeCurrent;
extern float peakPositiveVoltage;
extern float peakNegativeVoltage;
extern float averagePositiveVoltage;
extern float averageNegativeVoltage;
extern float latestCurrent;
extern float latestRaw;

// ADC Accumulation
extern float positive_adc_sum;
extern float negative_adc_sum;
extern uint32_t positive_adc_count;
extern uint32_t negative_adc_count;

// Timing Variables
extern uint32_t reversestartTime;
extern unsigned long lastNotifyTime;
extern unsigned long lastReconnectAttempt;
extern unsigned long currentReconnectInterval;
extern unsigned long lastLogTime;
extern unsigned long lastTimeSyncAttempt;
extern unsigned long currentLogStartTime;

// Time Synchronization
extern bool timeIsSynced;
extern uint16_t currentDayNumber;
extern String currentLogFilename;

// Peak Reset Management
extern bool hasResetPeakCurrent;

// ADC Buffers (allocated on heap to prevent stack overflow)
extern uint8_t* adc_buffer;

#endif // GLOBALS_H
