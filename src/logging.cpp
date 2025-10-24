/*
 * Data Logging Module Implementation
 * OrinTech ElectroOxidizer Device
 *
 * Implements NTP time synchronization with offline fallback to day-based logging
 */

#include "logging.h"
#include "config.h"
#include "globals.h"
#include <WiFi.h>
#include <sys/stat.h>
#include <dirent.h>

// ============================================================================
// TIME SYNCHRONIZATION FUNCTIONS
// ============================================================================

bool syncTimeWithNTP()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("NTP: WiFi not connected, skipping sync");
    return false;
  }

  Serial.println("\n=== Syncing Time with NTP ===");
  Serial.printf("NTP Servers: %s, %s\n", NTP_SERVER1, NTP_SERVER2);

  // Configure NTP with UTC timezone
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER1, NTP_SERVER2);

  // Wait for time sync (with timeout)
  unsigned long startAttempt = millis();
  while (millis() - startAttempt < NTP_TIMEOUT_MS)
  {
    time_t now = time(nullptr);
    if (now > 1000000000) // Reasonable timestamp (after year 2001)
    {
      struct tm timeinfo;
      localtime_r(&now, &timeinfo);

      char timeStr[64];
      strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S UTC", &timeinfo);

      Serial.printf("NTP sync successful: %s\n", timeStr);
      Serial.println("=== NTP Sync Complete ===\n");

      timeIsSynced = true;
      lastTimeSyncAttempt = millis();
      return true;
    }
    delay(100);
  }

  Serial.println("NTP sync failed: Timeout");
  Serial.println("=== NTP Sync Failed ===\n");
  timeIsSynced = false;
  lastTimeSyncAttempt = millis();
  return false;
}

bool isTimeValid()
{
  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  // Check if year is reasonable (2020 or later)
  int year = timeinfo.tm_year + 1900;
  return (year >= 2020);
}

String getTimestampString()
{
  if (isTimeValid())
  {
    // Use actual date/time
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String(timestamp);
  }
  else
  {
    // Use uptime format
    unsigned long totalSeconds = millis() / 1000;
    unsigned long days = totalSeconds / 86400;
    unsigned long hours = (totalSeconds % 86400) / 3600;
    unsigned long minutes = (totalSeconds % 3600) / 60;
    unsigned long seconds = totalSeconds % 60;

    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "Day %lu - %02lu:%02lu:%02lu",
             days + 1, hours, minutes, seconds);
    return String(timestamp);
  }
}

// ============================================================================
// DAY COUNTER FUNCTIONS (OFFLINE MODE)
// ============================================================================

uint16_t loadDayCounter()
{
  if (LittleFS.exists(DAY_COUNTER_FILE))
  {
    File file = LittleFS.open(DAY_COUNTER_FILE, "r");
    if (file)
    {
      String content = file.readString();
      file.close();
      uint16_t day = content.toInt();
      Serial.printf("Loaded day counter: %d\n", day);
      return day > 0 ? day : 1;
    }
  }

  Serial.println("Day counter file not found, starting at day 1");
  return 1;
}

void saveDayCounter(uint16_t day)
{
  File file = LittleFS.open(DAY_COUNTER_FILE, "w");
  if (file)
  {
    file.print(day);
    file.close();
    Serial.printf("Saved day counter: %d\n", day);
  }
  else
  {
    Serial.println("ERROR: Failed to save day counter");
  }
}

void incrementDayCounter()
{
  currentDayNumber++;
  saveDayCounter(currentDayNumber);
  Serial.printf("Day counter incremented to: %d\n", currentDayNumber);
}

// ============================================================================
// LOG FILE MANAGEMENT
// ============================================================================

bool initLogging()
{
  Serial.println("\n=== Initializing Logging System ===");

  // Ensure log directory exists
  if (!ensureLogDirectory())
  {
    Serial.println("ERROR: Failed to create log directory");
    return false;
  }

  // Load day counter for offline mode
  currentDayNumber = loadDayCounter();

  // Clean up old log files on startup
  cleanupOldLogs();

  // Generate initial log filename
  currentLogFilename = getLogFilename();
  currentLogStartTime = millis();

  Serial.println("Logging system initialized successfully");
  Serial.printf("Log mode: %s\n", isTimeValid() ? "DATE-BASED (Online)" : "DAY-BASED (Offline)");
  Serial.printf("Current log file: %s\n", currentLogFilename.c_str());
  Serial.printf("Log interval: %lu seconds\n", logInterval / 1000);
  Serial.printf("Retention: %d days (date-based logs only)\n", LOG_RETENTION_DAYS);
  Serial.println("=== Logging Initialization Complete ===\n");

  return true;
}

bool ensureLogDirectory()
{
  if (!LittleFS.exists(LOG_DIR))
  {
    Serial.printf("Creating log directory: %s\n", LOG_DIR);
    if (!LittleFS.mkdir(LOG_DIR))
    {
      Serial.println("ERROR: Failed to create log directory");
      return false;
    }
  }
  return true;
}

