#include <HTTPClient.h>
#include <ArduinoJson.h>

const char *server = "https://iot-data-server.vercel.app/feeder";

void postData()
{
    StaticJsonDocument<200> doc;

    doc['model'] = "nama-model";
    doc['time'] = "time";

    String body;
    serializeJson(doc, body);

    HTTPClient http;
    http.begin(server);
    http.addHeader("Content-Type", "application/json");
    int resCode = http.POST(body);

    if (resCode > 0)
    {
        String res = http.getString();
    }
    else
    {
        delay(1000);
        postData();
    }
}