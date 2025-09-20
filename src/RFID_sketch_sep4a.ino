/*
  ESP32 RFID Attendance System (v3) 
  - Per-card 5-minute cooldown (card-specific, persisted)
  - Google Sheets integration (SlNo, UID, Day, Date, Name, Time In, Time Out, Status)
  - Offline queue (up to MAX_QUEUE entries) stored in Preferences (NVS)
  - LCD UI, scrolling names, live people count
  - Menu via two buttons (INPUT_PULLUP) and Serial commands
  - Use NTP for timestamps; small fallbacks described in comments
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Preferences.h>
#include <ctype.h>

// ----------------- USER CONFIG (fill these) ---------------------------------------------------------------
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* GAS_URL       = "https://script.google.com/macros/s/XXXXX/exec";
const char* SECRET_TOKEN  = "YOUR_SECRET_TOKEN";
// -----------------------------------------------------------------------------------------------------------

// ----------------- PIN CONFIG (change if required) ----------------------------------------------------------------
// Use safe ESP32 pins by default. If you're using an UNO/Mega, change pins accordingly.
#define SS_PIN         21   // MFRC522 SDA/SS
#define RST_PIN        22   // MFRC522 RST
#define BUZZER_PIN     15
#define I2C_SDA        5
#define I2C_SCL        4
// buttons: INPUT_PULLUP
#define BUTTON_MENU     32
#define BUTTON_SELECT   33
// -----------------------------------------------------------------------------------------------------------------

// ----------------- Memory & behavior config ----------------------------------------------------------------
#define NAME_START_BLOCK 4
#define ID_START_BLOCK   8
#define NAME_LEN         32
#define ID_LEN           16
#define MAX_QUEUE        50      // offline queue max entries
const unsigned long CARD_COOLDOWN_SECS = 5UL * 60UL; // 5 minutes (per card)
const long UTC_OFFSET = 19800L; // IST
const unsigned long SETUP_DELAY_MS = 1500; // startup pause for UX
const unsigned long LOOP_DELAY_MS = 40;    // short loop delay to yield CPU
// ---------------------------------------------------------------------------------------------------------------

// ----------------- Objects & globals ----------------------------------------------------------------
LiquidCrystal_I2C lcd(0x27, 16, 2);
MFRC522 mfrc522(SS_PIN, RST_PIN);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", UTC_OFFSET);
Preferences prefs;

struct LogEntry {
  String uid;
  String name;
  String pid;
  String day;
  String date;
  String time;
  String status; // "IN" or "OUT"
};

int insideCount = 0;
unsigned long lastScreenUpdate = 0;
unsigned long lastMinuteCheck = 0;

// Menu variables
enum MenuState { MENU_HOME, MENU_VIEW_LOGS, MENU_STATUS, MENU_CLEAR_LOGS, MENU_SET_TIME };
MenuState currentMenu = MENU_HOME;
int menuIndex = 0;
unsigned long lastButtonPress = 0;
const unsigned long MENU_DEBOUNCE_MS = 300;
int logViewIndex = 0;
bool confirmClearPending = false;
unsigned long menuMessageEndTime = 0;

// Scrolling
String scrollText = "";
unsigned long lastScrollTime = 0;
int scrollPos = 0;
const unsigned long SCROLL_DELAY = 400;
const unsigned long SCROLL_PAUSE = 1600;

// ----------------- Function prototypes ----------------------------------------------------------------
void setupHardware();
void connectToWiFi();
void updateIdleScreen();
void handleMenu();
void showMenu();
void handleSelect();
void displayMenuMessage(const char* line1, const char* line2, unsigned long durationMs);
void processCard();
String getUID();
String readTextMultiBlock(uint8_t startBlock, int maxLen);
bool postLog(const LogEntry &e);
void saveOffline(const LogEntry &e);
void flushOffline();
bool toggleState(const String &uid);
void playBeep(int freq, int durMs);
String getDayName(int wday);
void handleSerial(); // serial commands
unsigned long getNowEpoch(); // returns epoch seconds (NTP if available)
void ensurePrefsBeginEnd(); // minor helper

// ----------------- Setup ----------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);

  // hardware & prefs
  prefs.begin("rfid_main", false);
  insideCount = prefs.getInt("insideCount", 0);
  prefs.end();

  setupHardware();
  delay(200);

  lcd.clear();
  lcd.setCursor(0,0); lcd.print("RFID Attendance");
  lcd.setCursor(0,1); lcd.print("Starting up...");
  delay(SETUP_DELAY_MS);

  // connect WiFi and start NTP
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  connectToWiFi();
  timeClient.begin();
  timeClient.update();

  // initial display
  displayMenuMessage("System Ready", "Tap your card", 1500);
}

// ----------------- Main loop -----------------
void loop() {
  delay(LOOP_DELAY_MS);           // small yield
  handleSerial();                 // allow serial commands anytime
  handleMenu();                   // non-blocking menu
  updateIdleScreen();             // idle updates (time, welcome)
  flushOffline();                 // try sending queued logs

  // if a brief menu message is being shown, don't scan for cards
  if (menuMessageEndTime > 0 && millis() < menuMessageEndTime) return;
  if (menuMessageEndTime > 0 && millis() >= menuMessageEndTime) {
    menuMessageEndTime = 0; lastScreenUpdate = 0; // force redraw
  }

  // check for new card
  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) return;

  processCard();

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();

  // small debounce for reader
  delay(250);
}

// ----------------- Functions ----------------------------------------------------------------

void setupHardware() {
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BUTTON_MENU, INPUT_PULLUP);
  pinMode(BUTTON_SELECT, INPUT_PULLUP);

  Wire.begin(I2C_SDA, I2C_SCL);
  lcd.init(); lcd.backlight();
  SPI.begin();
  mfrc522.PCD_Init();

  // ledc buzzer on ESP32
  if defined(ESP32)
    //ledcSetup(0, 2000, 8);
    ledcAttach(BUZZER_PIN, 0);
}

// Connect to WiFi (non-blocking style with timeout)
void connectToWiFi() {
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(200);
    Serial.print(".");
  }
  Serial.println(WiFi.status() == WL_CONNECTED ? " WiFi OK" : " WiFi Failed");
}

// Update idle screen (time) and scrolling
void updateIdleScreen() {
  if (menuMessageEndTime > 0) return; // message active
  if (millis() - lastScreenUpdate < 1000) return; // update every second
  lastScreenUpdate = millis();

  // update NTP once per minute
  if (millis() - lastMinuteCheck > 60000 || lastMinuteCheck == 0) {
    timeClient.update();
    lastMinuteCheck = millis();
  }

  unsigned long epoch = timeClient.getEpochTime();
  if (epoch < 1640995200UL) { // fallback message if time invalid (<2022)
    lcd.clear();
    lcd.setCursor(0,0); lcd.print("Welcome to RSB!");
    lcd.setCursor(0,1); lcd.print("Time not synced");
    return;
  }

  struct tm *ptm = gmtime((time_t*)&epoch);
  char dateBuf[16];
  sprintf(dateBuf, "%02d/%02d/%02d", ptm->tm_mday, ptm->tm_mon + 1, (ptm->tm_year+1900)%100);

  // first line static welcome (no heavy clear to reduce flicker)
  lcd.setCursor(0,0); lcd.print("Welcome to RSB!  ");
  // second line show date + HH:MM
  lcd.setCursor(0,1);
  String s = String(dateBuf) + " " + timeClient.getFormattedTime().substring(0,5);
  lcd.print(s.substring(0, min((int)s.length(), 16)));
}

// Handle menu button navigation (non-blocking)
void handleMenu() {
  bool menuPressed = (digitalRead(BUTTON_MENU) == LOW);
  bool selectPressed = (digitalRead(BUTTON_SELECT) == LOW);
  unsigned long now = millis();
  if (menuPressed && (now - lastButtonPress > MENU_DEBOUNCE_MS)) {
    lastButtonPress = now;
    confirmClearPending = false;
    menuIndex = (menuIndex + 1) % 5;
    currentMenu = (MenuState)menuIndex;
    showMenu();
  }
  if (selectPressed && (now - lastButtonPress > MENU_DEBOUNCE_MS)) {
    lastButtonPress = now;
    handleSelect();
  }
}

// Render menu title
void showMenu() {
  lcd.clear();
  lcd.setCursor(0,0);
  switch (currentMenu) {
    case MENU_HOME: lcd.print("> Home"); break;
    case MENU_VIEW_LOGS: lcd.print("> View Logs"); break;
    case MENU_STATUS: lcd.print("> Status"); break;
    case MENU_CLEAR_LOGS: 
      if (confirmClearPending) { lcd.print("CONFIRM CLEAR"); lcd.setCursor(0,1); lcd.print("Select to CLEAR"); }
      else { lcd.print("> Clear Logs"); }
      break;
    case MENU_SET_TIME: lcd.print("> Set Time"); break;
  }
}

// Menu select actions
void handleSelect() {
  switch (currentMenu) {
    case MENU_HOME:
      lastScreenUpdate = 0; // force idle redraw
      menuIndex = 0;
      currentMenu = MENU_HOME;
      break;

    case MENU_VIEW_LOGS: {
      prefs.begin("rfid_queue", false);
      int count = prefs.getInt("count", 0);
      if (count == 0) {
        displayMenuMessage("Offline Logs", "Queue empty", 1500);
      } else {
        if (logViewIndex >= count) logViewIndex = 0;
        String packed = prefs.getString(String(logViewIndex).c_str(), "");
        // show first 16 chars and second line next chunk
        lcd.clear();
        lcd.setCursor(0,0); lcd.print(packed.substring(0, min(16, (int)packed.length())));
        lcd.setCursor(0,1); if (packed.length() > 16) lcd.print(packed.substring(16, min(32, (int)packed.length())));
        logViewIndex = (logViewIndex + 1) % count;
        delay(1400);
      }
      prefs.end();
      break;
    }

    case MENU_STATUS: {
      prefs.begin("rfid_main", false);
      int cnt = prefs.getInt("insideCount", 0);
      prefs.end();
      char buf[20];
      sprintf(buf, "Currently In: %d", cnt);
      displayMenuMessage("System Status", buf, 1800);
      break;
    }

    case MENU_CLEAR_LOGS:
      if (confirmClearPending) {
        prefs.begin("rfid_queue", false);
        int count = prefs.getInt("count", 0);
        for (int i = 0; i < count; ++i) prefs.remove(String(i).c_str());
        prefs.putInt("count", 0);
        prefs.end();
        displayMenuMessage("Offline Logs", "Cleared", 1400);
        confirmClearPending = false;
      } else {
        confirmClearPending = true;
        showMenu();
      }
      break;

    case MENU_SET_TIME:
      displayMenuMessage("Time Sync", "Syncing...", 200);
      if (!timeClient.forceUpdate()) displayMenuMessage("Time Sync", "Failed", 1400);
      else displayMenuMessage("Time Synced", timeClient.getFormattedTime().substring(0,5).c_str(), 1400);
      break;
  }
}

// Show a non-blocking menu message for durationMs
void displayMenuMessage(const char* line1, const char* line2, unsigned long durationMs) {
  lcd.clear();
  lcd.setCursor(0,0); lcd.print(line1);
  lcd.setCursor(0,1); lcd.print(line2);
  menuMessageEndTime = millis() + durationMs;
}

// --- Main card processing ---
// Card-based cooldown: each UID has own timestamp (epoch seconds) stored in Preferences
void processCard() {
  String uid = getUID();

  // determine current epoch (prefer NTP)
  unsigned long nowEpoch = getNowEpoch();
  // read last tap for this uid
  prefs.begin("rfid_users", false);
  unsigned long lastTapEpoch = prefs.getULong(uid.c_str(), 0);
  if (nowEpoch >= 1600000000UL) {
    // we have valid epoch time -> use epoch seconds cooldown
    if (lastTapEpoch != 0 && (nowEpoch - lastTapEpoch) < CARD_COOLDOWN_SECS) {
      prefs.end();
      displayMenuMessage("Cooldown Active", "Try later", 1800);
      playBeep(400, 160);
      return;
    }
    prefs.putULong(uid.c_str(), nowEpoch);
  } else {
    // no valid NTP time: use millis-based fallback stored as negative encoded (not ideal)
    unsigned long lastTapMs = prefs.getULong(uid.c_str(), 0);
    if (lastTapMs != 0 && (millis() - lastTapMs) < (CARD_COOLDOWN_SECS * 1000UL)) {
      prefs.end();
      displayMenuMessage("Cooldown Active", "Try later", 1800);
      playBeep(400, 160);
      return;
    }
    prefs.putULong(uid.c_str(), millis());
  }
  prefs.end();

  // Read name and id from card (multi-block for name)
  String name = readTextMultiBlock(NAME_START_BLOCK, NAME_LEN);
  String pid  = readTextMultiBlock(ID_START_BLOCK, ID_LEN);
  if (name.length() == 0) {
    displayMenuMessage("Read Error", "Card invalid", 1800);
    playBeep(300, 320);
    return;
  }

  // Toggle state (IN/OUT)
  bool isNowIn = toggleState(uid);
  String status = isNowIn ? "IN" : "OUT";

  // Timestamp: prefer NTP epoch; build date/day/time strings
  timeClient.update();
  unsigned long epoch = timeClient.getEpochTime();
  struct tm *ptm = gmtime((time_t*)&epoch);
  char dateBuf[16];
  sprintf(dateBuf, "%02d/%02d/%04d", ptm->tm_mday, ptm->tm_mon+1, ptm->tm_year+1900);
  String date = String(dateBuf);
  String timeS = timeClient.getFormattedTime(); // HH:MM:SS
  String day = getDayName(ptm->tm_wday);

  LogEntry e;
  e.uid = uid;
  e.name = name;
  e.pid = pid;
  e.day = day;
  e.date = date;
  e.time = timeS;
  e.status = status;

  // Feedback: scrolling name if too long
  String shortName = name;
  if (shortName.length() > 12) shortName = shortName.substring(0,11) + ".";
  displayMenuMessage((status + ": " + shortName).c_str(), timeS.substring(0,5).c_str(), 1800);
  playBeep(1000, 120);

  // send or store offline
  if (!postLog(e)) {
    saveOffline(e);
    displayMenuMessage("Saved Offline", "Will retry", 1400);
  }
}

// Read UID as continuous hex string
String getUID() {
  String s = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10) s += "0";
    s += String(mfrc522.uid.uidByte[i], HEX);
  }
  s.toUpperCase();
  return s;
}

// Multi-block reader for names/ids; stops before trailer block
String readTextMultiBlock(uint8_t startBlock, int maxLen) {
  MFRC522::MIFARE_Key localKey;
  for (byte i = 0; i < 6; ++i) localKey.keyByte[i] = 0xFF; // default sector key unless you formatted cards
  String out = "";
  byte buffer[18];

  uint8_t block = startBlock;
  while ((int)out.length() < maxLen) {
    if ((block % 4) == 3) break; // avoid sector trailer
    byte size = sizeof(buffer);
    if (mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, block, &localKey, &(mfrc522.uid)) != MFRC522::STATUS_OK) break;
    if (mfrc522.MIFARE_Read(block, buffer, &size) != MFRC522::STATUS_OK) break;
    for (int i = 0; i < 16 && (int)out.length() < maxLen; ++i) {
      if (buffer[i] == 0) { out.trim(); return out; }
      out += (char)buffer[i];
    }
    block++;
  }
  out.trim();
  return out;
}

// Toggle IN/OUT state persisted to prefs; returns new state (true=IN)
bool toggleState(const String &uid) {
  prefs.begin("rfid_state", false);
  bool cur = prefs.getBool(uid.c_str(), false);
  cur = !cur;
  prefs.putBool(uid.c_str(), cur);
  prefs.end();

  // update insideCount and persist
  prefs.begin("rfid_main", false);
  insideCount = prefs.getInt("insideCount", 0);
  insideCount += cur ? 1 : -1;
  if (insideCount < 0) insideCount = 0;
  prefs.putInt("insideCount", insideCount);
  prefs.end();

  return cur;
}

// Post log to Apps Script. Returns true if success (HTTP 200). Retries 2 times.
bool postLog(const LogEntry &e) {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client; client.setInsecure();
  HTTPClient http;
  const int MAX_TRIES = 2;
  for (int attempt = 1; attempt <= MAX_TRIES; ++attempt) {
    if (!http.begin(client, GAS_URL)) { delay(200); continue; }
    http.setTimeout(10000);
    http.addHeader("Content-Type", "application/json");
    String payload = "{";
    payload += "\"token\":\"" + String(SECRET_TOKEN) + "\",";
    payload += "\"uid\":\"" + e.uid + "\",";
    payload += "\"day\":\"" + e.day + "\",";
    payload += "\"date\":\"" + e.date + "\",";
    payload += "\"name\":\"" + e.name + "\",";
    payload += "\"time\":\"" + e.time + "\",";
    payload += "\"status\":\"" + e.status + "\"";
    payload += "}";
    int code = http.POST(payload);
    String resp = (code > 0) ? http.getString() : String();
    Serial.printf("postLog try %d code=%d resp=%s\n", attempt, code, resp.c_str());
    http.end();
    if (code == 200) return true;
    delay(300 * attempt);
  }
  return false;
}

// Save a log into offline queue (Preferences)
void saveOffline(const LogEntry &e) {
  prefs.begin("rfid_queue", false);
  int count = prefs.getInt("count", 0);
  if (count >= MAX_QUEUE) {
    // shift down drop oldest
    for (int i = 1; i < count; ++i) {
      String val = prefs.getString(String(i).c_str(), "");
      prefs.putString(String(i-1).c_str(), val);
    }
    count = MAX_QUEUE - 1;
  }
  // pack: uid|name|pid|day|date|time|status
  String packed = e.uid + "|" + e.name + "|" + e.pid + "|" + e.day + "|" + e.date + "|" + e.time + "|" + e.status;
  prefs.putString(String(count).c_str(), packed);
  prefs.putInt("count", count + 1);
  prefs.end();
  Serial.println("Saved offline: " + packed);
}

// Flush offline queue safely: attempt send, compact remaining, no data-loss
void flushOffline() {
  if (WiFi.status() != WL_CONNECTED) return;
  prefs.begin("rfid_queue", false);
  int count = prefs.getInt("count", 0);
  if (count <= 0) { prefs.end(); return; }
  Serial.printf("Flushing %d queued logs\n", count);

  int writeIndex = 0;
  for (int i = 0; i < count; ++i) {
    String packed = prefs.getString(String(i).c_str(), "");
    if (packed.length() == 0) continue;
    int p1 = packed.indexOf('|'), p2 = packed.indexOf('|', p1+1);
    int p3 = packed.indexOf('|', p2+1), p4 = packed.indexOf('|', p3+1);
    int p5 = packed.indexOf('|', p4+1), p6 = packed.indexOf('|', p5+1);
    if (p1 < 0 || p2 < 0 || p3 < 0 || p4 < 0 || p5 < 0 || p6 < 0) {
      Serial.println("Malformed queued entry, removing: " + packed);
      continue;
    }
    LogEntry e;
    e.uid = packed.substring(0,p1);
    e.name = packed.substring(p1+1,p2);
    e.pid  = packed.substring(p2+1,p3);
    e.day  = packed.substring(p3+1,p4);
    e.date = packed.substring(p4+1,p5);
    e.time = packed.substring(p5+1,p6);
    e.status = packed.substring(p6+1);

    if (postLog(e)) {
      Serial.println("Flushed: " + packed);
      // success -> do not copy back
    } else {
      // preserve this and remaining in compacted order
      prefs.putString(String(writeIndex).c_str(), packed);
      writeIndex++;
    }
  }

  // remove leftover keys beyond writeIndex
  for (int k = writeIndex; k < count; ++k) prefs.remove(String(k).c_str());
  prefs.putInt("count", writeIndex);
  prefs.end();
}

// Buzzer beep (LEDc on ESP32 or tone fallback)
void playBeep(int freq, int durMs) {
  #if defined(ESP32)
    ledcWriteTone(0, freq);
    delay(durMs);
    ledcWriteTone(0, 0);
  #else
    tone(BUZZER_PIN, freq, durMs);
    delay(durMs);
    noTone(BUZZER_PIN);
  #endif
}

// Day name helper
String getDayName(int wday) {
  static const char* days[]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
  if (wday < 0 || wday > 6) return "N/A";
  return String(days[wday]);
}

// Serial command handler (non-blocking)
void handleSerial() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  cmd.toLowerCase();
  if (cmd == "help") {
    Serial.println("Commands: status | flush | viewlogs | clearlogs | help");
  } else if (cmd == "status") {
    prefs.begin("rfid_main", false);
    int c = prefs.getInt("insideCount", 0);
    prefs.end();
    Serial.printf("Inside count: %d\n", c);
  } else if (cmd == "flush") {
    Serial.println("Manual flush...");
    flushOffline();
  } else if (cmd == "viewlogs") {
    prefs.begin("rfid_queue", false);
    int cnt = prefs.getInt("count", 0);
    for (int i = 0; i < cnt; ++i) {
      Serial.printf("%d: %s\n", i, prefs.getString(String(i).c_str(), "").c_str());
    }
    prefs.end();
  } else if (cmd == "clearlogs") {
    prefs.begin("rfid_queue", false);
    int cnt = prefs.getInt("count", 0);
    for (int i = 0; i < cnt; ++i) prefs.remove(String(i).c_str());
    prefs.putInt("count", 0);
    prefs.end();
    Serial.println("Queue cleared.");
  } else {
    Serial.println("Unknown. Type 'help'.");
  }
}

// Get current epoch seconds. If NTP not synced, returns 0
unsigned long getNowEpoch() {
  unsigned long epoch = timeClient.getEpochTime();
  if (epoch < 1600000000UL) return 0;
  return epoch;
}
