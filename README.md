# ESP32 HC-SR04 + AHT25 → Firebase

This project contains example sketches for an ESP32 that measure distance with an HC-SR04 ultrasonic sensor and temperature/humidity with an AHT2x (AHT25/AHT20-compatible) sensor and upload the measurements to Google Firebase (Firestore).

The repo includes several Arduino/ESP32 sketches:
- `mesure-all-firebase-with-timestamp.ino` — measures distance, temperature and humidity, gets an NTP timestamp and uploads to Firestore with a Unix timestamp as document ID.
- `mesure-distance-firebase.ino` — measures only the distance and uploads a document identified by date-time.
- `mesure-distance-with-wifi.ino` — simple distance measurement example that keeps WiFi connected and prints to serial (useful for debugging/wifi testing).

## Hardware
- ESP32 development board (any common variant)
- HC-SR04 ultrasonic distance sensor
- AHT2x I2C temperature & humidity sensor (AHT25 or AHT20 compatible breakout)
- Breadboard and jumper wires
- 3.3V power supply (ESP32 runs at 3.3V; if you power HC-SR04 from 5V take care with ESP32 I/O levels)

Wiring (typical):
- HC-SR04
  - VCC → 5V (or 3.3V depending on module) — prefer 5V modules; if using 5V, ensure Echo is level-shifted before connecting to ESP32 input
  - GND → GND
  - TRIG → ESP32 GPIO 5 (or change in code)
  - ECHO → ESP32 GPIO 18 (or change)
- AHT2x (I2C)
  - VCC → 3.3V
  - GND → GND
  - SDA → ESP32 SDA pin (default in `mesure-all-firebase-with-timestamp.ino` is GPIO 21)
  - SCL → ESP32 SCL pin (default GPIO 22)

Adjust pins in the sketches if you use different GPIOs.

## Required Libraries
Install the following Arduino / PlatformIO libraries before compiling:
- Firebase ESP Client (e.g. Firebase_ESP_Client) — for Firestore integration
- Adafruit AHTX0 (Adafruit_AHTX0) — for AHT2x sensor
- ArduinoJson or FirebaseJson — the Firebase client may require this (the included sketches use FirebaseJson)

You can install libraries in the Arduino IDE via Sketch → Include Library → Manage Libraries, or with PlatformIO's lib_deps.

## Firebase Setup (Firestore)
Follow these minimal steps to create a Firebase project and make Firestore usable from the ESP32 sketches:

1. Create Firebase project
   - Go to https://console.firebase.google.com/ and create a new project (give it a name).

2. Enable Firestore
   - In the project console, open 'Firestore Database' and create a database (in production choose rules accordingly, for testing you can use test mode but secure it later).

3. Create a Web API Key & Service Account (for REST usage)
   - In Project Settings → General you'll find the Web API Key. Copy it — in the sketches it's the `FIREBASE_API_KEY` placeholder.
   - For authentication, the Arduino Firebase client often uses email/password sign-in (create a test user in Authentication → Users) or a custom token. The provided sketches expect an email/password user:
     - Go to Authentication → Sign-in method and enable Email/Password.
     - Then create a user under Authentication → Users. Use this email and password in the sketch (`FIREBASE_USER_EMAIL`, `FIREBASE_USER_PASSWORD`).

4. Project ID
   - In Project Settings → General you can find the Project ID (use as `FIREBASE_PROJECT_ID`).

5. Rules and security
   - For initial testing you can set the Firestore rules to allow reads/writes while you debug, but lock them down before production. Example test rule (not for production):

```
service cloud.firestore {
  match /databases/{database}/documents {
    match /{document=**} {
      allow read, write: if true;
    }
  }
}
```

Note: The Firebase REST/embedded libraries have different auth flows — the sketches in this repo use the Firebase Arduino client that handles authentication with email/password; if you'd prefer to use a service account/key you must adapt the library and token flow.

## Configuration
1. Open the sketch you want to use in the Arduino IDE or PlatformIO.
2. Fill in your WiFi credentials (`ssid`, `password`).
3. Fill in Firebase placeholders in the sketch:
   - `FIREBASE_API_KEY`
   - `FIREBASE_PROJECT_ID`
   - `FIREBASE_USER_EMAIL`
   - `FIREBASE_USER_PASSWORD`
4. Adjust pin definitions if your wiring differs.

## Build & Upload
- Using Arduino IDE: select the correct ESP32 board (Tools → Board), select the correct port, then compile and upload.
- Using PlatformIO: configure `platformio.ini` for an ESP32 board and build/upload.

## Troubleshooting
- If AHT sensor not found: check wiring (SDA/SCL), VCC (3.3V) and that you set correct SDA/SCL pins in `Wire.begin()` if using custom pins.
- If HC-SR04 returns invalid values: verify TRIG/ECHO wiring, ensure correct power (some modules need 5V), and consider adding a level shifter on ECHO to ESP32.
- WiFi problems: check SSID/password, serial logs often show status. Reconnect logic is included in the WiFi example.
- Firebase auth errors: verify API key, Project ID and that the email/password user exists and is enabled.

## Notes and next steps
- Consider switching to secure token-based authentication or Cloud Functions for heavy usage.
- Add error logging and retry/backoff for network and Firebase operations.
- Add OTA updates if you want remote firmware updates.

---
If you want, I can:
- Add a wiring diagram image (ask and I’ll produce a simple SVG/PDF),
- Fix small code issues (e.g. `=` vs `==` in `connectToWiFi()` in `mesure-distance-with-wifi.ino`) and run a quick compile check (if you have your board and toolchain).