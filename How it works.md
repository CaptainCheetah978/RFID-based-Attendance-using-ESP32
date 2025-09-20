# 📖 How It Works — ESP32 RFID Attendance System

This document explains **everything** about the project — from hardware to software, setup, usage, and troubleshooting.  
It’s written as both a **guide** for new users and a **reference** for future improvements.

---
### Table of contents
1.  Required hardware (BOM + suggested models)
2.  Wiring / pin mapping (exact)
3.  Required software, boards & libraries (Arduino IDE)
4. How to configure secrets (secrets.example.h)
5. Google Sheets & Apps Script (full doPost script + deployment steps)
6. How the Arduino code works (high-level + file-by-file)
7. Step-by-step: compile & upload in Arduino IDE
8. Testing: Serial Monitor (simulated scans) & Wokwi
9. Using the physical system (user flow)
10. Offline queue / cooldown / persistence — deeper explanation
11. Troubleshooting (common errors and fixes)
12. Recommended future improvements
13. License

---
## 🛠️ 1. Hardware Requirements

### Core Components
| Component | Model / Specs | Qty | Notes |
|-----------|---------------|-----|-------|
| **ESP32 Development Board** | ESP32-WROOM-32 (DevKit V1) | 1 | Chosen for built-in Wi-Fi, BLE, dual-core 240MHz CPU, 4MB Flash |
| **RFID Reader** | MFRC522 (13.56 MHz) | 1 | Works with Mifare Classic 1K cards |
| **RFID Cards / Tags** | Mifare Classic 1K, 13.56 MHz | ≥2 | Store Name + ID on card memory |
| **LCD Display** | 16x2 LiquidCrystal_I2C (I2C address 0x27) | 1 | Shows real-time messages |
| **Buzzer** | Passive buzzer module | 1 | Provides audio feedback |
| **Push Buttons** | Tactile buttons (Menu + Select) | 2 | Navigation for LCD menu |
| **Breadboard** | Standard size | 1 | For prototyping |
| **Jumper Wires** | Male-to-male | ~20 | For connections |
| **Power Supply** | USB cable (5V) | 1 | From PC or 5V adapter |

Optional but useful:
- RTC (DS3231) — for real-time without Wi-Fi
- MicroSD module — for larger offline store alternatives
- Enclosure, standoffs, screws
---

## ⚙️ 2. Software & Libraries

### Board
- **ESP32 Dev Module** (select in Arduino IDE → Tools → Board → ESP32 Dev Module)

### Required Libraries (install via Arduino Library Manager)
- `WiFi.h` (built-in ESP32 core)
- `WiFiClientSecure.h` (built-in ESP32 core)
- `HTTPClient.h` (built-in ESP32 core)
- `SPI.h`
- `MFRC522.h` (by GitHUbCommunity) -- for RFID Sensor
- `Wire.h`
- `LiquidCrystalDisplay_I2C.h` (by Martin Kubovcik, Frank de Brabander) -- For I2C LCD Display
- `NTPClient.h` (by by Fabrice Weinberg) 
- `Preferences.h` (built-in ESP32 core)

### From BOARDS MANAGER
- `esp32` by Espressif Systems
---

## 💻 3. Wring Connections 

1. ESP32 → MFRC522 RFID Module
   - SDA → GPIO 21 (SS)
   - RST → GPIO 22
   - MOSI → GPIO 23
   - MISO → GPIO 19
   - SCK → GPIO 18
   - 3.3V → VCC `IMPORTANT: RC522 must be powered with 3.3V`
   - GND → GND
     
2. ESP32 → LCD (I2C)
    - SDA → GPIO 5
    - SCL → GPIO 4
    - VCC → 5V
    - GND → GND
  `Common I2C addresses: 0x27 or 0x3F (run I²C scanner if blank LCD)`

3. ESP32 → Buzzer
    - Buzzer Pin → GPIO 15 `(PWM/LEDC capable)`
    - GND → GND
      
