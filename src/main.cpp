#include <Arduino.h>
#include <WifiCon.h>
#include <ESP32Servo.h>
#include "Data.h"

const int btnPin = 4;
const int servoPin = 26;
volatile bool buttonPressed = false;

const size_t MAX_TIMES = 20;
uint8_t times[MAX_TIMES][3];
size_t loaded = 0;

Servo servo;

void TaskServo(void *pvParameters);
void TaskDisplay(void *pvParameters);
void IRAM_ATTR InterruptButton();

void setup()
{
    Serial.begin(9600);
    servo.attach(servoPin);
    pinMode(btnPin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(btnPin), InterruptButton, FALLING);

    connectWifi();

    if (loadSettings(times, loaded, MAX_TIMES))
    {
        Serial.print("Loaded ");
        Serial.print(loaded);
        Serial.println(" entries from flash:");
        for (size_t i = 0; i < loaded; ++i)
        {
            Serial.print("  ");
            Serial.print(i);
            Serial.print(": ");
            Serial.print(times[i][0]);
            Serial.print(":");
            Serial.print(times[i][1]);
            Serial.print(":");
            Serial.println(times[i][2]);
        }
    }
    else
    {
        Serial.println("No saved times or read error");
    }

    xTaskCreatePinnedToCore(TaskServo, "Servo", 4096, NULL, 1, NULL, 1);
}

void loop() {}

void TaskServo(void *pvParameters)
{
    (void)pvParameters;
    while (1)
    {
        for (int i = 0; i <= 180; ++i)
        {
            servo.write(i);
            vTaskDelay(pdMS_TO_TICKS(15));
        }
        for (int i = 180; i >= 0; --i)
        {
            servo.write(i);
            vTaskDelay(pdMS_TO_TICKS(15));
        }
    }
}

void TaskDisplay(void *pvParameters) {}

void IRAM_ATTR InterruptButton()
{
    buttonPressed = true;
}
