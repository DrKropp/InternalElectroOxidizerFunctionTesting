/*
 * Data Logging Module Header
 * OrinTech ElectroOxidizer Device
 *
 * This module handles:
 * - Daily CSV log file creation
 * - 5-minute interval data logging
 * - 7-day log rotation and cleanup
 * - Log compression and download
 */

#ifndef LOGGING_H
#define LOGGING_H

#include <Arduino.h>
#include <LittleFS.h>
#include <time.h>

// Initialize logging system (create directory, setup time)
bool initLogging();

// Log current data to today's log file
bool logData(float avgPosCurrent, float avgNegCurrent,
             float peakPosCurrent, float peakNegCurrent,
             float avgPosVoltage, float avgNegVoltage,
             float peakPosVoltage, float peakNegVoltage,
             int forwardTime, int reverseTime);

// Get today's log filename (format: /logs/log_YYYYMMDD.csv)
String getTodayLogFilename();

// Clean up log files older than LOG_RETENTION_DAYS
void cleanupOldLogs();

// Get list of all available log files
String listLogFiles();

// Create compressed archive of all log files
bool createLogArchive(const char* archivePath);

// Get total size of all log files
size_t getLogsTotalSize();

// Check if logs directory exists and create if needed
bool ensureLogDirectory();

#endif // LOGGING_H
