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

## ⚡ Getting Started

### 1. Clone Repo
```bash
git clone https://github.com/<your-username>/ESP32-RFID-Attendance.git
cd ESP32-RFID-Attendance
