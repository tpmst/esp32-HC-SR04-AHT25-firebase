#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// ===== PIN DEFINITIONS =====
#define LED_BUILTIN 2       // Onboard LED
#define I2C_SDA 7          // AHT25 SDA (I2C Data)
#define I2C_SCL 9          // AHT25 SCL (I2C Clock)

// RCWL-1670 GPIO Pins (Trigger/Echo Mode)
#define TRIGGER_PIN 21      // ESP Output -> RCWL Trig
#define ECHO_PIN 20        // ESP Input  -> RCWL Echo

// UART Pins for A7670E Modem (Serial1)
#define MODEM_RX_PIN 4      // ESP RX Pin -> Connected to A7670E TXD
#define MODEM_TX_PIN 5      // ESP TX Pin -> Connected to A7670E RXD

// Network Credentials
const char* ssid = "REPLACE_ME_WIFI_SSID";
const char* password = "REPLACE_ME_WIFI_PASSWORD";

// Firebase Credentials
#define FIREBASE_API_KEY "REPLACE_ME_FIREBASE_API_KEY"
#define FIREBASE_PROJECT_ID "REPLACE_ME_FIREBASE_PROJECT_ID"
#define FIREBASE_USER_EMAIL "REPLACE_ME_FIREBASE_USER_EMAIL"
#define FIREBASE_USER_PASSWORD "REPLACE_ME_FIREBASE_USER_PASSWORD"

// Deep Sleep Configuration
#define SLEEP_DURATION 21600 // 6 hours (in seconds)
#define uS_TO_S_FACTOR 1000000ULL

// RTC memory boot counter (retained during Deep Sleep)
RTC_DATA_ATTR int bootCount = 0;

// Sensor & System Objects
Adafruit_AHTX0 aht;
unsigned long currentUnixTime = 0;
String idToken = ""; // Stores temporary Auth token

// Forward declarations
void goToDeepSleep();
bool connectToWiFi();
bool getUnixTime();
bool refreshAuthToken();
bool uploadToFirestore(float distance, float temperature, float humidity);
float measureDistance();

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Initial startup check
  if (bootCount == 0) {
    Serial.println("Erster Systemstart erkannt. Warte 30 Sekunden...");
    bootCount++; // Increment counter so it doesn't delay on next wake-up
    delay(30000); 
    Serial.println("Wartezeit vorbei. Starte Messung...");
  } else {
    Serial.println("Aus Deep Sleep aufgewacht. Messung startet sofort.");
  }
  
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  
  pinMode(TRIGGER_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIGGER_PIN, LOW);
  
  Wire.begin(I2C_SDA, I2C_SCL);
  
  if (!aht.begin()) {
    Serial.println("ERROR: AHT25 nicht gefunden!");
    goToDeepSleep();
    return;
  }
  
  float distance = measureDistance();
  if (distance < 0) {
    Serial.println("ERROR: Distanzmessung fehlgeschlagen");
    goToDeepSleep();
    return;
  }
  
  sensors_event_t humidity, temp;
  aht.getEvent(&humidity, &temp);
  float temperature = temp.temperature;
  float humidityValue = humidity.relative_humidity;
  
  if (!connectToWiFi()) {
    Serial.println("ERROR: WiFi-Verbindung fehlgeschlagen");
    goToDeepSleep();
    return;
  }
  Serial.println("✓ WiFi verbunden");
  
  if (!getUnixTime()) {
    Serial.println("ERROR: Zeit-Synchronisation fehlgeschlagen");
    WiFi.disconnect(true);
    goToDeepSleep();
    return;
  }
  Serial.println("✓ Zeit synchronisiert");
  
  // Step 1: Authenticate with Firebase & retrieve token
  if (!refreshAuthToken()) {
    Serial.println("ERROR: Firebase-Authentifizierung fehlgeschlagen");
    WiFi.disconnect(true);
    goToDeepSleep();
    return;
  }
  Serial.println("✓ Firebase authentifiziert");
  
  // Step 2: Upload data with retrieved token
  if (uploadToFirestore(distance, temperature, humidityValue)) {
    Serial.println("✓ Daten gesendet");
  } else {
    Serial.println("ERROR: Daten-Upload fehlgeschlagen");
  }
  
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("✓ WiFi getrennt");
  
  goToDeepSleep();
}

void loop() {
  // Unused
}

bool connectToWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    attempts++;
  }
  return (WiFi.status() == WL_CONNECTED);
}

