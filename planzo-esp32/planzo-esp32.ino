#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <SparkFun_APDS9960.h>

// ========== OLED ==========
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define I2C_SDA 6
#define I2C_SCL 7
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ========== CLOCK ==========
#define BUZZER_PIN 2
Preferences prefs;
unsigned long lastMillis = 0;
int hours = 0, minutes = 0, seconds = 0;
int alarmHour = -1, alarmMinute = -1;
int studyDuration = 25;
String reminderMsg = "";
bool timerRunning = false;
unsigned long timerStartMillis = 0;

// ========== EYES ==========
int eyeW = 36, eyeH = 34, corner = 6, pupilR = 11;
int spacing = 42, yBase = 18, xBaseLeft = 15;
int gazeXRange = 12, gazeYRange = 8, speed = 3, frameDelay = 50;
int xOffset = 0, yOffset = 0, dir = 1;
bool horizontalPhase = true;

// ========== SCREENS ==========
enum ScreenState { BOOT, CLOCK_SCREEN, EYES_SCREEN, COUNTER_SCREEN };
ScreenState currentScreen = BOOT;
unsigned long screenStart = 0;
unsigned long lastInteraction = 0;
const unsigned long inactivityTimeout = 15000;
bool toggleEyes = false;

// ========== APDS9960 ==========
SparkFun_APDS9960 apds;
bool counterRunning = false;
unsigned long counterStart = 0;
unsigned long frozenCounter = 0;

// ---------- Helpers ----------
void playTone(int freq, int duration) {
  tone(BUZZER_PIN, freq, duration);
  delay(duration * 1.3);
  noTone(BUZZER_PIN);
}
void playWelcomeTune() { playTone(880,200); playTone(988,200); playTone(1046,300); }

// ---------- NVS ----------
void saveState() {
  prefs.begin("planzo", false);
  prefs.putInt("hours", hours);
  prefs.putInt("minutes", minutes);
  prefs.putInt("seconds", seconds);
  prefs.putInt("alarmHour", alarmHour);
  prefs.putInt("alarmMinute", alarmMinute);
  prefs.putInt("duration", studyDuration);
  prefs.putString("reminder", reminderMsg);
  prefs.end();
  Serial.println("[NVS] Saved");
}
void loadState() {
  prefs.begin("planzo", true);
  hours = prefs.getInt("hours", 0);
  minutes = prefs.getInt("minutes", 0);
  seconds = prefs.getInt("seconds", 0);
  alarmHour = prefs.getInt("alarmHour", -1);
  alarmMinute = prefs.getInt("alarmMinute", -1);
  studyDuration = prefs.getInt("duration", 25);
  reminderMsg = prefs.getString("reminder", "");
  prefs.end();
  Serial.println("[NVS] Loaded");
}

// ---------- Status Sync ----------
void sendStatus() {
  StaticJsonDocument<256> doc;
  doc["type"] = "status";
  doc["time"]["hours"] = hours;
  doc["time"]["minutes"] = minutes;
  doc["time"]["seconds"] = seconds;
  doc["alarm"]["hour"] = alarmHour;
  doc["alarm"]["minute"] = alarmMinute;
  doc["duration"] = studyDuration;
  doc["reminder"] = reminderMsg;
  doc["timerRunning"] = timerRunning;

  serializeJson(doc, Serial);
  Serial.println();
}

