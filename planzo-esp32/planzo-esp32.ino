#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <TimeLib.h>

// OLED Setup
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// NVS Storage
Preferences prefs;

// Time management
struct TimeKeeper {
    int hours = 0;
    int minutes = 0;
    int seconds = 0;
    unsigned long lastMillis = 0;

    void update() {
        unsigned long currentMillis = millis();
        unsigned long elapsed = currentMillis - lastMillis;
        
        if (elapsed >= 1000) {  // One second has passed
            seconds += elapsed / 1000;
            lastMillis = currentMillis;
            
            if (seconds >= 60) {
                minutes += seconds / 60;
                seconds %= 60;
                
                if (minutes >= 60) {
                    hours += minutes / 60;
                    minutes %= 60;
                    
                    if (hours >= 24) {
                        hours %= 24;
                    }
                }
            }
        }
    }

    void setTime(int h, int m, int s) {
        hours = h;
        minutes = m;
        seconds = s;
        lastMillis = millis();
    }

    String getTimeString() {
        char timeStr[9];
        snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d", hours, minutes, seconds);
        return String(timeStr);
    }
} timeKeeper;

// User Config
struct Config {
    char alarmTime[6];     // HH:MM
    int studyDuration;     // Minutes
    char reminder[128];    // Reminder message
    bool timerActive;      // Is timer running?
    int timeRemaining;     // Seconds remaining in current session
};
Config userConfig;

// Timer States
enum TimerState {
    STOPPED,
    RUNNING,
    PAUSED
} timerState = STOPPED;

unsigned long timerLastUpdate = 0;

// ========== OLED HELPERS ==========
void initOLED() {
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println("SSD1306 allocation failed");
        for (;;);  // Don't proceed
    }
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.display();
}

void showOLEDMessage(const String& line1, const String& line2 = "", const String& line3 = "") {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println(line1);
    if (line2 != "") {
        display.setCursor(0, 16);
        display.println(line2);
    }
    if (line3 != "") {
        display.setCursor(0, 32);
        display.println(line3);
    }
    display.display();
}

void updateOLEDStatus() {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Time: " + timeKeeper.getTimeString());
    
    display.setCursor(0, 16);
    display.print("Alarm: ");
    display.println(userConfig.alarmTime);
    
    if (timerState != STOPPED) {
        display.setCursor(0, 32);
        int mins = userConfig.timeRemaining / 60;
        int secs = userConfig.timeRemaining % 60;
        display.printf("Remaining: %02d:%02d", mins, secs);
        
        display.setCursor(0, 48);
        display.print(timerState == RUNNING ? "RUNNING" : "PAUSED");
    } else {
        display.setCursor(0, 32);
        display.printf("Duration: %d min", userConfig.studyDuration);
    }
    
    display.display();
}

// ========== NVS HELPERS ==========
void loadConfig() {
    prefs.begin("studykit", true);
    strncpy(userConfig.alarmTime, prefs.getString("alarm", "07:30").c_str(), 6);
    userConfig.studyDuration = prefs.getInt("duration", 25);
    strncpy(userConfig.reminder, prefs.getString("reminder", "").c_str(), 128);
    userConfig.timerActive = false;
    userConfig.timeRemaining = userConfig.studyDuration * 60;
    prefs.end();
}

void saveConfig() {
    prefs.begin("studykit", false);
    prefs.putString("alarm", userConfig.alarmTime);
    prefs.putInt("duration", userConfig.studyDuration);
    prefs.putString("reminder", userConfig.reminder);
    prefs.end();
    
    Serial.println("Config saved:");
    Serial.printf("Alarm: %s\n", userConfig.alarmTime);
    Serial.printf("Duration: %d min\n", userConfig.studyDuration);
    Serial.printf("Reminder: %s\n", userConfig.reminder);
}

