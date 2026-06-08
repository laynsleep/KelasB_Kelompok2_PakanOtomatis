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
const int rotarySWPin = 25;
const int rotaryDTPin = 33;
const int rotaryCLKPin = 32;
volatile bool buttonPressed = false;
volatile unsigned long lastInterrupt = 0;

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

    Wire.begin(lcdSDAPin, lcdSCLPin);
    lcd.init();
    lcd.backlight();
    lcd.clear();

    pinMode(rotarySWPin, INPUT_PULLUP);
    pinMode(rotaryDTPin, INPUT_PULLUP);
    pinMode(rotaryCLKPin, INPUT_PULLUP);

    connectWifi();

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

int readEncoder()
{
    static int lastCLK = HIGH;
    int currentCLK = digitalRead(rotaryCLKPin);

    if (currentCLK != lastCLK && currentCLK == LOW) {
        if (digitalRead(rotaryDTPin) != currentCLK) {
            lastCLK = currentCLK;
            return 1;
        } else {
            lastCLK = currentCLK;
            return -1;
        }
    }

    lastCLK = currentCLK;
    return 0;
}

bool getNextSchedule(uint8_t hour, uint8_t minute, uint8_t &nextHour, uint8_t &nextMinute)
{
    if (loaded == 0) return false;

    int currentTime = hour * 60 + minute;
    int smallestDiff = 24 * 60 + 1;
    int nextSchedule = -1;

    for (size_t i = 0; i < loaded; i++) {
        int scheduleTime = times[i][0] * 60 + times[i][1];
        int diff = scheduleTime - currentTime;

        if (diff <= 0) diff += 24 * 60;
        if (diff < smallestDiff) {
            smallestDiff = diff;
            nextSchedule = i;
        }
    }

    // if (nextSchedule == -1) return false;

    nextHour = times[nextSchedule][0];
    nextMinute = times[nextSchedule][1];
    return true;
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
    uint8_t currentHour, currentMinute, currentSecond, nextHour, nextMinute;
    DisplayState lastState = STATE_DEFAULT;
    int hour, minute;
    char timeStr[20];

    while (1)
    {
        getLocalTime(currentHour, currentMinute, currentSecond);
        int encoderDelta = readEncoder();

        // read encoder input
        switch (currentState) {
            case STATE_MENU:
                menuIndex += encoderDelta;
                menuIndex = constrain(menuIndex, 0, 1);
                break;
            case STATE_ADD_HOUR:
                hour = tempHour + encoderDelta;
                if (hour > 23) hour = 0;
                if (hour < 0) hour = 23;
                tempHour = hour;
                break;
            case STATE_ADD_MINUTE:
                minute = tempMinute + encoderDelta;
                if (minute > 59) minute = 0;
                if (minute < 0) minute = 59;
                tempMinute = minute;
                break;;
            case STATE_DELETE:
                selectedDeleteIndex += encoderDelta;
                selectedDeleteIndex = constrain(
                    selectedDeleteIndex,
                    0,
                    loaded - 1
                );
                break;
        }

        // change display state if button interrupt is pressed
        if (buttonPressed) {
            bool exist = false;
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
                        selectedDeleteIndex = 0;
                        loaded == 0 ? currentState = STATE_DEFAULT : 
                        currentState = STATE_DELETE;
                    }
                    break;
                case STATE_ADD_HOUR:
                    currentState = STATE_ADD_MINUTE;
                    break;
                case STATE_ADD_MINUTE:
                    // save
                    for (size_t i = 0; i < loaded; i++) {
                        if(times[i][0] == tempHour && times[i][1] == tempMinute) {
                            exist = true;
                            break;
                        }
                    }
                    if (!exist && loaded < MAX_TIMES) {
                        times[loaded][0] = tempHour;
                        times[loaded][1] = tempMinute;
                        times[loaded][2] = 0;

                        loaded++;
                        saveSettings(times, loaded);
                    }
                    currentState = STATE_DEFAULT;
                    break;
                case STATE_DELETE:
                    // delete
                    if (loaded > 0) {
                        memmove(
                            &times[selectedDeleteIndex], 
                            &times[selectedDeleteIndex + 1], 
                            (loaded - selectedDeleteIndex - 1) * sizeof(times[0]));

                        loaded--;
                        saveSettings(times, loaded);
                    }
                    currentState = STATE_DEFAULT;
                    break;
            }
        }

        if(lastState != currentState){
            lcd.clear();
            lastState = currentState;
        }

        // text to display
        switch (currentState) {
            case STATE_DEFAULT:
                snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d",
                        currentHour, currentMinute, currentSecond);
                lcd.setCursor(0, 0);
                lcd.print(timeStr);

                lcd.setCursor(0, 1);
                if (getNextSchedule(currentHour, currentMinute, nextHour, nextMinute)) {
                    snprintf(timeStr, sizeof(timeStr), "Pakan: %02d:%02d",
                            nextHour, nextMinute);
                    lcd.print("                ");
                    lcd.setCursor(0, 1);
                    lcd.print(timeStr);
                } else lcd.print("Jadwal kosong");
                break;
            case STATE_MENU:
                lcd.setCursor(0, 0);
                if (menuIndex == 0) {
                    lcd.setCursor(0, 0);
                    lcd.print(">Tambah Jadwal");
                    lcd.setCursor(0, 1);
                    lcd.print("Hapus Jadwal");
                } else {
                    lcd.setCursor(0, 0);
                    lcd.print("Tambah Jadwal");
                    lcd.setCursor(0, 1);
                    lcd.print(">Hapus Jadwal");
                }
                break;
            case STATE_ADD_HOUR:
                lcd.setCursor(0,0);
                lcd.print("Set Jam");

                lcd.setCursor(0,1);
                if (tempHour < 10) lcd.print("0");
                lcd.print(tempHour);
                lcd.print(":");
                lcd.print("00");
                break;
            case STATE_ADD_MINUTE:
                lcd.setCursor(0,0);
                lcd.print("Set Menit");

                lcd.setCursor(0,1);
                if (tempHour < 10) lcd.print("0");
                lcd.print(tempHour);
                lcd.print(":");
                if (tempMinute < 10) lcd.print("0");
                lcd.print(tempMinute);
                break;
            case STATE_DELETE:
                // list jadwal
                lcd.setCursor(0,0);
                lcd.print("Hapus Jadwal");

                lcd.setCursor(0,1);
                snprintf(timeStr, sizeof(timeStr), "%02d:%02d",
                        times[selectedDeleteIndex][0], times[selectedDeleteIndex][1]);
                lcd.print(timeStr);
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
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
    unsigned long currentInterrupt = millis();
    if (currentInterrupt - lastInterrupt > 200)
    {
        buttonPressed = true;
        lastInterrupt = currentInterrupt;
    }
}

