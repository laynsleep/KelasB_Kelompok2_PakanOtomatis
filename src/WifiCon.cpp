#include <WiFi.h>

const char *ssid = "nama-wifi";
const char *password = "passwordwifi";

const char *hostname = "nama perangkat";

#define BUILTIN_LED 2

void connectWifi()
{
    pinMode(BUILTIN_LED, OUTPUT);
    WiFi.setHostname(hostname);
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
}