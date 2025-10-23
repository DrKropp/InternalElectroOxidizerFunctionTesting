/*
 * Configuration and Constants Header
 * OrinTech ElectroOxidizer Device
 *
 * This file contains all configuration constants, pin definitions,
 * and data structures used throughout the application.
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============================================================================
// GPIO PIN DEFINITIONS
// ============================================================================

// DRV8706H-Q1 H-Bridge Pins
const uint8_t VoltControl_PWM_Pin = 8;      // PWM output to control 24V supply voltage
const uint8_t outputEnablePin = 4;          // H-Bridge enable (In1/EN)
const uint8_t nHiZ1Pin = 5;                 // Not used in mode 2
const uint8_t outputDirectionPin = 6;       // H-Bridge direction control (In2/PH)
const uint8_t nHiZ2Pin = 7;                 // Not used in mode 2
const uint8_t nSleepPin = 15;               // Sleep mode control (HIGH=wake, LOW=sleep)
const uint8_t DRVOffPin = 16;               // Disable DRV output (HIGH=disable)
const uint8_t nFaultPin = 17;               // Fault indicator (pulled LOW on fault)

// Other GPIO Pins
const uint8_t testButton = 1;               // Test button (active LOW)
const uint8_t RGBLedPin = 48;               // Built-in RGB LED
const int ADC_PIN = 2;                      // Current sense ADC input

// ============================================================================
// PWM CONFIGURATION
// ============================================================================

const uint8_t outputBits = 10;              // 10-bit PWM resolution (0-1023)
const uint16_t PWMFreq = 25000;             // 25 kHz PWM frequency
const float TargetVoltsConversionFactor = 0.0301686059427937; // Calibrated 16-Jan-2025

// ============================================================================
// ADC CONFIGURATION
// ============================================================================

const int SAMPLE_RATE = 20000;              // 20 kHz sampling rate
const unsigned long WINDOW_US = 40000;      // 40ms sampling window
const int MAX_SAMPLES_NEW = 1000;           // Max samples per window
const int BUFFER_SIZE = MAX_SAMPLES_NEW * 4;
const uint8_t MAX_SAMPLES = 100;            // Samples for averaging

// ADC Calibration Constants (from calibration 7/5/25)
const float ADC_INTERCEPT = -39.3900104981669f;
const float ADC_SLOPE = 0.0192397497221598f;

// ============================================================================
// TIMING CONSTANTS
// ============================================================================

const unsigned long notifyInterval = 500;        // WebSocket update interval (ms) - reduced from 100ms to prevent queue overflow
const unsigned long reconnectInterval = 10000;   // WiFi reconnect interval (ms)
const unsigned long logInterval = 300000;        // Data logging interval (ms) - 5 minutes

// ============================================================================
// LOGGING CONSTANTS
// ============================================================================

const uint8_t LOG_RETENTION_DAYS = 7;            // Keep logs for 7 days
const char* LOG_DIR = "/logs";                   // Log directory path
const char* LOG_FILE_PREFIX = "/logs/log_";      // Log file prefix (will be log_YYYYMMDD.csv)
const char* DAY_COUNTER_FILE = "/day_counter.txt"; // Day counter persistence file

// ============================================================================
// NTP TIME SYNCHRONIZATION CONSTANTS
// ============================================================================

const char* NTP_SERVER1 = "pool.ntp.org";        // Primary NTP server
const char* NTP_SERVER2 = "time.nist.gov";       // Secondary NTP server
const long GMT_OFFSET_SEC = 0;                   // UTC timezone (0 offset)
const int DAYLIGHT_OFFSET_SEC = 0;               // No daylight saving
const unsigned long NTP_SYNC_INTERVAL = 3600000; // Sync every 1 hour (ms)
const unsigned long NTP_TIMEOUT_MS = 5000;       // 5 second timeout for NTP sync
const unsigned long LOG_ROLLOVER_24H = 86400000UL; // 24 hours in milliseconds

// ============================================================================
// BUTTON MULTI-RESET DETECTION CONSTANTS
// ============================================================================

const unsigned long BUTTON_DEBOUNCE_MS = 50;     // Debounce delay (ms)
const unsigned long BUTTON_RESET_WINDOW_MS = 5000; // Time window for button presses (ms)
const uint8_t BUTTON_RESET_COUNT = 3;            // Number of button presses to trigger reset

// ============================================================================
// MULTI-NETWORK WIFI CONSTANTS
// ============================================================================

const uint8_t MAX_WIFI_NETWORKS = 5;             // Maximum stored networks
const unsigned long NETWORK_CONNECT_TIMEOUT = 15000; // 15 seconds per network attempt

// ============================================================================
// DATA STRUCTURES
// ============================================================================

// Structure to store WiFi network credentials
struct WiFiCredential {
  char ssid[33];
  char password[64];
  uint8_t priority;
  unsigned long lastConnected;
  bool isValid;
};

#endif // CONFIG_H