bool getUnixTime() {
  configTime(3600, 3600, "pool.ntp.org", "time.nist.gov", "time.google.com");
  int attempts = 0;
  while (attempts < 20) {
    time_t now = time(nullptr);
    if (now > 1000000000) {
      currentUnixTime = now;
      return true;
    }
    delay(500);
    attempts++;
  }
  return false;
}

// Retrieves ID token from Google Auth via REST API
bool refreshAuthToken() {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  
  String url = "https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=" + String(FIREBASE_API_KEY);
  
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  
  String authPayload = "{\"email\":\"" + String(FIREBASE_USER_EMAIL) + "\",\"password\":\"" + String(FIREBASE_USER_PASSWORD) + "\",\"returnSecureToken\":true}";
  
  int httpResponseCode = http.POST(authPayload);
  
  if (httpResponseCode == 200) {
    String response = http.getString();
    
    int tokenIndex = response.indexOf("\"idToken\": \"");
    if (tokenIndex != -1) {
      tokenIndex += 12;
      int endIndex = response.indexOf("\"", tokenIndex);
      idToken = response.substring(tokenIndex, endIndex);
      http.end();
      return true;
    }
  }
  
  Serial.print("Auth-Fehler Code: ");
  Serial.println(httpResponseCode);
  http.end();
  return false;
}

bool uploadToFirestore(float distance, float temperature, float humidity) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  
  String bearerHeader = "Bearer " + idToken;
  
  String jsonPayload = "{\"fields\":{"
    "\"distance\":{\"doubleValue\":" + String(distance, 2) + "},"
    "\"temperature\":{\"doubleValue\":" + String(temperature, 2) + "},"
    "\"humidity\":{\"doubleValue\":" + String(humidity, 2) + "},"
    "\"timestamp\":{\"integerValue\":\"" + String(currentUnixTime) + "\"}"
  "}}";

  String url = "https://firestore.googleapis.com/v1/projects/" + String(FIREBASE_PROJECT_ID) + "/databases/(default)/documents/measurements?documentId=" + String(currentUnixTime);
  
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", bearerHeader.c_str());
  
  int httpResponseCode = http.POST(jsonPayload);
  Serial.print("Firestore New Doc Response Code: ");
  Serial.println(httpResponseCode);
  http.end();
  
  String latestUrl = "https://firestore.googleapis.com/v1/projects/" + String(FIREBASE_PROJECT_ID) + "/databases/(default)/documents/measurements/latest?updateMask.fieldPaths=distance&updateMask.fieldPaths=temperature&updateMask.fieldPaths=humidity&updateMask.fieldPaths=timestamp";
  
  http.begin(client, latestUrl);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", bearerHeader.c_str());
  
  int latestResponseCode = http.PATCH(jsonPayload);
  Serial.print("Firestore Latest Patch Response Code: ");
  Serial.println(latestResponseCode);
  http.end();
  
  return (httpResponseCode == 200);
}

float measureDistance() {
  const int NUM_MEASUREMENTS = 3;
  float measurements[NUM_MEASUREMENTS];
  int validMeasurements = 0;
  
  for (int i = 0; i < NUM_MEASUREMENTS; i++) {
    digitalWrite(TRIGGER_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIGGER_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIGGER_PIN, LOW);
    
    unsigned long timeout = millis() + 1000;
    while (digitalRead(ECHO_PIN) == LOW) {
      if (millis() > timeout) { measurements[i] = -1; break; }
    }
    if (measurements[i] == -1) continue;
    
    unsigned long startTime = micros();
    while (digitalRead(ECHO_PIN) == HIGH) {
      if (millis() > timeout) { measurements[i] = -1; break; }
    }
    if (measurements[i] == -1) continue;
    
    unsigned long endTime = micros();
    unsigned long duration = endTime - startTime;
    float distance = (duration * 0.0343) / 2.0;
    
    if (distance >= 2.0 && distance <= 400.0) {
      measurements[i] = distance;
      validMeasurements++;
    } else {
      measurements[i] = -1;
    }
    delay(100);
  }
  
  if (validMeasurements == 0) return -1;
  
  float sum = 0;
  for (int i = 0; i < NUM_MEASUREMENTS; i++) {
    if (measurements[i] > 0) sum += measurements[i];
  }
  return sum / validMeasurements;
}

void goToDeepSleep() {
  Serial.println("→ Deep Sleep");
  delay(100);
  esp_sleep_enable_timer_wakeup(SLEEP_DURATION * uS_TO_S_FACTOR);
  esp_deep_sleep_start();
}
