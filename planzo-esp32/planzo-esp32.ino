#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <time.h>  // configTime and getLocalTime

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Buzzer pin
#define BUZZER_PIN 2

// WiFi credential slots and time source
#define WIFI_SLOTS 3
String wifiSSIDs[WIFI_SLOTS];
String wifiPasswords[WIFI_SLOTS];
bool wifiConnected = false;
String timeSource = "PC";

Preferences prefs;

// Clock state
unsigned long lastMillis = 0;
int hours = 0, minutes = 0, seconds = 0;

// Stored settings
int alarmHour = -1, alarmMinute = -1;
int studyDuration = 25; // minutes
String reminderMsg = "";
bool timerRunning = false;
bool timerPaused = false;
unsigned long timerStartMillis = 0;

// Forward declarations
void saveTimeToNVS();
void playTone(int freq, int duration);

// ====== WiFi slot and time helpers ======
// Load all WiFi credentials from NVS
void loadWiFiCredentials() {
  prefs.begin("wifi", true);
  for (int i = 0; i < WIFI_SLOTS; i++) {
    char key[16];
    snprintf(key, sizeof(key), "ssid%d", i);
    wifiSSIDs[i] = prefs.getString(key, "");
    snprintf(key, sizeof(key), "password%d", i);
    wifiPasswords[i] = prefs.getString(key, "");
  }
  prefs.end();
  // Debug: show loaded WiFi slots
  Serial.println("[WIFI] Loaded credentials:");
  for (int i = 0; i < WIFI_SLOTS; i++) {
    Serial.printf("  Slot %d: %s\n", i, wifiSSIDs[i].length() ? wifiSSIDs[i].c_str() : "<empty>");
  }
}

// Save a single slot's credentials to NVS
void saveWiFiCredential(int slot) {
  prefs.begin("wifi", false);
  char key[16];
  snprintf(key, sizeof(key), "ssid%d", slot);
  prefs.putString(key, wifiSSIDs[slot]);
  snprintf(key, sizeof(key), "password%d", slot);
  prefs.putString(key, wifiPasswords[slot]);
  prefs.end();
}

// Attempt WiFi connections from stored slots
void attemptWiFiConnections() {
  struct tm tmTime;
  // Debug: scan for available networks
  Serial.println("[SCAN] Scanning for WiFi networks...");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  int n = WiFi.scanNetworks();
  Serial.printf("[SCAN] %d networks found\n", n);
  for (int i = 0; i < n; i++) {
    Serial.printf("  %d: %s (RSSI %d)\n", i, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
  }
  // Prepare to attempt connections
  for (int i = 0; i < WIFI_SLOTS; i++) {
    if (wifiSSIDs[i].length() == 0) {
      Serial.printf("[WIFI] Slot %d: empty, skipping\n", i);
      continue;
    }
    // Check if SSID is in scan results
    bool found = false;
    for (int j = 0; j < n; j++) {
      if (WiFi.SSID(j) == wifiSSIDs[i]) {
        found = true;
        break;
      }
    }
    Serial.printf("[WIFI] Slot %d: SSID '%s' %s\n", i, wifiSSIDs[i].c_str(), found ? "found" : "not found");
    Serial.printf("[WIFI] Attempting slot %d connection...\n", i);
    WiFi.begin(wifiSSIDs[i].c_str(), wifiPasswords[i].c_str());
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
      delay(500);
      Serial.print('.');
    }
    if (WiFi.status() == WL_CONNECTED) {
      wifiConnected = true;
      timeSource = "WiFi";
      Serial.printf("\n[WIFI] Connected slot %d! IP: %s\n", i, WiFi.localIP().toString().c_str());
      configTime(0, 0, "pool.ntp.org");
      if (getLocalTime(&tmTime)) {
        hours = tmTime.tm_hour; minutes = tmTime.tm_min; seconds = tmTime.tm_sec;
        saveTimeToNVS();
        Serial.printf("[TIME][%s] %02d:%02d:%02d\n", timeSource.c_str(), hours, minutes, seconds);
      }
      return;
    }
    Serial.println("\n[WIFI] Slot connection failed");
  }
  wifiConnected = false;
  timeSource = "PC";
  Serial.println("[WIFI] No slot connected; will sync via PC only");
}

