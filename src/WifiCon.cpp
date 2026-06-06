#include <WiFi.h>

const char *ssid = "Crzx";
const char *password = "CrzxaExe3";

const char *hostname = "Feeder";

#define BUILTIN_LED 2

void connectWifi()
{
    pinMode(BUILTIN_LED, OUTPUT);
    WiFi.setHostname(hostname);
    WiFi.begin(ssid, password);
    Serial.print("Connecting to wifi");

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
}