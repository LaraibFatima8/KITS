#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define BUZZER_PIN 2

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

  display.setTextSize(1);
  display.setCursor(0, 32);
  display.print("Reminder:");
  display.setCursor(0, 42);
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
  Serial.println("System started. Ready to receive data.");

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

    if (strcmp(type, "sync") == 0) {
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