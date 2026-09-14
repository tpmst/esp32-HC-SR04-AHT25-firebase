#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>

// ===== PIN DEFINITIONS =====
#define LED_BUILTIN 2        // Onboard LED

// ===== WIFI CREDENTIALS =====
const char* ssid     = "";     
const char* password = "";    

// ===== FIREBASE CONFIGURATION =====
#define FIRESTORE_HOST ""
#define FIREBASE_PROJECT_ID ""
#define SECRET_KEY ""

// ===== ETA HEIZUNG CONFIGURATION =====
const String etaHost = ""; // IP anpassen!
const int    etaPort = 8080;
const String pathStatus   = "/user/var/264/10891/0/0/12080"; 
const String pathAsche    = "/user/var/264/10891/0/0/12013"; 
const String pathMenge    = "/user/var/264/10891/0/0/12016"; 
const String pathErrors   = "/user/errors";

// Deep Sleep Konfiguration (Prüfung alle 5 Minuten = 300 Sekunden)
#define SLEEP_DURATION 300
#define uS_TO_S_FACTOR 1000000ULL

// Intervall für den regelmäßigen Upload (z. B. alle 12 Stunden in Sekunden = 43200)
#define UPLOAD_INTERVAL_SEC 43200 

// RTC-Speicher (bleibt im Deep Sleep erhalten)
RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR bool lastIsAn = true;
RTC_DATA_ATTR unsigned long lastUploadEpoch = 0;
RTC_DATA_ATTR bool firstRun = true;

unsigned long currentUnixTime = 0;

// Declarations
void goToDeepSleep();
void setupWiFi();
void fetchUnixTime();
String getEtaValue(String subPath);
String parseXmlStrValue(String xml);
String escapeJsonString(String str);
bool uploadToFirestoreHTTPS(String url, String jsonPayload);
bool updateStatusDocument(bool isAn);
bool uploadAllData(String xmlStatus, String xmlAsche, String xmlMenge, String xmlErrors, bool isAn);

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // 30 Sekunden Pause nur beim allerersten Start
  if (bootCount == 0 && firstRun) {
    Serial.println("Erster Systemstart erkannt. Warte 30 Sekunden...");
    delay(30000); 
    Serial.println("Wartezeit vorbei. Starte System...");
  }
  
  Serial.print("Aufgewacht. Boot-Zähler: ");
  Serial.println(bootCount);
  
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // WLAN starten, um lokale Daten von der Heizung auszulesen
  setupWiFi();

  Serial.println("Lese Status von ETA-Heizung...");
  
  // Status abrufen und parsen, um zu prüfen, ob sie an/aus ist
  String xmlStatus = getEtaValue(pathStatus);
  String statusText = parseXmlStrValue(xmlStatus);

  bool isAn = true;
  if (statusText.equalsIgnoreCase("Aus") || statusText.equalsIgnoreCase("Abstellen")) {
    isAn = false;
  }

  // Zeit holen
  fetchUnixTime();

  // Prüfen, ob sich der Ein/Aus-Status geändert hat
  bool statusChanged = (firstRun) || (isAn != lastIsAn);
  bool intervalReached = (currentUnixTime >= lastUploadEpoch + UPLOAD_INTERVAL_SEC);

  // Wenn sich der Status geändert hat, aktualisieren wir das separate Dokument "status_heizung"
  if (statusChanged) {
    Serial.println("-> Status-Änderung (Ein/Aus) erkannt! Aktualisiere Dokument 'status_heizung'...");
    if (updateStatusDocument(isAn)) {
      Serial.println("✓ Status-Dokument erfolgreich aktualisiert!");
    } else {
      Serial.println("❌ Fehler beim Aktualisieren des Status-Dokuments.");
    }
  }

  // Bedingungen für den großen XML-Daten-Upload prüfen
  if (statusChanged || intervalReached) {
    if (statusChanged) {
      Serial.println("-> Lade alle XML-Daten wegen Statusänderung hoch...");
    } else {
      Serial.println("-> Regelmäßiges Zeitintervall (12h) erreicht! Lade alle XML-Daten hoch...");
    }

    // Restliche XML-Daten abrufen
    delay(2000);
    String xmlAsche  = getEtaValue(pathAsche);
    delay(2000);
    String xmlMenge  = getEtaValue(pathMenge);
    delay(2000);
    String xmlErrors = getEtaValue(pathErrors);

    // Hochladen der Messdaten in Firestore
    if (uploadAllData(xmlStatus, xmlAsche, xmlMenge, xmlErrors, isAn)) {
      Serial.println("✓ ETA-XML-Messdaten erfolgreich in Firestore gespeichert!");
      
      // Zustände im RTC-Speicher aktualisieren
      lastIsAn = isAn;
      lastUploadEpoch = currentUnixTime;
      firstRun = false;
    } else {
      Serial.println("ERROR: Messdaten-Upload fehlgeschlagen");
    }
  } else {
    Serial.println("Keine Statusänderung und Intervall noch nicht erreicht. Kein Messdaten-Upload.");
  }

  // Zähler erhöhen
  bootCount++;

  // WLAN trennen und ab in den Deep Sleep
  WiFi.disconnect(true);
  goToDeepSleep();
}

void loop() { }

