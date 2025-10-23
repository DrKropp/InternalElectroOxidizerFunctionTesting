/*
 * Button Multi-Reset Handler
 * OrinTech ElectroOxidizer Device
 *
 * Handles button press detection and multi-reset functionality.
 * Allows WiFi credential reset via 3 fast button presses within 5 seconds.
 */

#ifndef BUTTON_HANDLER_H
#define BUTTON_HANDLER_H

#include <Arduino.h>
#include "config.h"

// ============================================================================
// EXTERNAL VARIABLES
// ============================================================================

extern unsigned long buttonPressTimestamps[BUTTON_RESET_COUNT];
extern uint8_t buttonPressIndex;
extern bool lastButtonState;
extern unsigned long lastDebounceTime;
extern char ap_ssid[64];
extern char chip_id_hex[9];
extern char hostname[64];
extern char deviceName[64];

// ============================================================================
// FUNCTION DECLARATIONS
// ============================================================================

/**
 * Initialize button press timestamp array
 */
void initButtonHandler();

/**
 * Check for button press and multi-reset condition
 * Called every loop iteration
 */
void checkButtonMultiReset();

/**
 * Detect if multi-reset condition is met
 * @return true if 3 presses occurred within time window
 */
bool detectButtonMultiReset();

/**
 * Trigger WiFi credential reset and restart device
 */
void triggerWiFiReset();

#endif // BUTTON_HANDLER_H