String getLogFilename()
{
  if (isTimeValid())
  {
    // Use date-based naming (online mode)
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    char filename[32];
    snprintf(filename, sizeof(filename), "%s%04d%02d%02d.csv",
             LOG_FILE_PREFIX,
             timeinfo.tm_year + 1900,
             timeinfo.tm_mon + 1,
             timeinfo.tm_mday);

    return String(filename);
  }
  else
  {
    // Use day-based naming (offline mode)
    char filename[32];
    snprintf(filename, sizeof(filename), "%sday%03d.csv",
             LOG_FILE_PREFIX, currentDayNumber);

    return String(filename);
  }
}

bool shouldRolloverLog()
{
  String expectedFilename = getLogFilename();

  // Check if filename changed (midnight transition when online)
  if (expectedFilename != currentLogFilename)
  {
    Serial.printf("Log rollover triggered: filename changed from %s to %s\n",
                  currentLogFilename.c_str(), expectedFilename.c_str());
    return true;
  }

  // Check 24-hour rollover for offline mode
  if (!isTimeValid())
  {
    unsigned long elapsed = millis() - currentLogStartTime;
    if (elapsed >= LOG_ROLLOVER_24H)
    {
      Serial.printf("Log rollover triggered: 24 hours elapsed (offline mode)\n");
      return true;
    }
  }

  return false;
}

void handleLogRollover()
{
  Serial.println("\n=== Log File Rollover ===");

  // Increment day counter if in offline mode
  if (!isTimeValid())
  {
    incrementDayCounter();
  }

  // Update current log filename and start time
  currentLogFilename = getLogFilename();
  currentLogStartTime = millis();

  Serial.printf("New log file: %s\n", currentLogFilename.c_str());
  Serial.println("=== Rollover Complete ===\n");
}

bool logData(float avgPosCurrent, float avgNegCurrent,
             float peakPosCurrent, float peakNegCurrent,
             float avgPosVoltage, float avgNegVoltage,
             float peakPosVoltage, float peakNegVoltage,
             int forwardTime, int reverseTime)
{
  // Check filesystem space before logging
  size_t totalBytes = LittleFS.totalBytes();
  size_t usedBytes = LittleFS.usedBytes();
  float usagePercent = (float)usedBytes / (float)totalBytes * 100.0;

  if (usagePercent >= 95.0)
  {
    Serial.printf("WARNING: Filesystem %d%% full (%d/%d bytes), cleaning up old logs...\n",
                  (int)usagePercent, usedBytes, totalBytes);
    cleanupOldLogs();

    // Re-check after cleanup
    usedBytes = LittleFS.usedBytes();
    usagePercent = (float)usedBytes / (float)totalBytes * 100.0;

    if (usagePercent >= 98.0)
    {
      Serial.printf("ERROR: Filesystem critically full (%d%%), cannot log data\n", (int)usagePercent);
      return false;
    }
  }

  String filename = currentLogFilename;
  bool isNewFile = !LittleFS.exists(filename);

  File logFile = LittleFS.open(filename, "a");
  if (!logFile)
  {
    Serial.printf("ERROR: Failed to open log file: %s\n", filename.c_str());
    return false;
  }

  // Write CSV header if new file
  if (isNewFile)
  {
    logFile.println("Timestamp,Avg_Pos_Current_A,Avg_Neg_Current_A,Peak_Pos_Current_A,Peak_Neg_Current_A,Avg_Pos_Voltage_V,Avg_Neg_Voltage_V,Peak_Pos_Voltage_V,Peak_Neg_Voltage_V,Forward_Time_ms,Reverse_Time_ms");
    Serial.printf("Created new log file: %s\n", filename.c_str());
  }

  // Get timestamp string
  String timestamp = getTimestampString();

  // Write data row
  logFile.printf("%s,%.3f,%.3f,%.3f,%.3f,%.2f,%.2f,%.2f,%.2f,%d,%d\n",
                 timestamp.c_str(),
                 avgPosCurrent, avgNegCurrent,
                 peakPosCurrent, peakNegCurrent,
                 avgPosVoltage, avgNegVoltage,
                 peakPosVoltage, peakNegVoltage,
                 forwardTime, reverseTime);

  logFile.close();

  Serial.printf("Logged data to %s at %s\n", filename.c_str(), timestamp.c_str());
  return true;
}

// ============================================================================
// LOG CLEANUP AND MANAGEMENT
// ============================================================================