// ====== Helper: Play Tunes ======
void playTone(int freq, int duration) {
  tone(BUZZER_PIN, freq, duration);
  delay(duration * 1.3);
  noTone(BUZZER_PIN);
}

void playWelcomeTune() {
  playTone(880, 200);
  playTone(988, 200);
  playTone(1046, 300);
}

void playRebootTune() {
  playTone(988, 200);
  playTone(880, 200);
}

void playAlarmTune() {
  for (int i = 0; i < 3; i++) {
    playTone(1046, 200);
    playTone(880, 200);
  }
}

void playTimerCompleteTune() {
  playTone(523, 200);
  playTone(659, 200);
  playTone(784, 300);
}

// ====== OLED Functions ======
void showClock() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 0);
  display.printf("%02d:%02d:%02d", hours, minutes, seconds);

  // WiFi status indicator
  display.setTextSize(1);
  display.setCursor(100, 0);
  if (wifiConnected) {
    display.print("WiFi");
    display.setCursor(100, 8);
    if (timeSource == "WiFi") {
      display.print("NTP");
    }
  }

  display.setCursor(0, 24);
  display.print("Reminder:");
  display.setCursor(0, 34);
  display.print(reminderMsg);

  if (timerRunning) {
    unsigned long elapsed = (millis() - timerStartMillis) / 1000;
    int remain = (studyDuration * 60) - elapsed;
    int mm = remain / 60, ss = remain % 60;
    display.setCursor(0, 54);
    display.printf("Timer: %02d:%02d", mm, ss);
  }

  display.display();
}

void showAlarmAnimation() {
  for (int i = 0; i < 5; i++) {
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(20, 20);
    display.print("ALARM!");
    display.display();
    delay(300);

    display.clearDisplay();
    display.display();
    delay(300);
  }
}

// ====== NVS Helpers ======
void saveTimeToNVS() {
  prefs.begin("clock", false);
  prefs.putInt("hours", hours);
  prefs.putInt("minutes", minutes);
  prefs.putInt("seconds", seconds);
  prefs.end();
}

void loadTimeFromNVS() {
  prefs.begin("clock", true);
  hours = prefs.getInt("hours", 0);
  minutes = prefs.getInt("minutes", 0);
  seconds = prefs.getInt("seconds", 0);
  alarmHour = prefs.getInt("alarmHour", -1);
  alarmMinute = prefs.getInt("alarmMinute", -1);
  studyDuration = prefs.getInt("studyDuration", 25);
  reminderMsg = prefs.getString("reminderMsg", "");
  prefs.end();
}

void saveConfigToNVS() {
  prefs.begin("clock", false);
  prefs.putInt("alarmHour", alarmHour);
  prefs.putInt("alarmMinute", alarmMinute);
  prefs.putInt("studyDuration", studyDuration);
  prefs.putString("reminderMsg", reminderMsg);
  prefs.end();
}

void printStatus() {
  Serial.println("=== STATUS ===");
  Serial.printf("Clock: %02d:%02d:%02d\n", hours, minutes, seconds);
  Serial.printf("Alarm: %02d:%02d\n", alarmHour, alarmMinute);
  Serial.printf("Study Duration: %d mins\n", studyDuration);
  Serial.printf("Reminder: %s\n", reminderMsg.c_str());
  Serial.printf("Timer: %s %s\n", timerRunning ? "Running" : "Stopped",
                timerPaused ? "(Paused)" : "");
  Serial.printf("WiFi: %s", wifiConnected ? "Connected" : "Disconnected");
  if (wifiConnected) {
    Serial.printf(" (IP: %s)", WiFi.localIP().toString().c_str());
  }
  Serial.println();
  Serial.printf("Time Source: %s\n", timeSource.c_str());
  Serial.println("==============");
}

