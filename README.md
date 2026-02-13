# ESP32-BMP280-Temperature-Monitor
ESP32 IoT project

## What It Does
- Reads temperature from BMP280 sensor every 500 milliseconds
- Sends data over WiFi to my computer
- Shows live temperature on a web dashboard

## Hardware Used
- ESP32 Development Board
- BMP280 Temperature Sensor
- Jumper wires

## Wiring
```
ESP32 GPIO21 → BMP280 SDA
ESP32 GPIO22 → BMP280 SCL
ESP32 3.3V   → BMP280 VCC
ESP32 GND    → BMP280 GND
```

## Files
- `BMP280project.cpp` - ESP32 firmware code
- `server.py` - Python Flask server
- `index.html` - Web dashboard

## How to Run

### ESP32
```bash
idf.py build flash monitor
```

### Server
```bash
python server.py
```

Then open: http://localhost:5000

## Course
CSYE 6550 - IoT/Embedded Development
Northeastern University