void cleanupOldLogs()
{
  Serial.println("Checking for old log files...");

  if (!isTimeValid())
  {
    Serial.println("Time not valid, skipping cleanup (can't determine log age)");
    return;
  }

  time_t now = time(nullptr);
  time_t cutoffTime = now - (LOG_RETENTION_DAYS * 24 * 60 * 60);

  File dir = LittleFS.open(LOG_DIR);
  if (!dir || !dir.isDirectory())
  {
    Serial.println("ERROR: Cannot open log directory");
    return;
  }

  int deletedCount = 0;
  File file = dir.openNextFile();
  while (file)
  {
    String filename = String(file.name());

    // Only process date-based log files (log_YYYYMMDD.csv)
    // Skip day-based logs (log_dayXXX.csv) - can't determine age
    if (filename.startsWith("log_") && !filename.startsWith("log_day") && filename.endsWith(".csv"))
    {
      // Extract date from filename (log_YYYYMMDD.csv)
      if (filename.length() >= 17)
      {
        int underscorePos = filename.indexOf('_');
        int dotPos = filename.indexOf('.');

        if (underscorePos >= 0 && dotPos >= 0)
        {
          String dateStr = filename.substring(underscorePos + 1, dotPos);

          if (dateStr.length() == 8) // YYYYMMDD
          {
            int year = dateStr.substring(0, 4).toInt();
            int month = dateStr.substring(4, 6).toInt();
            int day = dateStr.substring(6, 8).toInt();

            // Create tm struct for file date
            struct tm fileTime = {0};
            fileTime.tm_year = year - 1900;
            fileTime.tm_mon = month - 1;
            fileTime.tm_mday = day;
            fileTime.tm_hour = 0;
            fileTime.tm_min = 0;
            fileTime.tm_sec = 0;

            time_t fileTimestamp = mktime(&fileTime);

            // Delete if older than retention period
            if (fileTimestamp < cutoffTime && fileTimestamp > 0)
            {
              String fullPath = String(LOG_DIR) + "/" + filename;
              Serial.printf("Deleting old log file: %s\n", fullPath.c_str());
              LittleFS.remove(fullPath);
              deletedCount++;
            }
          }
        }
      }
    }

    file = dir.openNextFile();
  }
  dir.close();

  if (deletedCount > 0)
  {
    Serial.printf("Deleted %d old log file(s)\n", deletedCount);
  }
  else
  {
    Serial.println("No old log files to delete");
  }
}

String listLogFiles()
{
  String fileList = "";

  File dir = LittleFS.open(LOG_DIR);
  if (!dir || !dir.isDirectory())
  {
    return fileList;
  }

  File file = dir.openNextFile();
  while (file)
  {
    String filename = String(file.name());

    if ((filename.startsWith("log_") || filename.startsWith("log_day")) && filename.endsWith(".csv"))
    {
      if (fileList.length() > 0) fileList += ",";
      fileList += filename;
    }

    file = dir.openNextFile();
  }
  dir.close();

  return fileList;
}

size_t getLogsTotalSize()
{
  size_t totalSize = 0;

  File dir = LittleFS.open(LOG_DIR);
  if (!dir || !dir.isDirectory())
  {
    return 0;
  }

  File file = dir.openNextFile();
  while (file)
  {
    String filename = String(file.name());

    if ((filename.startsWith("log_") || filename.startsWith("log_day")) && filename.endsWith(".csv"))
    {
      totalSize += file.size();
    }

    file = dir.openNextFile();
  }
  dir.close();

  return totalSize;
}

// ============================================================================
// LOG ARCHIVING
// ============================================================================

bool createLogArchive(const char* archivePath)
{
  // Delete old archive if it exists
  if (LittleFS.exists(archivePath))
  {
    LittleFS.remove(archivePath);
  }

  File archive = LittleFS.open(archivePath, "w");
  if (!archive)
  {
    Serial.printf("ERROR: Failed to create archive: %s\n", archivePath);
    return false;
  }

  // Write archive header
  archive.println("=== OrinTech ElectroOxidizer Data Logs Archive ===");
  archive.printf("Generated: %s\n", getTimestampString().c_str());
  archive.printf("Log mode: %s\n", isTimeValid() ? "Date-based (Online)" : "Day-based (Offline)");
  archive.println("===============================================\n");

  File dir = LittleFS.open(LOG_DIR);
  if (!dir || !dir.isDirectory())
  {
    Serial.println("ERROR: Cannot open log directory");
    archive.close();
    return false;
  }

  int fileCount = 0;
  File file = dir.openNextFile();
  while (file)
  {
    String filename = String(file.name());

    if ((filename.startsWith("log_") || filename.startsWith("log_day")) && filename.endsWith(".csv"))
    {
      archive.printf("\n=== File: %s (%d bytes) ===\n", filename.c_str(), file.size());

      // Copy file contents with error checking
      while (file.available())
      {
        uint8_t byte = file.read();
        size_t written = archive.write(byte);

        // Check for write errors (disk full, corruption, etc.)
        if (written == 0)
        {
          Serial.println("ERROR: Archive write failed - disk may be full");
          file.close();
          dir.close();
          archive.close();
          LittleFS.remove(archivePath);  // Clean up incomplete archive
          return false;
        }
      }

      archive.println("\n");
      fileCount++;
    }

    file.close();  // Ensure file handle is closed
    file = dir.openNextFile();
  }
  dir.close();
  archive.close();

  Serial.printf("Created archive with %d log files: %s\n", fileCount, archivePath);
  return true;
}
