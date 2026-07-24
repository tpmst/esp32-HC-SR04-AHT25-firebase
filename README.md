# ESP32 Distance & Climate Monitor → Firebase

This repository contains example firmware for an ESP32 micro-controller that measures distance using an **HC-SR04** or **RCWL-1670** ultrasonic sensor, and ambient temperature/humidity using an **AHT2x** (AHT25/AHT20-compatible) sensor. Collected measurements are automatically uploaded to Google Firebase (Firestore).

The system supports dual connectivity: it attempts to upload data over a cellular network using an **A7670E LTE module** and automatically falls back to **Wi-Fi** if no cellular connection is available.

---

## Features & Supported Hardware

* **Microcontroller:** ESP32 development board (any standard variant).
* **Ultrasonic Distance Sensors:**
  * **HC-SR04** (Pulse Trigger/Echo mode).
  * **RCWL-1670** (Waterproof, long-range ultrasonic sensor; supports Pulse Trigger/Echo or UART mode).
* **Climate Sensor:** **AHT2x** I2C temperature & humidity sensor (AHT25 / AHT20).
* **Cellular Modem (Optional):** **A7670E** LTE Cat-1 module (communicates over Hardware Serial using AT commands).
* **Power Optimization:** Configured with Deep Sleep functionality (wakes up, measures, uploads data, turns off modem, and re-enters sleep).

---

## Wiring Guide

### 1. Distance Sensors (Pulse Mode)
* **VCC** $\rightarrow$ 5V (or 3.3V depending on module specifications)
* **GND** $\rightarrow$ GND
* **TRIG** $\rightarrow$ ESP32 GPIO 6
* **ECHO** $\rightarrow$ ESP32 GPIO 8

> **Note:** If powering 5V sensors, ensure the `ECHO` line is stepped down or level-shifted to 3.3V to protect the ESP32 input pin.

### 2. Climate Sensor (AHT25)
* **VCC** $\rightarrow$ 3.3V
* **GND** $\rightarrow$ GND
* **SDA** $\rightarrow$ ESP32 GPIO 7 (Default I2C Data)
* **SCL** $\rightarrow$ ESP32 GPIO 9 (Default I2C Clock)

### 3. Cellular Modem (A7670E)
* **VCC** $\rightarrow$ 5V – 10V (Must support peak current bursts up to 2A)
* **GND** $\rightarrow$ Common GND
* **TXD** $\rightarrow$ ESP32 GPIO 20 (`Serial1` RX)
* **RXD** $\rightarrow$ ESP32 GPIO 21 (`Serial1` TX)

---

## 3D Printed Enclosures
### HC-SR04 Enclosure

<img width="527" height="662" alt="image" src="https://github.com/user-attachments/assets/073a582e-577a-4d86-93b4-127253ba2f39" />
<img width="790" height="642" alt="image" src="https://github.com/user-attachments/assets/f9e592eb-6857-463a-8dbb-0c1acc432e50" />
<img width="749" height="643" alt="image" src="https://github.com/user-attachments/assets/6ebf0214-24ec-4b1e-8440-d2ba425ea50d" />

### RCWL-1670 Enclosure

<img width="1062" height="746" alt="image" src="https://github.com/user-attachments/assets/33d69839-0a9a-4ed6-84c2-53312cb3b307" />
<img width="911" height="579" alt="image" src="https://github.com/user-attachments/assets/e85e13e2-ffa4-4492-a19e-380194664d0d" />
<img width="890" height="575" alt="image" src="https://github.com/user-attachments/assets/1775b5ef-e1d6-4e0c-b435-18235bba3379" />
<img width="866" height="542" alt="image" src="https://github.com/user-attachments/assets/f46cebfd-67d3-4c09-a507-9337d41aa5df" />


## Software Setup

### Required Libraries
Install these libraries in your Arduino IDE or PlatformIO project:
* **Adafruit AHTX0** (`Adafruit_AHTX0`) — for reading the AHT25 sensor.
* **WiFiClientSecure** & **HTTPClient** — included in the default ESP32 core for REST API interactions.

### Configuration
1. Open the project sketch in Arduino IDE or PlatformIO.
2. Update network credentials and Firebase constants in the code:

```cpp
const char* ssid = "REPLACE_ME_WIFI_SSID";
const char* password = "REPLACE_ME_WIFI_PASSWORD";

#define FIREBASE_API_KEY "REPLACE_ME_FIREBASE_API_KEY"
#define FIREBASE_PROJECT_ID "REPLACE_ME_FIREBASE_PROJECT_ID"
#define FIREBASE_USER_EMAIL "REPLACE_ME_FIREBASE_USER_EMAIL"
#define FIREBASE_USER_PASSWORD "REPLACE_ME_FIREBASE_USER_PASSWORD"
