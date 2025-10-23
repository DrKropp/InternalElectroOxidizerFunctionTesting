# Code Refactoring Guide
## OrinTech ElectroOxidizer - Modular Structure

### Overview
The codebase has been refactored from a single monolithic `main.cpp` file into a well-organized modular structure for improved readability and maintainability.

---

## New File Structure

```
InternalElectroOxidizerFunctionTesting/
├── include/
│   ├── config.h              # All constants and configuration
│   ├── globals.h             # Global variable declarations
│   ├── multi_network.h       # Multi-network WiFi management
│   ├── button_handler.h      # Button multi-reset functionality
│   └── portal_css.h          # WiFi portal CSS styling
├── src/
│   ├── main.cpp              # Main application (simplified)
│   ├── multi_network.cpp     # Multi-network implementation
│   └── button_handler.cpp    # Button handler implementation
└── data/
    ├── index.html            # Main dashboard HTML
    ├── style.css             # Main dashboard CSS
    └── script.js             # Main dashboard JavaScript
```

---

## Module Responsibilities

### `config.h` - Configuration & Constants
**Purpose**: Centralized configuration management

**Contains**:
- GPIO pin definitions
- PWM configuration
- ADC configuration
- Timing constants
- Multi-network settings
- Data structures (WiFiCredential)

**Usage**:
```cpp
#include "config.h"
// Access any constant directly
pinMode(testButton, INPUT_PULLUP);
```

---

### `globals.h` - Global Variables
**Purpose**: Central declaration of all shared variables

**Contains**:
- Global objects (WiFiManager, AsyncWebServer, etc.)
- Configuration variables (hostname, deviceName, etc.)
- Runtime state variables (isRunning, measurements, etc.)
- ADC buffers and handles

**Usage**:
```cpp
#include "globals.h"
// All global variables are available
isRunning = true;
```

---

### `multi_network.h/cpp` - Multi-Network WiFi
**Purpose**: Manage multiple WiFi network credentials

**Key Functions**:
- `initMultiNetworkStorage()` - Initialize network storage
- `loadSavedNetworks()` - Load from LittleFS
- `saveSavedNetworks()` - Save to LittleFS
- `addOrUpdateNetwork(ssid, password)` - Add/update network
- `removeNetwork(ssid)` - Remove network
- `connectToSavedNetworks()` - Try all networks in priority order
- `listSavedNetworks()` - Get formatted list

**Storage**: `/networks.json` in LittleFS

**Usage**:
```cpp
#include "multi_network.h"

// Try to connect to any saved network
if (connectToSavedNetworks()) {
    Serial.println("Connected!");
}

// Add a new network
addOrUpdateNetwork("MyWiFi", "password123");
```

---

### `button_handler.h/cpp` - Button Multi-Reset
**Purpose**: Handle button press detection and WiFi reset

**Key Functions**:
- `initButtonHandler()` - Initialize timestamp array
- `checkButtonMultiReset()` - Check for button presses (call in loop)
- `detectButtonMultiReset()` - Detect if reset condition met
- `triggerWiFiReset()` - Execute WiFi reset and restart

**Behavior**:
- 3 button presses within 5 seconds = WiFi reset
- Clears all networks and device settings
- Restarts ESP32 in config mode

**Usage**:
```cpp
#include "button_handler.h"

void setup() {
    initButtonHandler();
}

void loop() {
    checkButtonMultiReset();  // Call every iteration
}
```

---

### `portal_css.h` - WiFi Portal Styling
**Purpose**: Modern CSS for WiFiManager captive portal

**Contains**:
- Complete CSS as const char* string
- Modern gradient design
- Mobile-optimized responsive layout
- Inter font family

**Usage**:
```cpp
#include "portal_css.h"

wifiManager.setCustomHeadElement(PORTAL_CSS);
wifiManager.setTitle("OrinTech Device WiFi Configuration");
```

---

## Integration Example

### Simplified main.cpp Structure

```cpp
// Include all headers
#include "config.h"
#include "globals.h"
#include "multi_network.h"
#include "button_handler.h"
#include "portal_css.h"

// Define global variables (implementation)
WiFiManager wifiManager;
AsyncWebServer server(80);
// ... other globals ...

void setup() {
    Serial.begin(115200);

    // Initialize modules
    initFS();
    initMultiNetworkStorage();
    loadSavedNetworks();
    initButtonHandler();

    // Rest of setup...
}

void loop() {
    // Check for button reset
    checkButtonMultiReset();

    // WiFi reconnection with multi-network
    if (WiFi.status() != WL_CONNECTED) {
        connectToSavedNetworks();
    }

    // Rest of loop...
}
```

