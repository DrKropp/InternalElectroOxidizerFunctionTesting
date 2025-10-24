/*
 * Data Logging Module Header
 * OrinTech ElectroOxidizer Device
 *
 * This module handles:
 * - NTP time synchronization with offline fallback
 * - Daily CSV log file creation (date-based when online)
 * - Day-numbered logs when offline (24-hour rollover)
 * - 5-minute interval data logging
 * - 7-day log rotation and cleanup
 * - Log compression and download
 */

#ifndef LOGGING_H
#define LOGGING_H

#include <Arduino.h>
#include <LittleFS.h>
#include <time.h>

// ============================================================================
// TIME SYNCHRONIZATION FUNCTIONS
// ============================================================================

// Synchronize time with NTP servers
bool syncTimeWithNTP();

// Check if time is valid (synced and not stuck in 1970)
bool isTimeValid();

// Get formatted timestamp string (date/time or uptime)
String getTimestampString();

// ============================================================================
// DAY COUNTER FUNCTIONS (OFFLINE MODE)
// ============================================================================

// Load day counter from LittleFS
uint16_t loadDayCounter();

// Save day counter to LittleFS
void saveDayCounter(uint16_t day);

// Increment day counter
void incrementDayCounter();

// ============================================================================
// LOG FILE MANAGEMENT
// ============================================================================

// Initialize logging system (create directory, setup time)
bool initLogging();

// Get current log filename (date-based or day-based)
String getLogFilename();

// Check if log file should rollover (midnight or 24h)
bool shouldRolloverLog();

// Handle log file rollover
void handleLogRollover();

// Log current data to active log file
bool logData(float avgPosCurrent, float avgNegCurrent,
             float peakPosCurrent, float peakNegCurrent,
             float avgPosVoltage, float avgNegVoltage,
             float peakPosVoltage, float peakNegVoltage,
             int forwardTime, int reverseTime);

// Clean up log files older than LOG_RETENTION_DAYS (date-based only)
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