void setupWiFi() {
  Serial.println("Verbinde mit WLAN...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(1000);
    Serial.print(".");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✓ WiFi Verbunden!");
  } else {
    Serial.println("\n❌ WLAN Verbindung fehlgeschlagen!");
  }
}

void fetchUnixTime() {
  configTime(3600, 3600, "pool.ntp.org", "time.nist.gov");
  
  Serial.println("Warte auf NTP Zeit-Synchronisation...");
  struct tm timeinfo;
  int retry = 0;
  while (!getLocalTime(&timeinfo) && retry < 15) {
    delay(500);
    Serial.print(".");
    retry++;
  }
  
  if (retry >= 15) {
    currentUnixTime = 1700000000; // Fallback
  } else {
    currentUnixTime = (unsigned long)mktime(&timeinfo);
  }
}

String getEtaValue(String subPath) {
  HTTPClient http;
  String url = "http://" + etaHost + ":" + String(etaPort) + subPath;
  String payload = "";
  
  if (http.begin(url)) {
    http.setTimeout(4000);
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
      payload = http.getString();
    } else {
      Serial.print("  -> HTTP Fehler, Code: ");
      Serial.println(httpCode);
    }
    http.end();
  }
  return payload;
}

String parseXmlStrValue(String xml) {
  int idx = xml.indexOf("strValue=\"");
  if (idx == -1) return "0";
  int start = idx + 10;
  int end = xml.indexOf("\"", start);
  if (end == -1) return "0";
  return xml.substring(start, end);
}

String escapeJsonString(String str) {
  str.replace("\\", "\\\\");
  str.replace("\"", "\\\"");
  str.replace("\n", "");
  str.replace("\r", "");
  return str;
}

// Spezielles Dokument für den Ein/Aus-Status: "status_heizung"
bool updateStatusDocument(bool isAn) {
  // Wir nutzen hier PATCH statt POST, damit das Dokument "status_heizung" bei jeder Änderung überschrieben/aktualisiert wird
  String url = "https://" + String(FIRESTORE_HOST) + "/v1/projects/" + String(FIREBASE_PROJECT_ID) + "/databases/(default)/documents/heizung/status_heizung";
  
  String stringIsAn = isAn ? "true" : "false";

  String jsonPayload = "{\"fields\":{"
    "\"ist_an\":{\"booleanValue\":" + stringIsAn + "},"
    "\"letzte_aenderung\":{\"integerValue\":\"" + String(currentUnixTime) + "\"},"
    "\"secretKey\":{\"stringValue\":\"" + String(SECRET_KEY) + "\"}"
  "}}";

  // PATCH-Methode erzwingen für das feste Dokument
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (http.begin(client, url)) {
    http.setTimeout(5000);
    http.addHeader("Content-Type", "application/json");
    
    // PATCH aktualisiert das existierende Dokument direkt
    int httpResponseCode = http.sendRequest("PATCH", (uint8_t*)jsonPayload.c_str(), jsonPayload.length());
    
    Serial.print("Firestore Status-Doc HTTP Response Code: ");
    Serial.println(httpResponseCode);

    bool success = (httpResponseCode == 200);
    http.end();
    return success;
  }
  return false;
}

// Regulärer Upload der XML-Messdaten (mit Timestamp als Dokument-ID)
bool uploadAllData(String xmlStatus, String xmlAsche, String xmlMenge, String xmlErrors, bool isAn) {
  String url = "https://" + String(FIRESTORE_HOST) + "/v1/projects/" + String(FIREBASE_PROJECT_ID) + "/databases/(default)/documents/heizung?documentId=" + String(currentUnixTime);
  
  String stringIsAn = isAn ? "true" : "false";

  String safeStatus  = escapeJsonString(xmlStatus);
  String safeAsche   = escapeJsonString(xmlAsche);
  String safeMenge   = escapeJsonString(xmlMenge);
  String safeErrors  = escapeJsonString(xmlErrors);

  String jsonPayload = "{\"fields\":{"
    "\"xml_status\":{\"stringValue\":\"" + safeStatus + "\"},"
    "\"xml_asche\":{\"stringValue\":\"" + safeAsche + "\"},"
    "\"xml_menge\":{\"stringValue\":\"" + safeMenge + "\"},"
    "\"xml_errors\":{\"stringValue\":\"" + safeErrors + "\"},"
    "\"ist_an\":{\"booleanValue\":" + stringIsAn + "},"
    "\"timestamp\":{\"integerValue\":\"" + String(currentUnixTime) + "\"},"
    "\"secretKey\":{\"stringValue\":\"" + String(SECRET_KEY) + "\"}"
  "}}";

  return uploadToFirestoreHTTPS(url, jsonPayload);
}

// Allgemeine HTTP-POST Funktion
bool uploadToFirestoreHTTPS(String url, String payload) {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (http.begin(client, url)) {
    http.setTimeout(5000);
    http.addHeader("Content-Type", "application/json");
    int httpResponseCode = http.POST(payload);
    
    Serial.print("Firestore HTTP Response Code: ");
    Serial.println(httpResponseCode);

    bool success = (httpResponseCode == 200 || httpResponseCode == 201);
    http.end();
    return success;
  }
  return false;
}

void goToDeepSleep() {
  Serial.println("Gehe in den Deep Sleep für 5 Minuten...");
  esp_sleep_enable_timer_wakeup(SLEEP_DURATION * uS_TO_S_FACTOR);
  esp_deep_sleep_start();
}
