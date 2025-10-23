/*
 * Multi-Network WiFi Management Implementation
 * OrinTech ElectroOxidizer Device
 */

#include "multi_network.h"

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================

WiFiCredential savedNetworks[MAX_WIFI_NETWORKS];

// ============================================================================
// FUNCTION IMPLEMENTATIONS
// ============================================================================

void initMultiNetworkStorage()
{
  // Initialize all network slots as invalid
  for (uint8_t i = 0; i < MAX_WIFI_NETWORKS; i++)
  {
    savedNetworks[i].isValid = false;
    savedNetworks[i].ssid[0] = '\0';
    savedNetworks[i].password[0] = '\0';
    savedNetworks[i].priority = 0;
    savedNetworks[i].lastConnected = 0;
  }
}

bool loadSavedNetworks()
{
  if (!LittleFS.exists("/networks.json"))
  {
    Serial.println("No saved networks found");
    return false;
  }

  File file = LittleFS.open("/networks.json", "r");
  if (!file)
  {
    Serial.println("Error: Failed to open networks file");
    return false;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error)
  {
    Serial.println("Error: Failed to parse networks file");
    return false;
  }

  JsonArray networks = doc["networks"].as<JsonArray>();
  uint8_t count = 0;

  for (JsonObject network : networks)
  {
    if (count >= MAX_WIFI_NETWORKS) break;

    const char* ssid = network["ssid"];
    const char* password = network["password"];
    uint8_t priority = network["priority"] | 0;
    unsigned long lastConnected = network["lastConnected"] | 0;

    if (ssid && strlen(ssid) > 0)
    {
      strncpy(savedNetworks[count].ssid, ssid, 32);
      savedNetworks[count].ssid[32] = '\0';

      if (password)
      {
        strncpy(savedNetworks[count].password, password, 63);
        savedNetworks[count].password[63] = '\0';
      }
      else
      {
        savedNetworks[count].password[0] = '\0';
      }

      savedNetworks[count].priority = priority;
      savedNetworks[count].lastConnected = lastConnected;
      savedNetworks[count].isValid = true;
      count++;
    }
  }

  Serial.printf("Loaded %d saved network(s)\n", count);
  return count > 0;
}

bool saveSavedNetworks()
{
  JsonDocument doc;
  JsonArray networks = doc["networks"].to<JsonArray>();

  for (uint8_t i = 0; i < MAX_WIFI_NETWORKS; i++)
  {
    if (savedNetworks[i].isValid)
    {
      JsonObject network = networks.add<JsonObject>();
      network["ssid"] = savedNetworks[i].ssid;
      network["password"] = savedNetworks[i].password;
      network["priority"] = savedNetworks[i].priority;
      network["lastConnected"] = savedNetworks[i].lastConnected;
    }
  }

  File file = LittleFS.open("/networks.json", "w");
  if (!file)
  {
    Serial.println("Error: Failed to create networks file");
    return false;
  }

  bool success = serializeJson(doc, file) > 0;
  file.close();

  if (success)
  {
    Serial.println("Networks saved successfully");
  }

  return success;
}

int8_t findNetworkIndex(const char* ssid)
{
  for (uint8_t i = 0; i < MAX_WIFI_NETWORKS; i++)
  {
    if (savedNetworks[i].isValid && strcmp(savedNetworks[i].ssid, ssid) == 0)
    {
      return i;
    }
  }
  return -1;
}