4. ESP32 → Buttons `(INPUT_PULLUP)`
   - Menu Button → GPIO 8 (INPUT_PULLUP)
   - Select Button → GPIO 9 (INPUT_PULLUP)

5. Power
   - ESP32 USB 5V -> VIN (on dev board)
   - Use 3.3V rail for MFRC522
   - Common GND for all modules

Notes:
- Keep SPI lines short (<10–15 cm) for reliability.
- Use female-to-female or male-to-female jumper wires depending on modules.
- If your I²C or SPI pins differ on your board, adjust the sketch accordingly.
---
## Configure secrets (create secrets.h safely)
- Create `include/secrets.h` (or in project root) — DO NOT commit this file.
- `secrets.example.h` (public template):
```cpp
#pragma once

// Wi-Fi
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Google Apps Script Web App URL
const char* GAS_URL = "https://script.google.com/macros/s/XXXXX/exec";

// Shared secret token (random, long)
const char* SECRET_TOKEN = "REPLACE_WITH_A_LONG_RANDOM_STRING";
```
- Save as `secrets.h` and add it to `.gitignore`.
- Use a strong random token (20+ characters). This token is validated by Apps Script.

---

## ☁️ 4. Google Sheets & Apps Script (full code + deployment)

We store logs in Google Sheets and protect writes using a `SECRET_TOKEN`. The Apps Script will:
- On Check-In: append a row with Time In and leave Time Out blank.
- On Check-Out: find the latest row for that UID with empty Time Out and fill Time Out and Status.
  
Step A — Create a Google Sheet
- Create a sheet and name the first sheet `Attendance`.
- Set column headers in row 1:
Sl No | UID | Day | Date | Name | Time In | Time Out | Status

Step B — Apps Script code
Open Extensions → Apps Script → Create a new project and replace `Code.gs` with:

// Apps Script: doPost to accept JSON and update spreadsheet
const SHEET_ID = "PASTE_YOUR_SHEET_ID_HERE"; // from sheet URL
const SHEET_NAME = "Attendance";
const SECRET = "REPLACE_WITH_YOUR_SECRET_TOKEN"; // same as SECRET_TOKEN in secrets.h

```javascript
function doPost(e) {
  try {
    if (!e.postData || !e.postData.contents) {
      return ContentService.createTextOutput("NoData");
    }
    var params = JSON.parse(e.postData.contents);
    if (params.token !== SECRET) {
      return ContentService.createTextOutput("Unauthorized");
    }

    var ss = SpreadsheetApp.openById(SHEET_ID);
    var sheet = ss.getSheetByName(SHEET_NAME);
    if (!sheet) {
      sheet = ss.insertSheet(SHEET_NAME);
      sheet.appendRow(["Sl No","UID","Day","Date","Name","Time In","Time Out","Status"]);
    }

    var uid = params.uid || "";
    var name = params.name || "";
    var day = params.day || "";
    var date = params.date || "";
    var time = params.time || "";
    var status = params.status || "";

    // Normalize status
    status = status.toUpperCase();

    if (status == "IN") {
      // Append new row for check-in
      var lastRow = sheet.getLastRow();
      var sl = (lastRow < 1) ? 1 : lastRow;
      sheet.appendRow([sl, uid, day, date, name, time, "", "IN"]);
    } else if (status == "OUT") {
      // Find last row matching UID with empty Time Out
      var data = sheet.getDataRange().getValues();
      // iterate backwards to find latest matching 'IN' without Time Out
      for (var r = data.length - 1; r >= 1; r--) {
        var row = data[r];
        var rowUID = String(row[1]);
        var rowStatus = String(row[7] || "");
        var timeOutVal = row[6];
        if (rowUID === uid && String(rowStatus).toUpperCase() === "IN" && (!timeOutVal || timeOutVal === "")) {
          // Update Time Out and status
          sheet.getRange(r + 1, 7).setValue(time); // Time Out column (G)
          sheet.getRange(r + 1, 8).setValue("OUT"); // Status column (H)
          return ContentService.createTextOutput("Updated");
        }
      }
      // If nothing found, append a row with OUT anyway
      sheet.appendRow(["", uid, day, date, name, "", time, "OUT"]);
    } else {
      // If status unknown, append general log
      sheet.appendRow(["", uid, day, date, name, time, "", status]);
    }

    return ContentService.createTextOutput("OK");
  } catch (err) {
    return ContentService.createTextOutput("Error:" + err.toString());
  }
}
```
Step C — Deploy
- Click Deploy → New deployment.
- Select Web app.
- For Who has access, choose Anyone (or restrict via Google account per your security considerations). For initial testing, `Anyone` is easiest.
- Click Deploy and copy the Web app URL.
- Paste that URL into `GAS_URL` in your `secrets.h`.
---



