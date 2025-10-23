/*
 * Multi-Network WiFi Management
 * OrinTech ElectroOxidizer Device
 *
 * Manages multiple WiFi network credentials with priority-based connection logic.
 * Allows device to remember and automatically connect to up to 5 different networks.
 */

#ifndef MULTI_NETWORK_H
#define MULTI_NETWORK_H

#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "config.h"

// ============================================================================
// EXTERNAL VARIABLES
// ============================================================================

extern WiFiCredential savedNetworks[MAX_WIFI_NETWORKS];

// ============================================================================
// FUNCTION DECLARATIONS
// ============================================================================

/**
 * Initialize multi-network storage (clear all slots)
 */
void initMultiNetworkStorage();

/**
 * Load saved networks from LittleFS
 * @return true if networks were loaded successfully
 */
bool loadSavedNetworks();

/**
 * Save all networks to LittleFS
 * @return true if save was successful
 */
bool saveSavedNetworks();

/**
 * Add a new network or update existing one
 * @param ssid Network SSID
 * @param password Network password
 * @return true if operation was successful
 */
bool addOrUpdateNetwork(const char* ssid, const char* password);

/**
 * Remove a network from storage
 * @param ssid Network SSID to remove
 * @return true if network was found and removed
 */
bool removeNetwork(const char* ssid);

/**
 * Find index of network by SSID
 * @param ssid Network SSID to find
 * @return index (0-4) or -1 if not found
 */
int8_t findNetworkIndex(const char* ssid);

/**
 * Sort networks by priority (highest first)
 */
void sortNetworksByPriority();

/**
 * Attempt to connect to a specific network
 * @param network Pointer to network credentials
 * @return true if connection successful
 */
bool connectToNetwork(WiFiCredential* network);

/**
 * Try to connect to saved networks in priority order
 * @return true if connected to any network
 */
bool connectToSavedNetworks();

/**
 * Get formatted list of saved networks
 * @return String with network list
 */
String listSavedNetworks();

#endif // MULTI_NETWORK_H