// ========== TIMER MANAGEMENT ==========
void updateTimer() {
    if (timerState == RUNNING) {
        unsigned long now = millis();
        if (now - timerLastUpdate >= 1000) {  // Update every second
            userConfig.timeRemaining--;
            timerLastUpdate = now;
            
            if (userConfig.timeRemaining <= 0) {
                timerState = STOPPED;
                showOLEDMessage("Time's Up!", userConfig.reminder);
                delay(2000);  // Show message for 2 seconds
            }
            updateOLEDStatus();
        }
    }
}

void handleTimerCommand(const char* command) {
    if (strcmp(command, "START") == 0) {
        if (timerState == STOPPED) {
            userConfig.timeRemaining = userConfig.studyDuration * 60;
        }
        timerState = RUNNING;
        timerLastUpdate = millis();
    }
    else if (strcmp(command, "STOP") == 0) {
        timerState = STOPPED;
        userConfig.timeRemaining = userConfig.studyDuration * 60;
    }
    else if (strcmp(command, "PAUSE") == 0) {
        if (timerState == RUNNING) {
            timerState = PAUSED;
        } else if (timerState == PAUSED) {
            timerState = RUNNING;
            timerLastUpdate = millis();
        }
    }
    else if (strcmp(command, "SHOW") == 0) {
        // Status is always shown by updateOLEDStatus
    }
    updateOLEDStatus();
}

// ========== SERIAL COMMUNICATION ==========
void handleSerialJson() {
    if (Serial.available()) {
        String jsonStr = Serial.readStringUntil('\n');
        StaticJsonDocument<512> doc;
        DeserializationError error = deserializeJson(doc, jsonStr);
        
        if (error) {
            Serial.print("deserializeJson() failed: ");
            Serial.println(error.c_str());
            return;
        }

        // Handle device time synchronization
        if (doc.containsKey("deviceTime")) {
            timeKeeper.setTime(
                doc["deviceTime"]["hours"],
                doc["deviceTime"]["minutes"],
                doc["deviceTime"]["seconds"]
            );
        }

        // Handle message types
        const char* type = doc["type"];
        
        if (strcmp(type, "config") == 0) {
            // Update configuration
            strncpy(userConfig.alarmTime, doc["alarmTime"], 6);
            userConfig.studyDuration = doc["studyDuration"];
            strncpy(userConfig.reminder, doc["reminder"], 128);
            saveConfig();
            
            // Send confirmation
            Serial.println("{\"status\":\"config_updated\"}");
            showOLEDMessage("Config Updated!", userConfig.alarmTime);
            delay(1000);
        }
        else if (strcmp(type, "command") == 0) {
            const char* action = doc["action"];
            handleTimerCommand(action);
            
            // Send confirmation
            Serial.printf("{\"status\":\"command_executed\",\"command\":\"%s\"}\n", action);
        }
        else if (strcmp(type, "sync") == 0) {
            // Time sync is handled above with deviceTime
            Serial.println("{\"status\":\"time_synced\"}");
        }
    }
}

// ========== MAIN ==========
void setup() {
    Serial.begin(9600);
    Wire.begin(6, 7);  // SDA=6, SCL=7
    Wire.setClock(50000);  // 50kHz
    
    initOLED();
    loadConfig();
    
    showOLEDMessage("PLanzo Ready!", "Waiting for", "connection...");
    Serial.println("{\"status\":\"ready\"}");
}

void loop() {
    timeKeeper.update();  // Update internal time
    updateTimer();        // Update study timer if running
    handleSerialJson();   // Handle incoming serial commands
    
    // Check for alarm
    if (timeKeeper.minutes == 0 && timeKeeper.seconds == 0) {  // Check every hour
        char currentTime[6];
        snprintf(currentTime, 6, "%02d:%02d", timeKeeper.hours, timeKeeper.minutes);
        
        if (strcmp(currentTime, userConfig.alarmTime) == 0) {
            showOLEDMessage("ALARM!", userConfig.alarmTime, userConfig.reminder);
            delay(5000);  // Show alarm for 5 seconds
        }
    }
}
