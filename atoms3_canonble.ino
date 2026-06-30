/*
 * Autonomous Canon Camera Remote
 * Hardware: M5Stack AtomS3 (ESP32-S3, 128x128 IPS display)
 * Version: 0.0.1
 * 
 * External button on GPIO38 + GND
 */

#include "M5AtomS3.h"
#include "CanonBLERemote.h"
#include <nvs_flash.h>

using namespace m5;

// External button pin
#define EXT_BUTTON_PIN 38

// Menu configuration
const int MENU_COUNT = 4;
const char* menuItems[] = {"Shutter", "Focus", "Video", "Pair"};

// Colors (RGB565)
#define C_BG       TFT_BLACK
#define C_TEXT     TFT_WHITE
#define C_SELECT   0x07E0  // Green
#define C_TITLE    0x07FF  // Cyan
#define C_OK       0x07E0  // Green
#define C_ERR      0xF800  // Red
#define C_DIM      0x630C  // Dark gray
#define C_WARN     0xFD20  // Orange

// Application state
CanonBLERemote* canon_ble = nullptr;
int selectedIdx = 0;
bool isConnected = false;
String statusMsg = "Init...";
bool isRecording = false;
unsigned long pressStartTime = 0;

// External button state
bool extButtonPressed = false;
unsigned long extPressStartTime = 0;
bool extButtonLastState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

// Draw complete screen
void drawScreen() {
    M5.Lcd.fillScreen(C_BG);
    
    // Title bar
    M5.Lcd.setTextColor(C_TITLE, C_BG);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(4, 4);
    M5.Lcd.print("Canon_ble_remote");
    
    // Status line
    if (isConnected) {
        M5.Lcd.setTextColor(C_OK, C_BG);
        M5.Lcd.setCursor(4, 16);
        M5.Lcd.print("* Connected");
    } else {
        M5.Lcd.setTextColor(C_WARN, C_BG);
        M5.Lcd.setCursor(4, 16);
        M5.Lcd.print("o ");
        M5.Lcd.print(statusMsg);
    }
    
    // Divider
    M5.Lcd.drawFastHLine(0, 27, 128, C_DIM);
    
    // Menu items
    for (int i = 0; i < MENU_COUNT; i++) {
        int y = 32 + i * 20;
        
        if (i == selectedIdx) {
            M5.Lcd.fillRoundRect(4, y, 120, 18, 4, C_SELECT);
            M5.Lcd.setTextColor(TFT_BLACK, C_SELECT);
        } else {
            M5.Lcd.setTextColor(C_TEXT, C_BG);
        }
        
        M5.Lcd.setTextSize(2);
        M5.Lcd.setCursor(12, y + 1);
        M5.Lcd.print(menuItems[i]);
        
        // Recording indicator
        if (i == 2 && isRecording) {
            M5.Lcd.fillCircle(114, y + 9, 5, C_ERR);
        }
    }
    
    // Footer
    M5.Lcd.drawFastHLine(0, 114, 128, C_DIM);
    M5.Lcd.setTextColor(C_DIM, C_BG);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(4, 118);
    M5.Lcd.print("Tap=Send  Hold=Next");
}

// Update status (screen only, no LED)
void updateStatus(String msg, bool connected) {
    statusMsg = msg;
    isConnected = connected;
    drawScreen();
}

// Execute selected command
void executeCommand(int idx) {
    if (idx == 3) {  // Pair
        statusMsg = "Pairing...";
        drawScreen();
        
        delete canon_ble;
        canon_ble = nullptr;
        
        nvs_flash_erase();
        delay(100);
        nvs_flash_init();
        delay(100);
        
        canon_ble = new CanonBLERemote("FlipCam");
        canon_ble->init();
        
        if (canon_ble->pair(15)) {
            updateStatus("Paired!", true);
        } else {
            updateStatus("Timeout", false);
        }
        return;
    }
    
    if (!canon_ble) return;
    
    statusMsg = String(menuItems[idx]) + "...";
    drawScreen();
    
    unsigned long start = millis();
    bool ok = false;
    
    switch (idx) {
        case 0: ok = canon_ble->trigger(); break;
        case 1: ok = canon_ble->focus(); break;
        case 2:
            ok = canon_ble->trigger();
            if (ok) isRecording = !isRecording;
            break;
    }
    
    unsigned long elapsed = millis() - start;
    
    if (ok) {
        if (elapsed > 2000) {
            updateStatus("OK " + String(elapsed / 1000) + "s", true);
        } else {
            updateStatus("OK " + String(elapsed) + "ms", true);
        }
    } else {
        updateStatus("FAIL", false);
    }
}

// Handle external button press
void handleExtButton() {
    int reading = digitalRead(EXT_BUTTON_PIN);
    
    // Debounce
    if (reading != extButtonLastState) {
        lastDebounceTime = millis();
    }
    
    if ((millis() - lastDebounceTime) > debounceDelay) {
        // Button pressed (LOW because of INPUT_PULLUP)
        if (reading == LOW && !extButtonPressed) {
            extButtonPressed = true;
            extPressStartTime = millis();
        }
        
        // Button released
        if (reading == HIGH && extButtonPressed) {
            extButtonPressed = false;
            unsigned long duration = millis() - extPressStartTime;
            
            if (duration >= 500) {
                // Long press: next menu item
                selectedIdx = (selectedIdx + 1) % MENU_COUNT;
                drawScreen();
            } else {
                // Short press: execute command
                executeCommand(selectedIdx);
            }
        }
    }
    
    extButtonLastState = reading;
}

void setup() {
    M5.begin();
    
    Serial.begin(115200);
    Serial.println("FlipCam BLE v0.0.1 - M5AtomS3");
    
    // Initialize external button
    pinMode(EXT_BUTTON_PIN, INPUT_PULLUP);
    
    nvs_flash_init();
    
    canon_ble = new CanonBLERemote("FlipCam");
    canon_ble->init();
    
    statusMsg = "Connecting...";
    drawScreen();
    
    bool connected = canon_ble->trigger();
    
    if (connected || canon_ble->isConnected()) {
        updateStatus("Ready", true);
    } else {
        updateStatus("Tap to connect", false);
    }
}

void loop() {
    M5.update();
    
    // Built-in button handling
    if (M5.BtnA.wasPressed()) {
        pressStartTime = millis();
    }
    
    if (M5.BtnA.wasReleased()) {
        unsigned long duration = millis() - pressStartTime;
        
        if (duration >= 500) {
            selectedIdx = (selectedIdx + 1) % MENU_COUNT;
            drawScreen();
        } else {
            executeCommand(selectedIdx);
        }
    }
    
    // External button handling
    handleExtButton();
    
    delay(10);
}