bool addOrUpdateNetwork(const char* ssid, const char* password)
{
  if (!ssid || strlen(ssid) == 0)
  {
    Serial.println("Error: Invalid SSID");
    return false;
  }

  // Check if network already exists
  int8_t existingIndex = findNetworkIndex(ssid);

  if (existingIndex >= 0)
  {
    // Update existing network
    Serial.printf("Updating network: %s\n", ssid);
    if (password && strlen(password) > 0)
    {
      strncpy(savedNetworks[existingIndex].password, password, 63);
      savedNetworks[existingIndex].password[63] = '\0';
    }
    savedNetworks[existingIndex].lastConnected = millis();
    savedNetworks[existingIndex].priority++;
    return saveSavedNetworks();
  }

  // Find empty slot
  int8_t emptySlot = -1;
  for (uint8_t i = 0; i < MAX_WIFI_NETWORKS; i++)
  {
    if (!savedNetworks[i].isValid)
    {
      emptySlot = i;
      break;
    }
  }

  if (emptySlot < 0)
  {
    // No empty slots, remove lowest priority network
    sortNetworksByPriority();
    emptySlot = MAX_WIFI_NETWORKS - 1;
    Serial.printf("Network list full, removing: %s\n", savedNetworks[emptySlot].ssid);
  }

  // Add new network
  Serial.printf("Adding network: %s\n", ssid);
  strncpy(savedNetworks[emptySlot].ssid, ssid, 32);
  savedNetworks[emptySlot].ssid[32] = '\0';

  if (password && strlen(password) > 0)
  {
    strncpy(savedNetworks[emptySlot].password, password, 63);
    savedNetworks[emptySlot].password[63] = '\0';
  }
  else
  {
    savedNetworks[emptySlot].password[0] = '\0';
  }

  savedNetworks[emptySlot].priority = 1;
  savedNetworks[emptySlot].lastConnected = millis();
  savedNetworks[emptySlot].isValid = true;

  return saveSavedNetworks();
}

bool removeNetwork(const char* ssid)
{
  int8_t index = findNetworkIndex(ssid);
  if (index < 0)
  {
    Serial.printf("Network not found: %s\n", ssid);
    return false;
  }

  Serial.printf("Removing network: %s\n", ssid);
  savedNetworks[index].isValid = false;
  savedNetworks[index].ssid[0] = '\0';
  savedNetworks[index].password[0] = '\0';

  return saveSavedNetworks();
}

void sortNetworksByPriority()
{
  // Simple bubble sort by priority (descending) and lastConnected (descending)
  for (uint8_t i = 0; i < MAX_WIFI_NETWORKS - 1; i++)
  {
    for (uint8_t j = 0; j < MAX_WIFI_NETWORKS - i - 1; j++)
    {
      bool shouldSwap = false;

      if (savedNetworks[j].isValid && savedNetworks[j + 1].isValid)
      {
        if (savedNetworks[j].priority < savedNetworks[j + 1].priority)
        {
          shouldSwap = true;
        }
        else if (savedNetworks[j].priority == savedNetworks[j + 1].priority &&
                 savedNetworks[j].lastConnected < savedNetworks[j + 1].lastConnected)
        {
          shouldSwap = true;
        }
      }
      else if (!savedNetworks[j].isValid && savedNetworks[j + 1].isValid)
      {
        shouldSwap = true;
      }

      if (shouldSwap)
      {
        WiFiCredential temp = savedNetworks[j];
        savedNetworks[j] = savedNetworks[j + 1];
        savedNetworks[j + 1] = temp;
      }
    }
  }
}

bool connectToNetwork(WiFiCredential* network)
{
  if (!network || !network->isValid)
  {
    return false;
  }

  Serial.printf("Attempting to connect to: %s\n", network->ssid);

  WiFi.disconnect();
  delay(100);

  WiFi.begin(network->ssid, network->password);

  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < NETWORK_CONNECT_TIMEOUT)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.printf("Connected to: %s\n", network->ssid);
    network->lastConnected = millis();
    network->priority++;
    saveSavedNetworks();
    return true;
  }

  Serial.printf("Failed to connect to: %s\n", network->ssid);
  return false;
}

bool connectToSavedNetworks()
{
  Serial.println("\n=== Attempting Multi-Network Connection ===");

  // Sort networks by priority
  sortNetworksByPriority();

  // Try each network in priority order
  for (uint8_t i = 0; i < MAX_WIFI_NETWORKS; i++)
  {
    if (!savedNetworks[i].isValid)
    {
      continue;
    }

    if (connectToNetwork(&savedNetworks[i]))
    {
      Serial.println("=== Multi-Network Connection Successful ===\n");
      return true;
    }
  }

  Serial.println("=== All Saved Networks Failed ===\n");
  return false;
}

String listSavedNetworks()
{
  sortNetworksByPriority();

  String list = "Saved Networks:\n";
  uint8_t count = 0;

  for (uint8_t i = 0; i < MAX_WIFI_NETWORKS; i++)
  {
    if (savedNetworks[i].isValid)
    {
      count++;
      list += String(count) + ". " + String(savedNetworks[i].ssid);
      list += " (Priority: " + String(savedNetworks[i].priority) + ")\n";
    }
  }

  if (count == 0)
  {
    list = "No saved networks\n";
  }

  return list;
}
