#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define BUZZER_PIN 2

// WiFi and NTP setup
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000); // Update every minute
String wifiSSID = "";
String wifiPassword = "";
bool wifiConnected = false;
bool ntpTimeAvailable = false;

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

// ====== WiFi and NTP Functions ======
void connectToWiFi() {
  if (wifiSSID == "" || wifiPassword == "") {
    Serial.println("[WIFI] No credentials available");
    return;
  }
  
  Serial.printf("[WIFI] Connecting to %s...\n", wifiSSID.c_str());
  WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
  
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 15000) {
    delay(500);
    Serial.print(".");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.printf("\n[WIFI] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    
    // Initialize NTP
    timeClient.begin();
    if (timeClient.update()) {
      ntpTimeAvailable = true;
      // Get time from NTP and set local clock
      unsigned long epochTime = timeClient.getEpochTime();
      struct tm *ptm = gmtime((time_t *)&epochTime);
      
      hours = ptm->tm_hour;
      minutes = ptm->tm_min;
      seconds = ptm->tm_sec;
      
      saveTimeToNVS();
      Serial.printf("[NTP] Time synchronized: %02d:%02d:%02d\n", hours, minutes, seconds);
      
      // Play success tune
      playTone(659, 150);
      playTone(784, 150);
      playTone(880, 200);
    }
  } else {
    wifiConnected = false;
    Serial.println("\n[WIFI] Connection failed");
    
    // Play failure tune
    playTone(440, 200);
    playTone(330, 200);
  }
}

void updateNTPTime() {
  if (wifiConnected && ntpTimeAvailable && timeClient.update()) {
    unsigned long epochTime = timeClient.getEpochTime();
    struct tm *ptm = gmtime((time_t *)&epochTime);
    
    hours = ptm->tm_hour;
    minutes = ptm->tm_min;
    seconds = ptm->tm_sec;
    
    saveTimeToNVS();
    Serial.println("[NTP] Time updated from server");
  }
}

void saveWiFiCredentials() {
  prefs.begin("wifi", false);
  prefs.putString("ssid", wifiSSID);
  prefs.putString("password", wifiPassword);
  prefs.end();
}

void loadWiFiCredentials() {
  prefs.begin("wifi", true);
  wifiSSID = prefs.getString("ssid", "");
  wifiPassword = prefs.getString("password", "");
  prefs.end();
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
    if (ntpTimeAvailable) {
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
  Serial.printf("NTP Time: %s\n", ntpTimeAvailable ? "Available" : "Not Available");
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
  
  // Attempt WiFi connection if credentials exist
  if (wifiSSID != "" && wifiPassword != "") {
    connectToWiFi();
  }
  
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
    
    // Update NTP time every 5 minutes
    static int ntpUpdateCounter = 0;
    ntpUpdateCounter++;
    if (ntpUpdateCounter >= 300) { // 300 seconds = 5 minutes
      ntpUpdateCounter = 0;
      updateNTPTime();
    }
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
      wifiSSID = doc["ssid"] | "";
      wifiPassword = doc["password"] | "";
      
      if (wifiSSID != "" && wifiPassword != "") {
        saveWiFiCredentials();
        Serial.printf("[WIFI] Credentials received for: %s\n", wifiSSID.c_str());
        
        // Disconnect existing WiFi if connected
        if (WiFi.status() == WL_CONNECTED) {
          WiFi.disconnect();
          wifiConnected = false;
          ntpTimeAvailable = false;
        }
        
        // Attempt new connection
        connectToWiFi();
      } else {
        Serial.println("[WIFI] Invalid credentials received");
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