---

## Benefits of New Structure

### ✅ **Improved Readability**
- Each file has a single, clear purpose
- Easy to find specific functionality
- Logical organization

### ✅ **Better Maintainability**
- Changes isolated to specific modules
- Reduced risk of breaking unrelated code
- Clear dependencies

### ✅ **Easier Testing**
- Individual modules can be tested separately
- Mock external dependencies easily
- Unit test individual functions

### ✅ **Reduced Compilation Time**
- Only changed modules recompile
- Faster development iterations

### ✅ **Code Reusability**
- Multi-network module can be used in other projects
- Button handler is project-independent
- Configuration easily adaptable

---

## Migration Steps

### If starting fresh:
1. Use the new modular structure from the start
2. Include only needed headers in each file
3. Follow the established patterns

### If migrating existing code:
1. Identify self-contained functional areas
2. Move to appropriate header/source files
3. Update includes in main.cpp
4. Verify all external references
5. Test thoroughly

---

## Variable Scoping Rules

### Global Variables
- **Declared in**: `globals.h`
- **Defined in**: `main.cpp`
- **Used with**: `extern` in other modules

### Module-Local Variables
- **Declared in**: Module's .cpp file
- **Not exposed**: Keep internal to module
- **Example**: `buttonPressIndex` in `button_handler.cpp`

### Constants
- **Declared in**: `config.h`
- **Using**: `const` keyword
- **Scope**: Available everywhere via include

---

## Best Practices

### ✓ **DO**
- Keep related functions together in same module
- Use clear, descriptive function names
- Document public interfaces in headers
- Minimize global variables
- Use const for constants
- Include only what you need

### ✗ **DON'T**
- Mix unrelated functionality in one file
- Expose internal implementation details
- Use global variables when locals suffice
- Create circular dependencies
- Hardcode values (use config.h)

---

## Common Tasks

### Adding a New Constant
1. Open `include/config.h`
2. Add to appropriate section
3. Document purpose with comment
4. Use throughout project

### Adding a New Global Variable
1. Declare in `include/globals.h` with `extern`
2. Define in `src/main.cpp` (assign value)
3. Access from any module

### Creating a New Module
1. Create `include/module_name.h` (interface)
2. Create `src/module_name.cpp` (implementation)
3. Add function declarations to .h file
4. Implement functions in .cpp file
5. Include in main.cpp

---

## Troubleshooting

### Linker Error: "undefined reference"
- **Cause**: Variable declared but not defined
- **Fix**: Add definition in main.cpp

### Compiler Error: "redefinition"
- **Cause**: Variable defined in multiple files
- **Fix**: Use `extern` in headers, define only in .cpp

### Compiler Error: "does not name a type"
- **Cause**: Missing include
- **Fix**: Add necessary #include to file

---

## File Size Comparison

### Before Refactoring
```
main.cpp: ~1700 lines (all code)
```

### After Refactoring
```
main.cpp:         ~400 lines (core logic)
config.h:         ~100 lines (constants)
globals.h:        ~80 lines (declarations)
multi_network:    ~350 lines (network mgmt)
button_handler:   ~150 lines (button logic)
portal_css.h:     ~160 lines (styling)
```

**Result**: Each file is focused and manageable!

---

## Future Enhancements

### Potential Additional Modules
- `adc_manager.h/cpp` - ADC sampling and calibration
- `websocket_handler.h/cpp` - WebSocket message handling
- `settings_manager.h/cpp` - LittleFS settings operations
- `h_bridge_control.h/cpp` - Motor driver control logic

---

## Version History

### v2.0 - Modular Refactoring
- Split monolithic main.cpp into modules
- Created multi-network WiFi system
- Added button multi-reset handler
- Extracted CSS to separate file
- Improved code organization

### v1.0 - Original
- Single main.cpp file
- Basic WiFiManager integration
- WebSocket control interface

---

## Questions?

For additional information or issues:
1. Check individual header file documentation
2. Review function comments in source files
3. Refer to this guide for overall structure
4. Check git commit history for changes

---

**Last Updated**: 2025-10-23
**Author**: Claude (Anthropic) & Ramsey Kropp
**Project**: OrinTech ElectroOxidizer Device
