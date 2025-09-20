# ESP32 RFID Attendance System 🎓

A smart, low-cost attendance system designed for academic departments.  
Built on **ESP32 + RFID + LCD + Google Sheets**, with **offline logging, live people count, and easy menu navigation**.

---

## ✨ Features

- 🔐 **RFID Authentication** → Reads Name + ID from pre-registered RFID cards.  
- 📡 **Google Sheets Integration** → Logs UID, Name, ID, Date, Time, and Check-In/Out in real time.  
- 📴 **Offline Mode** → Stores up to 50 entries if WiFi is down; auto-syncs on reconnect.  
- ⏳ **Per-Card Cooldown** → Prevents multiple logs within 5 minutes.  
- 📟 **LCD Interface** → Displays real-time status, scrolling names, system feedback, and live count.  
- 🎛 **Menu Navigation** →
  - View stored logs  
  - Check current inside count  
  - Clear offline logs  
  - Sync time manually  
- 🎶 **Buzzer Alerts** → Audio feedback for success/failure.  
- 🧮 **Live People Count** → Tracks how many are inside at any given time.  
- ⚡ **Robust Design** → Recovers gracefully from network drops.  

---
## 🖥️ Tech Stack  

- **Language** → C++ (Arduino IDE) ![C++](https://img.shields.io/badge/code-C++-blue)  
- **Hardware** → ESP32, MFRC522 RFID, 16x2 LCD ![Arduino](https://img.shields.io/badge/framework-Arduino-green) ![ESP32](https://img.shields.io/badge/hardware-ESP32-orange)  
- **Integration** → Google Apps Script + Sheets ![Google Sheets](https://img.shields.io/badge/integration-Google%20Sheets-yellow)  
- **Libraries** → WiFi, HTTPClient, MFRC522, LiquidCrystal_I2C, NTPClient, Preferences
- **RFID Card** → MIFARE Classic 1K, 13.56 MHz, 1KB memory ![RFID](https://img.shields.io/badge/type-MIFARE%20Classic%201K-blueviolet)
 

---
## ✅ Pros
- **Automation** → No manual registers, seamless digital log.  
- **Low Maintenance** → LCD + buttons make it standalone, no PC needed.  
- **Scalable** → Expandable to multiple readers across rooms.  
- **User-Friendly** → Simple interface, no training required.  
- **Reliable** → Works online + offline, no data loss.  

---

## ⚠️ Cons
- Requires initial setup (RFID cards, wiring ESP32, LCD, RFID module).  
- Needs Google Apps Script for Google Sheets integration.  
- Offline storage capped at 50 entries (tweakable).  
- Some Arduino/ESP32 libraries may conflict after updates.  

---

## 🛠️ Hardware Requirements
- ESP32 Dev Board  
- MFRC522 RFID Module  
- 16x2 LCD with I2C  
- Push buttons (Menu, Select, Back)  
- Buzzer  
- Breadboard + jumper wires + power source  

---

## 📐 Circuit Diagram
![Circuit Diagram](docs/circuit-diagram.png)  

---
