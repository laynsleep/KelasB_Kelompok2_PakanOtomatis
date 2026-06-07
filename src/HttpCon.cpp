#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Arduino.h>

const char *server = "https://iot-data-server.vercel.app/feeder";

void postData(const char *modelName, const char *timeStr)
{
    StaticJsonDocument<200> doc;

    doc["model"] = modelName;
    doc["time"] = timeStr;

    String body;
    serializeJson(doc, body);

    Serial.print("Sending: ");
    Serial.println(body);

    HTTPClient http;
    http.begin(server);
    http.addHeader("Content-Type", "application/json");
    int resCode = http.POST(body);

    if (resCode > 0)
    {
        String res = http.getString();
        Serial.print("Response: ");
        Serial.println(res);
    }
    else
    {
        Serial.print("POST failed, error code: ");
        Serial.println(resCode);
    }
}