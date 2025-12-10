#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// ===== PIN DEFINITIONS =====
#define TRIGGER_PIN 5 // HC-SR04 Trigger
#define ECHO_PIN 18   // HC-SR04 Echo
#define LED_BUILTIN 2 // Onboard LED
#define I2C_SDA 21    // AHT25 SDA (I2C Data)
#define I2C_SCL 22    // AHT25 SCL (I2C Clock)

// WiFi-Credentials
const char *ssid = "network SSID";
const char *password = "networkpassword";

// Firebase-Credentials
#define FIREBASE_API_KEY "YOUR_API_KEY"
#define FIREBASE_PROJECT_ID "YOUR_PROJECT_ID"
#define FIREBASE_USER_EMAIL "YOUR_EMAIL"
#define FIREBASE_USER_PASSWORD "YOUR_PASSWORD"

#define SLEEP_DURATION 21600 // seconds (6 hours)
#define uS_TO_S_FACTOR 1000000ULL

// Sensor objects
Adafruit_AHTX0 aht;
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

bool firebaseReady = false;
unsigned long currentUnixTime = 0;

void setup()
{
    Serial.begin(115200);
    delay(1000);

    // Disable LED
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);

    // Ultrasonic pins
    pinMode(TRIGGER_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    digitalWrite(TRIGGER_PIN, LOW);

    // Initialize I2C for AHT25 with custom pins
    Wire.begin(I2C_SDA, I2C_SCL);

    // Initialize AHT25
    if (!aht.begin())
    {
        Serial.println("ERROR: AHT25 not found!");
        goToDeepSleep();
        return;
    }

    // Measure distance
    float distance = measureDistance();

    if (distance < 0)
    {
        Serial.println("ERROR: Distance measurement failed");
        goToDeepSleep();
        return;
    }

    // Measure temperature & humidity
    sensors_event_t humidity, temp;
    aht.getEvent(&humidity, &temp);

    float temperature = temp.temperature;
    float humidityValue = humidity.relative_humidity;

    // Connect to WiFi
    if (!connectToWiFi())
    {
        Serial.println("ERROR: WiFi connection failed");
        goToDeepSleep();
        return;
    }

    Serial.println("✓ WiFi connected");

    // Get time from NTP server
    if (!getUnixTime())
    {
        Serial.println("ERROR: Time synchronization failed");
        WiFi.disconnect(true);
        goToDeepSleep();
        return;
    }

    Serial.println("✓ Time synchronized");

    // Initialize Firebase
    if (!initFirebase())
    {
        Serial.println("ERROR: Firebase initialization failed");
        WiFi.disconnect(true);
        goToDeepSleep();
        return;
    }

    // Upload data
    if (uploadToFirestore(distance, temperature, humidityValue))
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

bool getUnixTime()
{
    // Configure NTP servers (GMT+1 for Germany, +3600 seconds offset)
    configTime(3600, 3600, "pool.ntp. org", "time.nist.gov", "time.google.com");

    // Wait for time synchronization
    int attempts = 0;
    while (attempts < 20)
    {
        time_t now = time(nullptr);
        if (now > 1000000000)
        { // Valid Unix time (after year 2001)
            currentUnixTime = now;
            return true;
        }
        delay(500);
        attempts++;
    }

    return false;
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

bool uploadToFirestore(float distance, float temperature, float humidity)
{
    if (!Firebase.ready())
    {
        return false;
    }

    // Document ID based on Unix timestamp
    String documentId = String(currentUnixTime);
    String documentPath = "measurements/" + documentId;

    FirebaseJson content;
    content.set("fields/distance/doubleValue", String(distance, 2));
    content.set("fields/temperature/doubleValue", String(temperature, 2));
    content.set("fields/humidity/doubleValue", String(humidity, 2));
    content.set("fields/timestamp/integerValue", String(currentUnixTime));

    if (!Firebase.Firestore.createDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str(), content.raw()))
    {
        Serial.print("Firestore Error: ");
        Serial.println(fbdo.errorReason());
        return false;
    }

    // Also update latest
    String latestPath = "measurements/latest";
    Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", latestPath.c_str(), content.raw(), "distance,temperature,humidity,timestamp");

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