// ====== MAIN ======
void setup() {
  Serial.begin(9600);
  pinMode(BUZZER_PIN, OUTPUT);

  // === OLED init (from working sketch) ===
  Wire.begin(6, 7);       // SDA = 6, SCL = 7
  Wire.setClock(50000);   // Slow I2C for stability

  bool oled_ok = false;
  for (int i = 0; i < 3; i++) {
    if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
      oled_ok = true;
      break;
    }
    delay(100);
  }

  if (!oled_ok) {
    Serial.println("OLED failed!");
    while (1);
  }

  display.clearDisplay();
  display.display();

    loadTimeFromNVS();
    loadWiFiCredentials();
    // Attempt connections from stored slots
    attemptWiFiConnections();
  
  Serial.println("System started. Ready to receive data.");
  printStatus();

  playWelcomeTune();
}

void loop() {
  // Tick clock
  if (millis() - lastMillis >= 1000) {
    lastMillis = millis();
    seconds++;
    if (seconds >= 60) {
      seconds = 0;
      minutes++;
      if (minutes >= 60) {
        minutes = 0;
        hours++;
        if (hours >= 24) hours = 0;
      }
    }
    saveTimeToNVS();

  // Update OLED
  showClock();
  // Print clock to serial with time source
  Serial.printf("[TIME][%s] %02d:%02d:%02d\n", timeSource.c_str(), hours, minutes, seconds);

    // Check alarm
    if (hours == alarmHour && minutes == alarmMinute && seconds == 0) {
      Serial.println("[ALARM] Triggered!");
      showAlarmAnimation();
      playAlarmTune();
    }

    // Check study timer
    if (timerRunning && !timerPaused) {
      unsigned long elapsed = (millis() - timerStartMillis) / 1000;
      if (elapsed >= (unsigned long)studyDuration * 60) {
        timerRunning = false;
        Serial.println("[TIMER] Study session complete!");
        playTimerCompleteTune();
      }
    }
    
    // (No periodic NTP client updates; time via WiFi slot or PC sync)
  }

  // Read serial input
  if (Serial.available()) {
    String json = Serial.readStringUntil('\n');
    json.trim();
    if (json.length() == 0) return;

    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
      Serial.println("[ERROR] Invalid JSON.");
      return;
    }

    const char* type = doc["type"];

    if (strcmp(type, "wifi") == 0) {
      int slot = doc["slot"] | -1;
      const char* ssid = doc["ssid"] | "";
      const char* pass = doc["password"] | "";
      if (slot >= 0 && slot < WIFI_SLOTS && strlen(ssid) > 0 && strlen(pass) > 0) {
        wifiSSIDs[slot] = String(ssid);
        wifiPasswords[slot] = String(pass);
        saveWiFiCredential(slot);
        Serial.printf("[WIFI] Slot %d credentials saved: %s\n", slot, ssid);
        attemptWiFiConnections();
      } else {
        Serial.println("[WIFI] Invalid slot or credentials");
      }
    }
    else if (strcmp(type, "sync") == 0) {
      hours = doc["deviceTime"]["hours"] | hours;
      minutes = doc["deviceTime"]["minutes"] | minutes;
      seconds = doc["deviceTime"]["seconds"] | seconds;
      saveTimeToNVS();
      Serial.printf("[SYNC] Clock set to %02d:%02d:%02d\n", hours, minutes, seconds);
    }
    else if (strcmp(type, "config") == 0) {
      String alarm = doc["alarmTime"] | "";
      if (alarm.length() > 0) {
        sscanf(alarm.c_str(), "%d:%d", &alarmHour, &alarmMinute);
      }
      studyDuration = doc["studyDuration"] | studyDuration;
      reminderMsg = doc["reminder"] | reminderMsg;
      saveConfigToNVS();
      Serial.println("[CONFIG] Saved configuration:");
      printStatus();
    }
    else if (strcmp(type, "command") == 0) {
      String action = doc["action"] | "";
      if (action == "START") {
        timerRunning = true;
        timerPaused = false;
        timerStartMillis = millis();
        Serial.println("[CMD] Timer started.");
      }
      else if (action == "STOP") {
        timerRunning = false;
        timerPaused = false;
        Serial.println("[CMD] Timer stopped.");
      }
      else if (action == "PAUSE") {
        if (timerRunning) {
          timerPaused = !timerPaused;
          Serial.printf("[CMD] Timer %s.\n", timerPaused ? "paused" : "resumed");
        }
      }
      else if (action == "SHOW") {
        printStatus();
      }
    }
  }
}