---

## ▶️ 6. Setup & Usage
1. Upload Code
2. Open RFID_sketch_sep4a.ino in Arduino IDE.
3. Select board = ESP32 Dev Module.
4. Connect ESP32 → Upload.
5. Configure Secrets
6. Edit include/secret_sauce.h:

```cpp
const char* WIFI_SSID = "YourWiFi";
const char* WIFI_PASSWORD = "YourPassword";
const char* GAS_URL = "YourGoogleAppsScriptURL";
const char* SECRET_TOKEN = "YourSharedToken";
```
Now,
1. Start System
2. LCD shows System Ready – Tap a card.
3. Tap an RFID card → data is read.
4. ESP32 sends log to Google Sheets.
5. Serial Monitor
6. Open Serial Monitor at 115200.

Debug logs show Wi-Fi connection, card reads, offline log status.

---

## ## 🐞 Troubleshooting Guide

| Problem | Possible Cause | Solution |
|---------|----------------|----------|
| **`ledcSetup not declared` during compile** | Wrong board selected in Arduino IDE | Go to **Tools → Board → ESP32 Arduino → ESP32 Dev Module** |
| **LCD screen is blank** | (1) Wrong I2C address, (2) Loose wiring | Run an **I2C Scanner sketch** to detect address (commonly 0x27 or 0x3F). Fix SDA/SCL wiring. |
| **Invalid Token in Google Sheets** | Token in `secret_sauce.h` does not match Google Apps Script | Make sure `SECRET_TOKEN` is identical in both ESP32 code and Apps Script. |
| **Wi-Fi not connecting** | (1) Wrong SSID/Password, (2) Weak signal | Update Wi-Fi credentials in `secret_sauce.h`. Place ESP32 closer to router. |
| **RFID card not detected** | (1) Miswired MFRC522, (2) Faulty card | Double-check wiring: SDA → GPIO21, RST → GPIO22, MOSI → GPIO23, MISO → GPIO19, SCK → GPIO18. Try another card. |
| **Offline logs not syncing to Google Sheets** | Wi-Fi unstable during sync | Wait until Wi-Fi stabilizes. Logs auto-sync when Wi-Fi reconnects. Can also clear logs manually via LCD menu. |
| **Offline log memory full (50 entries)** | Queue limit reached | Increase `MAX_QUEUE` in code if needed. Or clear logs via LCD menu. |
| **Time not updating on LCD** | NTP sync failed | Ensure ESP32 has internet. Try `MENU → Set Time` option again. |
| **Buzzer not working** | Incorrect GPIO pin in code | Confirm `BUZZER_PIN` matches wiring (default GPIO15). Test by running a simple tone example. |
| **System freezes on boot** | Power supply unstable | Use a good quality USB cable and 5V/1A+ power adapter. |


---

## 📌 8. Future Improvements
1. Add admin master card for system management
2. Add RTC module (DS1307/DS3231) as backup clock
3. Expand offline queue beyond 50 logs
4. Add web dashboard for real-time monitoring