// ---------- Display ----------
void showClock() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SH110X_WHITE);
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
    int mm = max(remain,0) / 60, ss = max(remain,0) % 60;
    display.setCursor(0, 54);
    display.printf("Timer: %02d:%02d", mm, ss);
  }

  display.display();
}
void drawEyes() {
  display.clearDisplay();
  int xShift = xOffset, yShift = yOffset;
  display.drawRoundRect(xBaseLeft + xShift, yBase + yShift, eyeW, eyeH, corner, SH110X_WHITE);
  display.fillCircle(xBaseLeft + xShift + eyeW/2, yBase + yShift + eyeH/2, pupilR, SH110X_WHITE);
  display.drawRoundRect(xBaseLeft + spacing + xShift, yBase + yShift, eyeW, eyeH, corner, SH110X_WHITE);
  display.fillCircle(xBaseLeft + spacing + xShift + eyeW/2, yBase + yShift + eyeH/2, pupilR, SH110X_WHITE);
  display.display();
  if (horizontalPhase) { xOffset += dir*speed; if(xOffset>gazeXRange||xOffset<-gazeXRange){dir=-dir;if(xOffset==0){horizontalPhase=false;dir=1;}}}
  else { yOffset += dir*speed; if(yOffset>gazeYRange||yOffset<-gazeYRange){dir=-dir;if(yOffset==0){horizontalPhase=true;dir=1;}}}
}
void showCounter() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SH110X_WHITE);
  unsigned long elapsed = counterRunning ? (millis()-counterStart)/1000 : frozenCounter;
  if (counterRunning) frozenCounter = elapsed;
  int mm = elapsed / 60, ss = elapsed % 60;
  display.setCursor(10, 20);
  display.printf("Count %02d:%02d", mm, ss);
  display.display();
}

// ---------- Serial JSON ----------
void handleSerialInput(String line) {
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, line)) return;
  const char* type = doc["type"];
  if (!type) return;

  if (strcmp(type,"sync")==0) {
    hours=doc["deviceTime"]["hours"];
    minutes=doc["deviceTime"]["minutes"];
    seconds=doc["deviceTime"]["seconds"];
    saveState();
    sendStatus();
  } else if (strcmp(type,"config")==0) {
    String alarm=doc["alarmTime"];
    if(alarm.length()==5){alarmHour=alarm.substring(0,2).toInt(); alarmMinute=alarm.substring(3).toInt();}
    studyDuration=doc["studyDuration"];
    reminderMsg=(const char*)doc["reminder"];
    saveState();
    sendStatus();
  } else if (strcmp(type,"command")==0) {
    String a=doc["action"];
    if(a=="START"){timerRunning=true; timerStartMillis=millis();}
    if(a=="STOP"){timerRunning=false;}
    if(a=="PAUSE"){timerRunning=false;}
    if(a=="SHOW"){sendStatus();}
  }
}

// ---------- Setup ----------
void setup() {
  Serial.begin(9600);
  pinMode(BUZZER_PIN, OUTPUT);
  Wire.begin(I2C_SDA, I2C_SCL);
  if (!display.begin(0x3C, OLED_RESET)) { Serial.println("[OLED] FAIL"); while(1); }
  display.clearDisplay(); display.display();

  if(apds.init()){ apds.enableGestureSensor(true); Serial.println("[APDS9960] OK"); }

  loadState();
  playWelcomeTune();
  currentScreen = BOOT;
  screenStart = millis();
  lastInteraction = millis();
  sendStatus(); // send boot status
}

// ---------- Loop ----------
void loop() {
  unsigned long now = millis();
  if (now - lastMillis >= 1000) {
    lastMillis = now; seconds++;
    if(seconds>=60){seconds=0;minutes++;if(minutes>=60){minutes=0;hours++;if(hours>=24)hours=0;}}
    if (seconds%30==0) saveState();
  }

  if (apds.isGestureAvailable()) {
    int g = apds.readGesture();
    switch(g){ case DIR_LEFT: currentScreen=CLOCK_SCREEN; break;
               case DIR_RIGHT: currentScreen=EYES_SCREEN; break;
               case DIR_UP: currentScreen=COUNTER_SCREEN; counterRunning=true; counterStart=millis(); break;
               case DIR_DOWN: counterRunning=false; break; }
    lastInteraction=now;
    sendStatus();
  }

  while(Serial.available()){
    String line=Serial.readStringUntil('\n'); line.trim();
    if(line.length()>0) handleSerialInput(line);
  }

  if((now-lastInteraction>inactivityTimeout)&&!counterRunning){
    currentScreen = toggleEyes ? EYES_SCREEN : CLOCK_SCREEN;
    toggleEyes=!toggleEyes; lastInteraction=now;
  }

  switch(currentScreen){
    case BOOT: drawEyes(); if(now-screenStart>3000) currentScreen=CLOCK_SCREEN; break;
    case CLOCK_SCREEN: showClock(); break;
    case EYES_SCREEN: drawEyes(); break;
    case COUNTER_SCREEN: showCounter(); break;
  }

  delay(frameDelay);
}
