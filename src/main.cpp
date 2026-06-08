#include <Arduino.h>
#include <WifiCon.h>
#include <ESP32Servo.h>
#include <time.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "Data.h"
#include "HttpCon.h"

enum DisplayState
{
    STATE_DEFAULT,
    STATE_MENU,
    STATE_ADD_HOUR,
    STATE_ADD_MINUTE,
    STATE_DELETE
};

const int btnPin = 4;
const int servoPin = 26;
const int lcdSDAPin = 21;
const int lcdSCLPin = 22;
volatile bool buttonPressed = false;

volatile DisplayState currentState = STATE_DEFAULT;
int menuIndex = 0;
int selectedDeleteIndex = 0;
uint8_t tempHour = 0;
uint8_t tempMinute = 0;

const char *modelName = "PakanOtomatis-v1"; // Nama model device
const size_t MAX_TIMES = 20;
uint8_t times[MAX_TIMES][3];
size_t loaded = 0;

volatile uint8_t lastHour = 255;
volatile uint8_t lastMinute = 255;
volatile uint8_t lastSecond = 255;
volatile uint8_t lastPostHour = 255;
volatile uint8_t lastPostMinute = 255;

Servo servo;
LiquidCrystal_I2C lcd(0x27, 16, 2);

void TaskServo(void *pvParameters);
void TaskPostData(void *pvParameters);
void TaskDisplay(void *pvParameters);
void IRAM_ATTR InterruptButton();
void getLocalTime(uint8_t &hour, uint8_t &minute, uint8_t &second);
bool isScheduledTime(uint8_t hour, uint8_t minute);

void setup()
{
    Serial.begin(9600);
    servo.attach(servoPin);
    pinMode(btnPin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(btnPin), InterruptButton, FALLING);

    connectWifi();

    Wire.begin(lcdSDAPin, lcdSCLPin);
    lcd.init();
    lcd.backlight();
    lcd.clear();

    // Sync time with NTP server
    configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    Serial.println("Waiting for NTP time sync...");
    time_t now = time(nullptr);
    int attempts = 0;
    while (now < 24 * 3600 && attempts < 30)
    {
        delay(500);
        now = time(nullptr);
        attempts++;
    }
    Serial.println();

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
    xTaskCreatePinnedToCore(TaskPostData, "PostData", 8192, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(TaskDisplay, "Display", 4096, NULL, 1, NULL, 1);
}

void loop() {}

// Get current local time
void getLocalTime(uint8_t &hour, uint8_t &minute, uint8_t &second)
{
    time_t now = time(nullptr);
    struct tm *timeinfo = localtime(&now);

    hour = timeinfo->tm_hour;
    minute = timeinfo->tm_min;
    second = timeinfo->tm_sec;
}

// Check if the given hour and minute match any scheduled time
bool isScheduledTime(uint8_t hour, uint8_t minute)
{
    for (size_t i = 0; i < loaded; ++i)
    {
        if (times[i][0] == hour && times[i][1] == minute)
        {
            return true;
        }
    }
    return false;
}

void TaskServo(void *pvParameters)
{
    (void)pvParameters;
    uint8_t currentHour, currentMinute, currentSecond;

    while (1)
    {
        getLocalTime(currentHour, currentMinute, currentSecond);

        // Check if current time matches any scheduled time
        // Only trigger once per minute to prevent multiple activations
        if ((currentHour != lastHour || currentMinute != lastMinute) &&
            isScheduledTime(currentHour, currentMinute))
        {
            Serial.print("Servo triggered at: ");
            Serial.print(currentHour);
            Serial.print(":");
            Serial.print(currentMinute);
            Serial.println();

            // Update last triggered time
            lastHour = currentHour;
            lastMinute = currentMinute;

            // Run servo sequence
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

        vTaskDelay(pdMS_TO_TICKS(500)); // Check every 500ms
    }
}

void TaskDisplay(void *pvParameters) {
    (void)pvParameters;

    while (1)
    {
        if (buttonPressed) {
            buttonPressed = false;
            switch (currentState) {
                case STATE_DEFAULT:
                    currentState = STATE_MENU;
                    break;
                case STATE_MENU:
                    if (menuIndex == 0) {
                        tempHour = 0;
                        tempMinute = 0;
                        currentState= STATE_ADD_HOUR;
                    } else {
                        currentState = STATE_DELETE;
                    }
                    break;
                case STATE_ADD_HOUR:
                    currentState = STATE_ADD_MINUTE;
                    break;
                case STATE_ADD_MINUTE:
                    // save
                    currentState = STATE_DEFAULT;
                    break;
                case STATE_DELETE:
                    // delete
                    currentState = STATE_DEFAULT;
                    break;
            }
        }
    }
}

void TaskPostData(void *pvParameters)
{
    (void)pvParameters;
    uint8_t currentHour, currentMinute, currentSecond;
    char timeStr[20];

    while (1)
    {
        getLocalTime(currentHour, currentMinute, currentSecond);

        // Send data every hour (only once per hour)
        if ((currentHour != lastPostHour || currentMinute != lastPostMinute) &&
            currentMinute == 0) // Send at the beginning of each hour
        {
            Serial.print("Posting data at: ");
            Serial.print(currentHour);
            Serial.print(":");
            Serial.print(currentMinute);
            Serial.println();

            // Format time string as HH:MM:SS
            snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d",
                     currentHour, currentMinute, currentSecond);

            // Send data to server
            postData(modelName, timeStr);

            // Update last post time
            lastPostHour = currentHour;
            lastPostMinute = currentMinute;

            vTaskDelay(pdMS_TO_TICKS(2000)); // Wait 2 seconds before next check
        }

        vTaskDelay(pdMS_TO_TICKS(1000)); // Check every 1 second
    }
}

void IRAM_ATTR InterruptButton()
{
    buttonPressed = true;
}
