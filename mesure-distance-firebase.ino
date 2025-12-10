#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// Pin definitions
#define TRIGGER_PIN 5
#define ECHO_PIN 18
#define LED_BUILTIN 2 // Onboard LED Pin

// WiFi-Credentials
const char *ssid = "network SSID";
const char *password = "networkpassword";

// Firebase-Credentials
#define FIREBASE_API_KEY "YOUR_API_KEY"
#define FIREBASE_PROJECT_ID "YOUR_PROJECT_ID"
#define FIREBASE_USER_EMAIL "YOUR_EMAIL"
#define FIREBASE_USER_PASSWORD "YOUR_PASSWORD"

// Deep Sleep configuration
#define SLEEP_DURATION 21600 // seconds (6 hours)
#define uS_TO_S_FACTOR 1000000ULL

// Firebase objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// RTC Memory
RTC_DATA_ATTR int bootCount = 0;

bool firebaseReady = false;
String currentDateTime = "";

void setup()
{
    Serial.begin(115200);
    delay(1000);

    bootCount++;

    // Disable onboard LED
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);

    pinMode(TRIGGER_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    digitalWrite(TRIGGER_PIN, LOW);

    // Measure distance
    float distance = measureDistance();

    if (distance < 0)
    {
        Serial.println("ERROR: Measurement failed");
        goToDeepSleep();
        return;
    }

    // Connect to WiFi
    if (!connectToWiFi())
    {
        Serial.println("ERROR: WiFi connection failed");
        goToDeepSleep();
        return;
    }

    Serial.println("✓ WiFi connected");

    // Sync NTP time
    syncTime();

    // Initialize Firebase
    if (!initFirebase())
    {
        Serial.println("ERROR: Firebase initialization failed");
        WiFi.disconnect(true);
        goToDeepSleep();
        return;
    }

    // Upload data
    if (uploadToFirestore(distance))
    {
        Serial.println("✓ Data sent");
    }
    else
    {
        Serial.println("ERROR: Data upload failed");
    }

    // Cleanup
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println("✓ WiFi disconnected");

    goToDeepSleep();
}

void loop()
{
    // Not used
}

bool connectToWiFi()
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40)
    {
        delay(500);
        attempts++;
    }

    return (WiFi.status() == WL_CONNECTED);
}

void syncTime()
{
    // NTP servers for Germany (GMT+1, DST GMT+2)
    configTime(3600, 3600, "pool.ntp.org", "time.nist.gov");

    int attempts = 0;
    while (time(nullptr) < 100000 && attempts < 20)
    {
        delay(500);
        attempts++;
    }

    time_t now = time(nullptr);
    if (now > 100000)
    {
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);

        // Format: YYYY-MM-DD_HH-MM-SS
        char buffer[32];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d_%H-%M-%S", &timeinfo);
        currentDateTime = String(buffer);

        Serial.print("✓ Time synchronized: ");
        Serial.println(currentDateTime);
    }
    else
    {
        // Fallback if NTP fails
        currentDateTime = "NoTime_" + String(bootCount) + "_" + String(millis());
        Serial.println("⚠ Time sync failed - using fallback");
    }
}

bool initFirebase()
{
    config.api_key = FIREBASE_API_KEY;
    auth.user.email = FIREBASE_USER_EMAIL;
    auth.user.password = FIREBASE_USER_PASSWORD;
    config.token_status_callback = tokenStatusCallback;
    config.timeout.serverResponse = 10 * 1000;

    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);

    int attempts = 0;
    while (!Firebase.ready() && attempts < 30)
    {
        delay(1000);
        attempts++;

        if (WiFi.status() != WL_CONNECTED)
        {
            return false;
        }
    }

    firebaseReady = Firebase.ready();
    return firebaseReady;
}

bool uploadToFirestore(float distance)
{
    if (!Firebase.ready())
    {
        return false;
    }

    // Document ID is date-time
    String documentPath = "measurements/" + currentDateTime;

    FirebaseJson content;
    content.set("fields/distance/doubleValue", String(distance, 2));

    // Unix timestamp
    time_t now = time(nullptr);
    if (now > 100000)
    {
        content.set("fields/unix_timestamp/integerValue", String(now));
    }

    if (!Firebase.Firestore.createDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str(), content.raw()))
    {
        return false;
    }

    // Also update latest
    String latestPath = "measurements/latest";
    Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", latestPath.c_str(), content.raw(), "distance");

    return true;
}

float measureDistance()
{
    const int NUM_MEASUREMENTS = 3;
    float measurements[NUM_MEASUREMENTS];
    int validMeasurements = 0;

    for (int i = 0; i < NUM_MEASUREMENTS; i++)
    {
        digitalWrite(TRIGGER_PIN, LOW);
        delayMicroseconds(2);
        digitalWrite(TRIGGER_PIN, HIGH);
        delayMicroseconds(10);
        digitalWrite(TRIGGER_PIN, LOW);

        unsigned long timeout = millis() + 1000;

        while (digitalRead(ECHO_PIN) == LOW)
        {
            if (millis() > timeout)
            {
                measurements[i] = -1;
                break;
            }
        }

        if (measurements[i] == -1)
            continue;

        unsigned long startTime = micros();

        while (digitalRead(ECHO_PIN) == HIGH)
        {
            if (millis() > timeout)
            {
                measurements[i] = -1;
                break;
            }
        }

        if (measurements[i] == -1)
            continue;

        unsigned long endTime = micros();
        unsigned long duration = endTime - startTime;
        float distance = (duration * 0.0343) / 2.0;

        if (distance >= 2.0 && distance <= 400.0)
        {
            measurements[i] = distance;
            validMeasurements++;
        }
        else
        {
            measurements[i] = -1;
        }

        delay(100);
    }

    if (validMeasurements == 0)
    {
        return -1;
    }

    float sum = 0;
    for (int i = 0; i < NUM_MEASUREMENTS; i++)
    {
        if (measurements[i] > 0)
        {
            sum += measurements[i];
        }
    }

    return sum / validMeasurements;
}

void goToDeepSleep()
{
    Serial.println("→ Deep Sleep");
    delay(100);

    esp_sleep_enable_timer_wakeup(SLEEP_DURATION * uS_TO_S_FACTOR);
    esp_deep_sleep_start();
}