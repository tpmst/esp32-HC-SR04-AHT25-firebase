#include <WiFi.h>

// Pin definitions
#define TRIGGER_PIN 5
#define ECHO_PIN 18

// WiFi-Credentials
const char *ssid = "network SSID";
const char *password = "networkpassword";

unsigned long lastMeasureTime = 0;
const unsigned long measureInterval = 30000; // 30 seconds
int measurementCount = 0;

void setup()
{
    Serial.begin(115200);
    delay(2000);

    pinMode(TRIGGER_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    digitalWrite(TRIGGER_PIN, LOW);

    // Connect to WiFi
    connectToWiFi();
}

void loop()
{
    // Monitor WiFi connection
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("\n⚠ WiFi connection lost!");
        reconnectWiFi();
        return;
    }

    // Check if it's time to measure
    if (millis() - lastMeasureTime >= measureInterval)
    {
        lastMeasureTime = millis();
        measurementCount++;

        float distance = measureDistance();

        if (distance >= 0)
        {
            Serial.print("\n[Measurement #");
            Serial.print(measurementCount);
            Serial.print("] Distance: ");
            Serial.print(distance);
            Serial.println(" cm");
            Serial.print("WiFi signal: ");
            Serial.print(WiFi.RSSI());
            Serial.println(" dBm");
        }
        else
        {
            Serial.println("\n⚠ Ultrasonic measurement failed!");
        }
    }

    delay(1000);
}

/**
 * Connects to WiFi
 */
void connectToWiFi()
{
    Serial.println("Connecting to WiFi...");
    Serial.print("SSID: ");
    Serial.println(ssid);

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);

    WiFi.begin(ssid, password);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40)
    {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    Serial.println();

    if (WiFi.status() = WL_CONNECTED)
    {
        Serial.println("WiFi connected!");
    }
    else
    {
        Serial.println("✗ WiFi connection failed!");
    }
}

/**
 * WiFi reconnect on connection loss
 */
void reconnectWiFi()
{
    Serial.println("Attempting WiFi reconnect...");
    WiFi.reconnect();

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20)
    {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    Serial.println();

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println("✓ WiFi reconnected!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
    }
    else
    {
        Serial.println("✗ WiFi reconnect failed!");
        Serial.println("Attempting full restart...");
        delay(2000);
        connectToWiFi();
    }
}

/**
 * Measure distance with HC-SR04
 */
float measureDistance()
{
    digitalWrite(TRIGGER_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIGGER_PIN, LOW);

    unsigned long timeout = millis() + 1000;

    while (digitalRead(ECHO_PIN) == LOW)
    {
        if (millis() > timeout)
            return -1;
    }

    unsigned long startTime = micros();

    while (digitalRead(ECHO_PIN) == HIGH)
    {
        if (millis() > timeout)
            return -1;
    }

    unsigned long endTime = micros();
    unsigned long duration = endTime - startTime;
    float distance = (duration * 0.0343) / 2.0;

    return distance;
}