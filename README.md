# ESP32 heating and sensor firmware

This repository contains Arduino sketches for ESP32 boards. The sketches cover
two related use cases:

* measuring distance with an HC-SR04 or RCWL-1670 ultrasonic sensor, optionally
  together with an AHT25/AHT20-compatible temperature and humidity sensor; and
* reading a locally reachable ETA heating controller over its HTTP/XML API and
  writing the result to Firebase Firestore for the
  [pellet-heizung-frontend](https://github.com/tpmst/pellet-heizung-frontend)
  dashboard.

Each `.ino` file is a separate example. Compile and upload only the sketch
matching the connected hardware and configured service.

## Supported sketches and hardware

| Sketch | Hardware and behavior |
| --- | --- |
| `mesure-distance-with-wifi.ino` | HC-SR04 distance monitor over Wi-Fi; measurements are printed to Serial every 30 seconds and are not uploaded to Firebase. Uses GPIO 5 (trigger) and GPIO 18 (echo). |
| `mesure-distance-firebase.ino` | HC-SR04 distance measurements uploaded to Firestore using the Firebase ESP Client library. Uses GPIO 5 (trigger) and GPIO 18 (echo), then sleeps for six hours. |
| `mesure-all-firebase-with-timestamp.ino` | HC-SR04 plus AHT25/AHT20-compatible temperature and humidity sensor. Uses GPIO 20/21 for trigger/echo and GPIO 7/9 for I2C, then uploads to Firestore and sleeps for six hours. |
| `mesure-distance-rcwl.ino` | RCWL-1670 in trigger/echo mode plus AHT25/AHT20-compatible temperature and humidity sensor. Uses GPIO 21/20 for trigger/echo and GPIO 7/9 for I2C. The sketch contains A7670E serial pin definitions, but its upload path uses Wi-Fi and Firebase REST authentication. |
| `eta-api-connector.ino` | ETA heating API connector. It does not measure the ultrasonic or climate sensors; it polls the ETA controller over local HTTP and uploads selected XML responses to Firestore. It checks every five minutes and sleeps between checks. |

The code targets a standard ESP32 Arduino environment. Pin assignments are
defined in each sketch and may need to be changed for a different ESP32 board.

## Wiring

### HC-SR04 (pulse trigger/echo)

The Firebase and local Wi-Fi HC-SR04 examples use the pin assignments shown
below:

* VCC -> 5 V (or the voltage specified by the sensor)
* GND -> GND
* Trigger -> GPIO 5
* Echo -> GPIO 18

`mesure-all-firebase.ino` uses GPIO 20 for trigger and GPIO 21 for echo
instead. The ESP32 is a 3.3 V device: level-shift a 5 V echo signal before it
reaches an ESP32 input.

### RCWL-1670 and AHT25

`mesure-distance-rcwl.ino` uses:

* RCWL-1670 trigger -> GPIO 21
* RCWL-1670 echo -> GPIO 20
* AHT25/AHT20 SDA -> GPIO 7
* AHT25/AHT20 SCL -> GPIO 9
* sensor VCC/GND -> the module's specified supply and common GND

`mesure-all-firebase-with-timestamp.ino` uses the same I2C pins and uses GPIO
20/21 for its HC-SR04.

### A7670E wiring in the RCWL example

The RCWL sketch defines `MODEM_RX_PIN` as GPIO 4 and `MODEM_TX_PIN` as GPIO 5
for a possible A7670E connection. The current sketch does not initialize or
use the modem; do not treat it as an active cellular fallback.

## ETA API connector

`eta-api-connector.ino` is the firmware used to bridge an ETA heating
controller that is reachable from the ESP32's local network to Firestore:

1. The ESP32 joins Wi-Fi.
2. It requests XML from the ETA controller at `http://<etaHost>:8080`.
3. It parses the `strValue` attribute from the status response to classify the
   heating as on or off.
4. On a status change, it updates the status document and uploads all four XML
   responses. Otherwise, it uploads the full set when the 12-hour interval is
   reached.
5. It disconnects Wi-Fi and enters five-minute deep sleep.

### Connector configuration

Edit the empty constants near the top of `eta-api-connector.ino` before
uploading:

```cpp
const char* ssid = "your-wifi-network";
const char* password = "your-wifi-password";

#define FIRESTORE_HOST "firestore.googleapis.com"
#define FIREBASE_PROJECT_ID "your-project-id"
#define SECRET_KEY "your-connector-secret"

const String etaHost = "192.168.1.100";
const int etaPort = 8080;
```

The connector requests these ETA paths:

* `/user/var/264/10891/0/0/12080` (`pathStatus`)
* `/user/var/264/10891/0/0/12013` (`pathAsche`)
* `/user/var/264/10891/0/0/12016` (`pathMenge`)
* `/user/errors` (`pathErrors`)

The status is considered off when `strValue` is `Aus` or `Abstellen`;
other parsed values are treated as on. The first boot is treated as a status
change. The regular full-upload interval is `43200` seconds (12 hours), and
the deep-sleep interval is `300` seconds (five minutes). A status change can
therefore cause an immediate full upload rather than waiting for the 12-hour
interval.

## Firestore data written by the ETA connector

The connector uses the Firestore REST API and writes to the `heizung`
collection:

* `heizung/status_heizung` is patched when the on/off state changes. It
  contains `ist_an` (boolean), `letzte_aenderung` (Unix time integer), and
  `secretKey` (string).
* `heizung/<unix-time>` is created for a status-triggered or 12-hour full
  upload. It contains `xml_status`, `xml_asche`, `xml_menge`, and `xml_errors`
  (the raw XML responses as strings), plus `ist_an` (boolean), `timestamp`
  (Unix time integer), and `secretKey` (string).

Document IDs for full uploads are the Unix timestamp returned after NTP
synchronization. The frontend reads these documents to display the ETA
heating state and history; the frontend repository is not built or deployed
by this firmware project.

The other Firebase examples use a separate `measurements` collection. They
write distance and, where applicable, temperature, humidity, and timestamp
fields, and update `measurements/latest`. They are independent of the
ETA-specific `heizung` documents.

## Wi-Fi, Firebase, and secrets

The sketches contain placeholder values for Wi-Fi and Firebase configuration.
Replace them locally before compiling:

* ETA connector: `ssid`, `password`, `FIRESTORE_HOST`,
  `FIREBASE_PROJECT_ID`, and `SECRET_KEY` in `eta-api-connector.ino`.
* Firebase sensor examples: the Wi-Fi credentials and Firebase API key,
  project ID, user email, and user password defined in the selected sketch.

Do not commit real Wi-Fi passwords, Firebase credentials, API keys, or
connector secrets. Keep local values in an ignored/private configuration or
restore placeholders before committing. The ETA connector currently embeds
`SECRET_KEY` as a Firestore field and calls `WiFiClientSecure::setInsecure()`;
this is the behavior of the current firmware, not a replacement for proper
Firestore authentication or certificate validation. Restrict Firestore rules
and network access accordingly, and treat the stored secret as exposed to
anyone who can read the document.

The Firebase sensor sketches authenticate with a Firebase email/password and
send the resulting ID token in the Firestore REST request (or use
`Firebase_ESP_Client` in `mesure-distance-firebase.ino`). Install the
corresponding ESP32 core and library dependencies in Arduino IDE or
PlatformIO, including `Adafruit_AHTX0` for the AHT sensor sketches and the
Firebase ESP Client library for `mesure-distance-firebase.ino`.

## 3D-printed enclosures

### HC-SR04 enclosure

<img width="527" height="662" alt="HC-SR04 enclosure" src="https://github.com/user-attachments/assets/073a582e-577a-4d86-93b4-127253ba2f39" />
<img width="790" height="642" alt="HC-SR04 enclosure" src="https://github.com/user-attachments/assets/f9e592eb-6857-463a-8dbb-0c1acc432e50" />
<img width="749" height="643" alt="HC-SR04 enclosure" src="https://github.com/user-attachments/assets/6ebf0214-24ec-4b1e-8440-d2ba425ea50d" />

### RCWL-1670 enclosure

<img width="1062" height="746" alt="RCWL-1670 enclosure" src="https://github.com/user-attachments/assets/33d69839-0a9a-4ed6-84c2-53312cb3b307" />
<img width="911" height="579" alt="RCWL-1670 enclosure" src="https://github.com/user-attachments/assets/e85e13e2-ffa4-4492-a19e-380194664d0d" />
<img width="890" height="575" alt="RCWL-1670 enclosure" src="https://github.com/user-attachments/assets/1775b5ef-e1d6-4e0c-b435-18235bba3379" />
<img width="866" height="542" alt="RCWL-1670 enclosure" src="https://github.com/user-attachments/assets/f46cebfd-67d3-4c09-a507-9337d41aa5df" />
