/*
 * Data Logging Module Implementation
 * OrinTech ElectroOxidizer Device
 */

#include "logging.h"
#include "config.h"
#include <sys/stat.h>
#include <dirent.h>

// ============================================================================
// INITIALIZATION
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

  // Clean up old log files on startup
  cleanupOldLogs();

  Serial.println("Logging system initialized successfully");
  Serial.printf("Log directory: %s\n", LOG_DIR);
  Serial.printf("Retention period: %d days\n", LOG_RETENTION_DAYS);
  Serial.printf("Log interval: %lu seconds\n", logInterval / 1000);

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

// ============================================================================
// LOG FILE OPERATIONS
// ============================================================================

String getTodayLogFilename()
{
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);

  char filename[32];
  snprintf(filename, sizeof(filename), "%s%04d%02d%02d.csv",
           LOG_FILE_PREFIX,
           timeinfo.tm_year + 1900,
           timeinfo.tm_mon + 1,
           timeinfo.tm_mday);

  return String(filename);
}

bool logData(float avgPosCurrent, float avgNegCurrent,
             float peakPosCurrent, float peakNegCurrent,
             float avgPosVoltage, float avgNegVoltage,
             float peakPosVoltage, float peakNegVoltage,
             int forwardTime, int reverseTime)
{
  String filename = getTodayLogFilename();
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

  // Get current timestamp
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);

  char timestamp[32];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &timeinfo);

  // Write data row
  logFile.printf("%s,%.3f,%.3f,%.3f,%.3f,%.2f,%.2f,%.2f,%.2f,%d,%d\n",
                 timestamp,
                 avgPosCurrent, avgNegCurrent,
                 peakPosCurrent, peakNegCurrent,
                 avgPosVoltage, avgNegVoltage,
                 peakPosVoltage, peakNegVoltage,
                 forwardTime, reverseTime);

  logFile.close();

  Serial.printf("Logged data to %s at %s\n", filename.c_str(), timestamp);
  return true;
}

// ============================================================================
// LOG MANAGEMENT
// ============================================================================

void cleanupOldLogs()
{
  Serial.println("Checking for old log files...");

  time_t now;
  time(&now);
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

    // Check if it's a log file (log_YYYYMMDD.csv)
    if (filename.startsWith("log_") && filename.endsWith(".csv"))
    {
      // Extract date from filename (log_YYYYMMDD.csv)
      if (filename.length() >= 17)
      {
        String dateStr = filename.substring(4, 12); // YYYYMMDD
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
        if (fileTimestamp < cutoffTime)
        {
          String fullPath = String(LOG_DIR) + "/" + filename;
          Serial.printf("Deleting old log file: %s\n", fullPath.c_str());
          LittleFS.remove(fullPath);
          deletedCount++;
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

    if (filename.startsWith("log_") && filename.endsWith(".csv"))
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

    if (filename.startsWith("log_") && filename.endsWith(".csv"))
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
  // Note: ESP32 LittleFS doesn't have built-in zip compression
  // We'll create a simple concatenated file with headers
  // For true ZIP support, would need to add a ZIP library

  File archive = LittleFS.open(archivePath, "w");
  if (!archive)
  {
    Serial.printf("ERROR: Failed to create archive: %s\n", archivePath);
    return false;
  }

  // Write archive header
  archive.println("=== OrinTech ElectroOxidizer Data Logs Archive ===");
  archive.printf("Generated: %lu\n", millis());
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

    if (filename.startsWith("log_") && filename.endsWith(".csv"))
    {
      archive.printf("\n=== File: %s ===\n", filename.c_str());

      // Copy file contents
      while (file.available())
      {
        archive.write(file.read());
      }

      archive.println("\n");
      fileCount++;
    }

    file = dir.openNextFile();
  }
  dir.close();
  archive.close();

  Serial.printf("Created archive with %d log files: %s\n", fileCount, archivePath);
  return true;
}
