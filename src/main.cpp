#include <Arduino.h>
#include <WiFi.h>
#include <ESP32Servo.h>
#include <time.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <stdint.h>
#include <stddef.h>
// Matikan brownout detector — mencegah reset saat WiFi tarik arus tinggi
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_system.h"

// ─────────────────────────────────────────────
// WiFi Config
// ─────────────────────────────────────────────
const char *ssid     = "bebas";
const char *password = "bebas123";
const char *hostname = "Feeder";
#define BUILTIN_LED 2

// ─────────────────────────────────────────────
// SERVO CONFIG — ubah nilai ini sesuai kebutuhan
// ─────────────────────────────────────────────
#define SERVO_OPEN_ANGLE   90    // derajat buka (0–180)
#define SERVO_CLOSE_ANGLE   0    // derajat tutup
#define SERVO_STEP         10    // derajat per langkah (lebih besar = lebih cepat)
#define SERVO_STEP_DELAY    5    // ms antar langkah (lebih kecil = lebih cepat)
#define SERVO_OPEN_HOLD   500    // ms servo tetap terbuka sebelum menutup

// ─────────────────────────────────────────────
// Ultrasonik — kalibrasi setengah botol
// ─────────────────────────────────────────────
const float DIST_FULL  =  2.5f;   // cm saat pakan penuh (dekat sensor)
const float DIST_EMPTY = 13.0f;   // cm saat pakan kosong (ke tutup/dasar botol)

// ─────────────────────────────────────────────
// Flash Storage
// PENTING: namespace max 15 karakter, key max 15 karakter
// Gunakan nama berbeda dari "test" agar tidak konflik
// ─────────────────────────────────────────────
#define PREF_NAMESPACE  "feeder_data"   // max 15 char
#define PREF_KEY_COUNT  "sched_count"   // max 15 char
#define PREF_KEY_DATA   "sched_data"    // max 15 char

Preferences preferences;

void saveSettings(const uint8_t schedTimes[][3], size_t count)
{
    bool ok = preferences.begin(PREF_NAMESPACE, false);
    if (!ok) {
        Serial.println("[SAVE] ERROR: tidak bisa buka Preferences!");
        return;
    }
    preferences.clear();  // bersihkan dulu agar tidak ada data lama
    uint16_t c = (uint16_t)count;
    preferences.putUShort(PREF_KEY_COUNT, c);
    if (count > 0) {
        size_t written = preferences.putBytes(PREF_KEY_DATA, schedTimes, count * 3);
        if (written != count * 3) {
            Serial.printf("[SAVE] ERROR: hanya %d/%d byte tersimpan!\n", (int)written, (int)(count*3));
        } else {
            Serial.printf("[SAVE] OK: %d jadwal tersimpan.\n", (int)count);
        }
    }
    preferences.end();
}

bool loadSettings(uint8_t schedTimes[][3], size_t &count, size_t maxCount)
{
    count = 0;
    bool ok = preferences.begin(PREF_NAMESPACE, true);  // read-only
    if (!ok) {
        Serial.println("[LOAD] Preferences belum ada, mulai kosong.");
        return true;
    }

    // Cek key count ada atau tidak
    if (!preferences.isKey(PREF_KEY_COUNT)) {
        Serial.println("[LOAD] Key count tidak ada, jadwal kosong.");
        preferences.end();
        return true;
    }

    uint16_t c = preferences.getUShort(PREF_KEY_COUNT, 0);
    Serial.printf("[LOAD] count = %d\n", (int)c);

    if (c == 0) {
        preferences.end();
        count = 0;
        return true;
    }

    if ((size_t)c > maxCount) {
        Serial.printf("[LOAD] count %d > maxCount %d, potong.\n", (int)c, (int)maxCount);
        c = (uint16_t)maxCount;
    }

    if (!preferences.isKey(PREF_KEY_DATA)) {
        Serial.println("[LOAD] Key data tidak ada!");
        preferences.end();
        count = 0;
        return false;
    }

    size_t expected = (size_t)c * 3;
    size_t read = preferences.getBytes(PREF_KEY_DATA, schedTimes, expected);
    preferences.end();

    if (read != expected) {
        Serial.printf("[LOAD] ERROR: baca %d tapi expect %d byte!\n", (int)read, (int)expected);
        count = 0;
        return false;
    }

    count = (size_t)c;
    Serial.printf("[LOAD] OK: %d jadwal dimuat.\n", (int)count);
    return true;
}

// ─────────────────────────────────────────────
// WiFi — dengan auto-reconnect task
// ─────────────────────────────────────────────
void connectWifi()
{
    pinMode(BUILTIN_LED, OUTPUT);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);     // ESP32 reconnect otomatis jika putus
    WiFi.persistent(false);          // jangan simpan ke flash tiap koneksi
    WiFi.setHostname(hostname);
    WiFi.begin(ssid, password);

    Serial.print("[WiFi] Connecting");
    int timeout = 0;
    while (WiFi.status() != WL_CONNECTED && timeout < 40) {
        delay(500);
        Serial.print(".");
        timeout++;
        // LED blink saat connecting
        digitalWrite(BUILTIN_LED, !digitalRead(BUILTIN_LED));
    }
    if (WiFi.status() == WL_CONNECTED) {
        digitalWrite(BUILTIN_LED, HIGH);
        Serial.println("\n[WiFi] Connected. IP: " + WiFi.localIP().toString());
    } else {
        digitalWrite(BUILTIN_LED, LOW);
        Serial.println("\n[WiFi] GAGAL, lanjut tanpa WiFi.");
    }
}

// Task WiFi watchdog — jalan di core 0, cek tiap 30 detik
void TaskWifiWatchdog(void *pvParameters)
{
    (void)pvParameters;
    vTaskDelay(pdMS_TO_TICKS(10000));  // tunggu 10 detik setelah boot
    while (1) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[WiFi] Putus! Reconnect...");
            digitalWrite(BUILTIN_LED, LOW);
            WiFi.disconnect();
            vTaskDelay(pdMS_TO_TICKS(1000));
            WiFi.begin(ssid, password);
            int tries = 0;
            while (WiFi.status() != WL_CONNECTED && tries < 20) {
                vTaskDelay(pdMS_TO_TICKS(500));
                tries++;
                Serial.print(".");
            }
            if (WiFi.status() == WL_CONNECTED) {
                digitalWrite(BUILTIN_LED, HIGH);
                Serial.println("\n[WiFi] Reconnected!");
            } else {
                Serial.println("\n[WiFi] Reconnect gagal, coba lagi 30 detik.");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(30000));  // cek tiap 30 detik
    }
}

// ─────────────────────────────────────────────
// HTTP Post
// ─────────────────────────────────────────────
const char *server = "https://iot-data-server.vercel.app/feeder/create";

void postData(const char *modelName, const char *timeStr, int levelPct)
{
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[POST] Skip: WiFi tidak terhubung.");
        return;
    }
    StaticJsonDocument<256> doc;
    doc["model"]      = modelName;
    doc["time"]       = timeStr;
    doc["feed_level"] = levelPct;

    String body;
    serializeJson(doc, body);
    Serial.print("[POST] Sending: "); Serial.println(body);

    HTTPClient http;
    http.begin(server);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(5000);   // timeout 5 detik agar tidak hang lama
    int resCode = http.POST(body);
    if (resCode > 0) {
        Serial.print("[POST] Response: "); Serial.println(http.getString());
    } else {
        Serial.print("[POST] Error: "); Serial.println(resCode);
    }
    http.end();
}

// ─────────────────────────────────────────────
// HTTP Post Status (dipanggil saat servo aktif)
// status: 0=idle, 1=manual feed, 2=otomatis
// ─────────────────────────────────────────────
const char *serverStatus = "https://iot-data-server.vercel.app/feeder/status/create";

void postStatus(int status)
{
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[STATUS] Skip: WiFi tidak terhubung.");
        return;
    }
    StaticJsonDocument<128> doc;
    doc["model"]  = "PakanOtomatis-v1";
    doc["status"] = status;

    String body;
    serializeJson(doc, body);
    Serial.print("[STATUS] Sending: "); Serial.println(body);

    HTTPClient http;
    http.begin(serverStatus);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(5000);
    int resCode = http.POST(body);
    if (resCode > 0) {
        Serial.print("[STATUS] Response: "); Serial.println(http.getString());
    } else {
        Serial.print("[STATUS] Error: "); Serial.println(resCode);
    }
    http.end();
}

// ─────────────────────────────────────────────
// Display State
// ─────────────────────────────────────────────
enum DisplayState {
    STATE_DEFAULT,
    STATE_MENU,
    STATE_ADD_HOUR,
    STATE_ADD_MINUTE,
    STATE_ADD_CONFIRM,    // konfirmasi sebelum save
    STATE_DELETE,
    STATE_MANUAL_FEED,
    STATE_VIEW_SCHEDULE
};

// ─────────────────────────────────────────────
// Pin Definitions
// ─────────────────────────────────────────────
const int btnPin       = 27;
const int servoPin     = 4;
const int lcdSDAPin    = 21;
const int lcdSCLPin    = 22;
const int rotarySWPin  = 25;
const int rotaryDTPin  = 33;
const int rotaryCLKPin = 32;
const int ultraTrigPin = 13;
const int ultraEchoPin = 14;

// ─────────────────────────────────────────────
// Rotary Encoder — ISR quadrature state machine
// ─────────────────────────────────────────────
static const int8_t ENCODER_TABLE[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0
};
static volatile int     encoderAccum = 0;
static volatile uint8_t encState     = 0;
static portMUX_TYPE     encMux       = portMUX_INITIALIZER_UNLOCKED;

void IRAM_ATTR ISR_Encoder() {
    portENTER_CRITICAL_ISR(&encMux);
    int clk = digitalRead(rotaryCLKPin);
    int dt  = digitalRead(rotaryDTPin);
    uint8_t ns = (uint8_t)((encState << 2) | (clk << 1) | dt);
    int8_t  d  = ENCODER_TABLE[ns & 0x0F];
    encState   = ns & 0x03;
    if (d != 0) encoderAccum += d;
    portEXIT_CRITICAL_ISR(&encMux);
}

int readEncoderDelta() {
    portENTER_CRITICAL(&encMux);
    int v = encoderAccum;
    encoderAccum = 0;
    portEXIT_CRITICAL(&encMux);
    return v;
}

// ─────────────────────────────────────────────
// Global Variables
// ─────────────────────────────────────────────
volatile unsigned long lastIRQ_main  = 0;
volatile DisplayState  currentState  = STATE_DEFAULT;

int     menuIndex           = 0;
int     selectedDeleteIndex = 0;
int     viewScrollIndex     = 0;
uint8_t tempHour            = 0;
uint8_t tempMinute          = 0;

const char  *modelName = "PakanOtomatis-v1";
const size_t MAX_TIMES = 20;
uint8_t      schedTimes[MAX_TIMES][3];
size_t       loaded = 0;

volatile uint8_t lastHour       = 255;
volatile uint8_t lastMinute     = 255;
volatile uint8_t lastPostHour   = 255;
volatile uint8_t lastPostMinute = 255;

volatile int feedLevelPct = 0;

SemaphoreHandle_t lcdMutex;
SemaphoreHandle_t manualFeedSem;
SemaphoreHandle_t postStatusSem;   // semaphore trigger post status
volatile int      pendingStatus = 0;  // status yang akan dikirim

Servo             servo;
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ─────────────────────────────────────────────
// Forward Declarations
// ─────────────────────────────────────────────
void TaskServo      (void *pvParameters);
void TaskPostData   (void *pvParameters);
void TaskDisplay    (void *pvParameters);
void TaskUltrasonic (void *pvParameters);
void TaskWifiWatchdog(void *pvParameters);
void TaskPostStatus (void *pvParameters);
void IRAM_ATTR ISR_BtnMain();
void IRAM_ATTR ISR_Encoder();
void getLocalTime(uint8_t &h, uint8_t &m, uint8_t &s);
bool isScheduledTime(uint8_t h, uint8_t m);
void doServoFeed();

// ─────────────────────────────────────────────
// Setup
// ─────────────────────────────────────────────
void setup()
{
    // Matikan brownout detector — cegah reset saat WiFi tarik arus ~300mA
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    Serial.begin(115200);
    Serial.println("\n=== PAKAN AYAM OTOMATIS v2 ===");

    // Cetak alasan boot — bantu debug kenapa bisa reset berkali-kali
    esp_reset_reason_t reason = esp_reset_reason();
    const char* rNames[] = {"UNKNOWN","POWERON","EXT","SW","PANIC",
                             "INT_WDT","TASK_WDT","WDT","DEEPSLEEP","BROWNOUT","SDIO"};
    int ri = (int)reason;
    Serial.printf("[BOOT] Reset reason: %s (%d)\n",
                  (ri>=0 && ri<=10) ? rNames[ri] : "?", ri);
    if (reason == ESP_RST_BROWNOUT)
        Serial.println("[BOOT] BROWNOUT — cek power supply! Butuh minimal 1A.");
    if (reason == ESP_RST_TASK_WDT || reason == ESP_RST_INT_WDT)
        Serial.println("[BOOT] WATCHDOG reset — ada task yang blocking!");

    // 1. LCD
    Wire.begin(lcdSDAPin, lcdSCLPin);
    delay(100);
    bool lcdFound = false;
    byte lcdAddr  = 0x27;
    for (byte addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("[I2C] Found: 0x%02X\n", addr);
            lcdAddr  = addr;
            lcdFound = true;
        }
    }
    if (lcdFound) {
        lcd = LiquidCrystal_I2C(lcdAddr, 16, 2);
        lcd.init();
        lcd.backlight();
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print("Pakan Otomatis");
        lcd.setCursor(0, 1); lcd.print("v2 - Memulai...");
        Serial.println("[LCD] OK");
    } else {
        Serial.println("[LCD] TIDAK DITEMUKAN!");
    }

    // 2. Servo
    servo.attach(servoPin);
    servo.write(SERVO_CLOSE_ANGLE);
    delay(300);

    // 3. Ultrasonik
    pinMode(ultraTrigPin, OUTPUT);
    pinMode(ultraEchoPin, INPUT);
    digitalWrite(ultraTrigPin, LOW);

    // 4. Button manual feed
    pinMode(btnPin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(btnPin), ISR_BtnMain, FALLING);

    // 5. Rotary encoder ISR
    pinMode(rotaryCLKPin, INPUT_PULLUP);
    pinMode(rotaryDTPin,  INPUT_PULLUP);
    pinMode(rotarySWPin,  INPUT_PULLUP);
    encState = (uint8_t)((digitalRead(rotaryCLKPin) << 1) | digitalRead(rotaryDTPin));
    attachInterrupt(digitalPinToInterrupt(rotaryCLKPin), ISR_Encoder, CHANGE);
    attachInterrupt(digitalPinToInterrupt(rotaryDTPin),  ISR_Encoder, CHANGE);

    // 6. Mutex & Semaphore
    lcdMutex      = xSemaphoreCreateMutex();
    manualFeedSem = xSemaphoreCreateBinary();
    postStatusSem = xSemaphoreCreateBinary();

    // 7. WiFi
    if (xSemaphoreTake(lcdMutex, portMAX_DELAY) == pdTRUE) {
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print("Connecting WiFi");
        lcd.setCursor(0, 1); lcd.print("...");
        xSemaphoreGive(lcdMutex);
    }
    connectWifi();

    // 8. NTP
    if (WiFi.status() == WL_CONNECTED) {
        if (xSemaphoreTake(lcdMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            lcd.setCursor(0, 1); lcd.print("Sync NTP...     ");
            xSemaphoreGive(lcdMutex);
        }
        configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
        time_t now = time(nullptr);
        int attempts = 0;
        while (now < 86400 && attempts < 30) {
            delay(500);
            now = time(nullptr);
            attempts++;
            Serial.print(".");
        }
        Serial.println("\n[NTP] OK");
    }

    // 9. Load jadwal dari flash
    if (xSemaphoreTake(lcdMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        lcd.setCursor(0, 1); lcd.print("Load jadwal...  ");
        xSemaphoreGive(lcdMutex);
    }
    if (loadSettings(schedTimes, loaded, MAX_TIMES)) {
        for (size_t i = 0; i < loaded; i++)
            Serial.printf("  Jadwal[%d]: %02d:%02d\n", (int)i, schedTimes[i][0], schedTimes[i][1]);
    }
    delay(500);

    // 10. FreeRTOS Tasks
    xTaskCreatePinnedToCore(TaskServo,       "Servo",      4096, NULL, 3, NULL, 1);
    xTaskCreatePinnedToCore(TaskDisplay,     "Display",    4096, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(TaskPostData,    "PostData",   8192, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(TaskUltrasonic,  "Ultrasonic", 4096, NULL, 2, NULL, 0);
    xTaskCreatePinnedToCore(TaskWifiWatchdog,"WiFiWatch",  4096, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(TaskPostStatus,  "PostStatus", 4096, NULL, 1, NULL, 0);

    Serial.println("[INIT] Sistem siap!");
}

void loop() { vTaskDelay(pdMS_TO_TICKS(1000)); }

// ─────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────
void getLocalTime(uint8_t &hour, uint8_t &minute, uint8_t &second)
{
    time_t now = time(nullptr);
    struct tm *t = localtime(&now);
    hour   = (uint8_t)t->tm_hour;
    minute = (uint8_t)t->tm_min;
    second = (uint8_t)t->tm_sec;
}

bool isScheduledTime(uint8_t hour, uint8_t minute)
{
    for (size_t i = 0; i < loaded; i++)
        if (schedTimes[i][0] == hour && schedTimes[i][1] == minute)
            return true;
    return false;
}

// Servo: buka-tahan-tutup, kecepatan dikontrol SERVO_STEP & SERVO_STEP_DELAY
void doServoFeed()
{
    // Buka
    for (int pos = SERVO_CLOSE_ANGLE; pos <= SERVO_OPEN_ANGLE; pos += SERVO_STEP) {
        servo.write(constrain(pos, 0, 180));
        vTaskDelay(pdMS_TO_TICKS(SERVO_STEP_DELAY));
    }
    servo.write(SERVO_OPEN_ANGLE);
    vTaskDelay(pdMS_TO_TICKS(SERVO_OPEN_HOLD));   // tahan terbuka

    // Tutup
    for (int pos = SERVO_OPEN_ANGLE; pos >= SERVO_CLOSE_ANGLE; pos -= SERVO_STEP) {
        servo.write(constrain(pos, 0, 180));
        vTaskDelay(pdMS_TO_TICKS(SERVO_STEP_DELAY));
    }
    servo.write(SERVO_CLOSE_ANGLE);
}

// ─────────────────────────────────────────────
// Task: Ultrasonik (core 0)
// Tidak pakai pulseIn() karena blocking dan menyebabkan stack overflow.
// Ganti dengan timing manual pakai esp_timer_get_time() (microseconds).
// ─────────────────────────────────────────────
void TaskUltrasonic(void *pvParameters)
{
    (void)pvParameters;
    const uint32_t TIMEOUT_US = 25000UL;  // 25ms timeout (~430cm)

    while (1) {
        // Kirim trigger 10µs
        digitalWrite(ultraTrigPin, LOW);
        delayMicroseconds(2);
        digitalWrite(ultraTrigPin, HIGH);
        delayMicroseconds(10);
        digitalWrite(ultraTrigPin, LOW);

        // Tunggu ECHO HIGH (mulai pulsa) — timeout 25ms
        uint32_t t0 = (uint32_t)esp_timer_get_time();
        while (digitalRead(ultraEchoPin) == LOW) {
            if ((uint32_t)esp_timer_get_time() - t0 > TIMEOUT_US) break;
        }

        // Catat waktu mulai
        uint32_t tStart = (uint32_t)esp_timer_get_time();

        // Tunggu ECHO LOW (akhir pulsa) — timeout 25ms
        while (digitalRead(ultraEchoPin) == HIGH) {
            if ((uint32_t)esp_timer_get_time() - tStart > TIMEOUT_US) break;
        }

        uint32_t duration = (uint32_t)esp_timer_get_time() - tStart;

        // Konversi ke cm
        float dist_cm = (duration > 0 && duration < TIMEOUT_US)
                        ? (duration * 0.0343f / 2.0f)
                        : DIST_EMPTY;

        // Clamp ke range valid
        if (dist_cm > DIST_EMPTY) dist_cm = DIST_EMPTY;
        if (dist_cm < DIST_FULL)  dist_cm = DIST_FULL;

        float pct = (DIST_EMPTY - dist_cm) / (DIST_EMPTY - DIST_FULL) * 100.0f;
        feedLevelPct = (int)constrain(pct, 0.0f, 100.0f);

        if (feedLevelPct <= 20)
            Serial.printf("[SENSOR] Pakan %d%% (%.1fcm) — HAMPIR HABIS!\n", feedLevelPct, dist_cm);

        vTaskDelay(pdMS_TO_TICKS(1000));  // cek tiap 1 detik, lebih ringan
    }
}

// ─────────────────────────────────────────────
// Task: Servo (core 1, prioritas 3)
// ─────────────────────────────────────────────
void TaskServo(void *pvParameters)
{
    (void)pvParameters;
    uint8_t h, m, s;
    while (1) {
        // Manual feed
        if (xSemaphoreTake(manualFeedSem, 0) == pdTRUE) {
            Serial.println("[SERVO] Manual feed!");
            if (xSemaphoreTake(lcdMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
                lcd.clear();
                lcd.setCursor(0, 0); lcd.print("MANUAL FEED!    ");
                lcd.setCursor(0, 1); lcd.print("Membuka servo...");
                xSemaphoreGive(lcdMutex);
            }
            currentState = STATE_MANUAL_FEED;
            pendingStatus = 1;
            xSemaphoreGive(postStatusSem);  // trigger post di background
            doServoFeed();                  // servo langsung jalan tanpa tunggu HTTP
            currentState = STATE_DEFAULT;
        }

        // Jadwal otomatis
        getLocalTime(h, m, s);
        if ((h != lastHour || m != lastMinute) && isScheduledTime(h, m)) {
            Serial.printf("[SERVO] Jadwal %02d:%02d aktif!\n", h, m);
            lastHour   = h;
            lastMinute = m;
            if (xSemaphoreTake(lcdMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
                lcd.clear();
                lcd.setCursor(0, 0); lcd.print("Memberi Pakan!  ");
                lcd.setCursor(0, 1); lcd.print("Otomatis...     ");
                xSemaphoreGive(lcdMutex);
            }
            currentState = STATE_MANUAL_FEED;
            pendingStatus = 2;
            xSemaphoreGive(postStatusSem);  // trigger post di background
            doServoFeed();                  // servo langsung jalan tanpa tunggu HTTP
            currentState = STATE_DEFAULT;
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

// ─────────────────────────────────────────────
// Task: Display + Encoder + SW (core 1)
// ─────────────────────────────────────────────
void TaskDisplay(void *pvParameters)
{
    (void)pvParameters;

    uint8_t      curH, curM, curS;
    DisplayState lastState     = STATE_DEFAULT;
    char         buf[20];

    // SW rotary debounce
    unsigned long swPressTime  = 0;
    bool          swWasPressed = false;

    // Throttle encoder: 1 step per 150ms
    unsigned long lastEncTime  = 0;
    const unsigned long ENC_MS = 150;

    while (1)
    {
        getLocalTime(curH, curM, curS);

        // ── Encoder ───────────────────────────────────────────────
        int encDelta = 0;
        unsigned long now = millis();
        if (now - lastEncTime >= ENC_MS) {
            int raw = readEncoderDelta();
            if      (raw > 0) { encDelta = 1;  lastEncTime = now; }
            else if (raw < 0) { encDelta = -1; lastEncTime = now; }
        } else {
            readEncoderDelta();  // buang akumulasi
        }

        // ── SW rotary ─────────────────────────────────────────────
        int swLvl = digitalRead(rotarySWPin);
        if (swLvl == LOW && !swWasPressed) {
            swWasPressed = true;
            swPressTime  = millis();
        } else if (swLvl == HIGH && swWasPressed) {
            unsigned long held = millis() - swPressTime;
            swWasPressed = false;

            if (held >= 600) {
                // TAHAN = Back
                readEncoderDelta();  // buang akumulasi
                Serial.println("[SW] Back");
                switch (currentState) {
                    case STATE_MENU:
                    case STATE_ADD_HOUR:
                    case STATE_ADD_MINUTE:
                    case STATE_ADD_CONFIRM:
                    case STATE_DELETE:
                    case STATE_VIEW_SCHEDULE:
                        currentState = STATE_DEFAULT;
                        break;
                    default: break;
                }
            } else {
                // TAP = OK/Pilih
                Serial.println("[SW] OK");
                switch (currentState)
                {
                    case STATE_DEFAULT:
                        menuIndex    = 0;
                        readEncoderDelta();
                        currentState = STATE_MENU;
                        break;

                    case STATE_MENU:
                        // 0=Tambah 1=Hapus 2=Lihat 3=Back
                        readEncoderDelta();
                        if (menuIndex == 0) {
                            tempHour = tempMinute = 0;
                            currentState = STATE_ADD_HOUR;
                        } else if (menuIndex == 1) {
                            selectedDeleteIndex = 0;
                            currentState = (loaded == 0) ? STATE_DEFAULT : STATE_DELETE;
                        } else if (menuIndex == 2) {
                            viewScrollIndex = 0;
                            currentState = (loaded == 0) ? STATE_DEFAULT : STATE_VIEW_SCHEDULE;
                        } else {
                            currentState = STATE_DEFAULT;
                        }
                        break;

                    case STATE_ADD_HOUR:
                        readEncoderDelta();
                        currentState = STATE_ADD_MINUTE;
                        break;

                    case STATE_ADD_MINUTE:
                        // Tampilkan konfirmasi dulu sebelum save
                        readEncoderDelta();
                        currentState = STATE_ADD_CONFIRM;
                        break;

                    case STATE_ADD_CONFIRM:
                        // OK = simpan jadwal
                        if (loaded < MAX_TIMES) {
                            schedTimes[loaded][0] = tempHour;
                            schedTimes[loaded][1] = tempMinute;
                            schedTimes[loaded][2] = 0;
                            loaded++;
                            saveSettings(schedTimes, loaded);
                            Serial.printf("[JADWAL] Ditambah: %02d:%02d, total=%d\n",
                                          tempHour, tempMinute, (int)loaded);
                            // Tampilkan konfirmasi singkat di LCD
                            if (xSemaphoreTake(lcdMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
                                lcd.clear();
                                lcd.setCursor(0, 0); lcd.print("TERSIMPAN!      ");
                                snprintf(buf, sizeof(buf), "Jadwal: %02d:%02d   ", tempHour, tempMinute);
                                lcd.setCursor(0, 1); lcd.print(buf);
                                xSemaphoreGive(lcdMutex);
                            }
                            vTaskDelay(pdMS_TO_TICKS(1500));  // tahan layar konfirmasi
                        } else {
                            Serial.println("[JADWAL] PENUH (max 20)!");
                        }
                        currentState = STATE_DEFAULT;
                        break;

                    case STATE_DELETE:
                        // Geser array hapus index terpilih
                        for (size_t i = selectedDeleteIndex; i + 1 < loaded; i++) {
                            schedTimes[i][0] = schedTimes[i+1][0];
                            schedTimes[i][1] = schedTimes[i+1][1];
                            schedTimes[i][2] = schedTimes[i+1][2];
                        }
                        if (loaded > 0) loaded--;
                        saveSettings(schedTimes, loaded);
                        selectedDeleteIndex = constrain(
                            selectedDeleteIndex, 0, loaded > 0 ? (int)loaded - 1 : 0);
                        // Konfirmasi hapus singkat
                        if (xSemaphoreTake(lcdMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
                            lcd.clear();
                            lcd.setCursor(0, 0); lcd.print("JADWAL DIHAPUS! ");
                            snprintf(buf, sizeof(buf), "Sisa: %d jadwal  ", (int)loaded);
                            lcd.setCursor(0, 1); lcd.print(buf);
                            xSemaphoreGive(lcdMutex);
                        }
                        vTaskDelay(pdMS_TO_TICKS(1200));
                        currentState = STATE_DEFAULT;
                        break;

                    case STATE_VIEW_SCHEDULE:
                        break;  // OK di view tidak ada aksi

                    default: break;
                }
            }
        }

        // ── Rotasi encoder → ubah nilai ───────────────────────────
        if (encDelta != 0) {
            switch (currentState) {
                case STATE_MENU:
                    menuIndex = (menuIndex + encDelta + 4) % 4;
                    break;
                case STATE_ADD_HOUR: {
                    int h = (int)tempHour + encDelta;
                    if (h > 23) h = 0;
                    if (h < 0)  h = 23;
                    tempHour = (uint8_t)h;
                    break;
                }
                case STATE_ADD_MINUTE: {
                    int m = (int)tempMinute + encDelta;
                    if (m > 59) m = 0;
                    if (m < 0)  m = 59;
                    tempMinute = (uint8_t)m;
                    break;
                }
                case STATE_DELETE:
                    if (loaded > 0)
                        selectedDeleteIndex = constrain(
                            selectedDeleteIndex + encDelta, 0, (int)loaded - 1);
                    break;
                case STATE_VIEW_SCHEDULE:
                    if (loaded > 0)
                        viewScrollIndex = constrain(
                            viewScrollIndex + encDelta, 0, (int)loaded - 1);
                    break;
                default: break;
            }
        }

        // ── Render LCD ────────────────────────────────────────────
        if (xSemaphoreTake(lcdMutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            if (lastState != currentState) {
                lcd.clear();
                lastState = currentState;
            }

            switch (currentState)
            {
                // ── DEFAULT ──────────────────────────────────────
                case STATE_DEFAULT: {
                    // Baris 0: "HH:MM:SS  XXX%"
                    snprintf(buf, sizeof(buf), "%02d:%02d:%02d  %3d%%",
                             curH, curM, curS, feedLevelPct);
                    lcd.setCursor(0, 0); lcd.print(buf);

                    // Baris 1: jadwal terdekat atau pesan
                    lcd.setCursor(0, 1);
                    // Peringatan pakan habis (kedip tiap 2 detik)
                    if (feedLevelPct <= 20 && (curS % 2 == 0)) {
                        lcd.print("!! PAKAN HABIS!!");
                    } else if (loaded > 0) {
                        // Cari jadwal terdekat
                        int nowMins  = curH * 60 + curM;
                        int bestIdx  = 0;
                        int bestDiff = 9999;
                        for (size_t i = 0; i < loaded; i++) {
                            int sMins = schedTimes[i][0] * 60 + schedTimes[i][1];
                            int diff  = sMins - nowMins;
                            if (diff <= 0) diff += 1440;
                            if (diff < bestDiff) { bestDiff = diff; bestIdx = (int)i; }
                        }
                        snprintf(buf, sizeof(buf), "Next:%02d:%02d (%d/%d) ",
                                 schedTimes[bestIdx][0], schedTimes[bestIdx][1],
                                 (int)loaded, (int)MAX_TIMES);
                        lcd.print(buf);
                    } else {
                        lcd.print("Belum ada jadwal");
                    }
                    break;
                }

                // ── MENU ─────────────────────────────────────────
                case STATE_MENU: {
                    const char *items[4] = {
                        "Tambah Jadwal",
                        "Hapus Jadwal ",
                        "Lihat Jadwal ",
                        "< Back       "
                    };
                    // Baris 0 = item terpilih (dengan >), baris 1 = item berikutnya
                    snprintf(buf, sizeof(buf), ">%-15s", items[menuIndex]);
                    lcd.setCursor(0, 0); lcd.print(buf);
                    snprintf(buf, sizeof(buf), " %-15s", items[(menuIndex + 1) % 4]);
                    lcd.setCursor(0, 1); lcd.print(buf);
                    break;
                }

                // ── SET JAM ──────────────────────────────────────
                case STATE_ADD_HOUR:
                    lcd.setCursor(0, 0); lcd.print("Set Jam: [putar]");
                    snprintf(buf, sizeof(buf), "  >> [%02d] :  -- ", tempHour);
                    lcd.setCursor(0, 1); lcd.print(buf);
                    break;

                // ── SET MENIT ────────────────────────────────────
                case STATE_ADD_MINUTE:
                    lcd.setCursor(0, 0); lcd.print("Set Menit:[putar");
                    snprintf(buf, sizeof(buf), "  >> %02d : [%02d]  ", tempHour, tempMinute);
                    lcd.setCursor(0, 1); lcd.print(buf);
                    break;

                // ── KONFIRMASI SIMPAN ─────────────────────────────
                case STATE_ADD_CONFIRM:
                    lcd.setCursor(0, 0); lcd.print("Simpan? OK=ya   ");
                    snprintf(buf, sizeof(buf), "  Jam %02d:%02d     ", tempHour, tempMinute);
                    lcd.setCursor(0, 1); lcd.print(buf);
                    break;

                // ── HAPUS JADWAL ─────────────────────────────────
                case STATE_DELETE:
                    lcd.setCursor(0, 0); lcd.print("Hapus? OK=hapus ");
                    if (loaded > 0) {
                        snprintf(buf, sizeof(buf), "[%02d:%02d]  %2d/%-2d  ",
                                 schedTimes[selectedDeleteIndex][0],
                                 schedTimes[selectedDeleteIndex][1],
                                 selectedDeleteIndex + 1, (int)loaded);
                        lcd.setCursor(0, 1); lcd.print(buf);
                    }
                    break;

                // ── LIHAT JADWAL ─────────────────────────────────
                case STATE_VIEW_SCHEDULE: {
                    // Baris 0: jadwal terpilih
                    snprintf(buf, sizeof(buf), "%2d> %02d:%02d        ",
                             viewScrollIndex + 1,
                             schedTimes[viewScrollIndex][0],
                             schedTimes[viewScrollIndex][1]);
                    lcd.setCursor(0, 0); lcd.print(buf);
                    // Baris 1: jadwal berikutnya atau total
                    int ni = viewScrollIndex + 1;
                    if (ni < (int)loaded) {
                        snprintf(buf, sizeof(buf), "%2d  %02d:%02d        ",
                                 ni + 1, schedTimes[ni][0], schedTimes[ni][1]);
                    } else {
                        snprintf(buf, sizeof(buf), "-- Total: %d/%-2d --", (int)loaded, (int)MAX_TIMES);
                    }
                    lcd.setCursor(0, 1); lcd.print(buf);
                    break;
                }

                // ── FEEDING (set oleh TaskServo) ──────────────────
                case STATE_MANUAL_FEED:
                    break;
            }

            xSemaphoreGive(lcdMutex);
        }

        vTaskDelay(pdMS_TO_TICKS(80));
    }
}

// ─────────────────────────────────────────────
// Task: Post Status (core 0)
// Mengirim status servo ke server di background
// agar tidak delay servo saat ditekan
// ─────────────────────────────────────────────
void TaskPostStatus(void *pvParameters)
{
    (void)pvParameters;
    while (1) {
        // Tunggu sinyal dari TaskServo
        if (xSemaphoreTake(postStatusSem, portMAX_DELAY) == pdTRUE) {
            int s = pendingStatus;
            Serial.printf("[STATUS] Background post: status=%d\n", s);
            postStatus(s);
        }
    }
}

// ─────────────────────────────────────────────
// Task: Post Data (core 0)
// ─────────────────────────────────────────────
void TaskPostData(void *pvParameters)
{
    (void)pvParameters;
    uint8_t h, m, s;
    char timeStr[20];
    while (1) {
        getLocalTime(h, m, s);
        if ((h != lastPostHour || m != lastPostMinute) && isScheduledTime(h, m)) {
            snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d", h, m, s);
            postData(modelName, timeStr, feedLevelPct);
            lastPostHour   = h;
            lastPostMinute = m;
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ─────────────────────────────────────────────
// ISR: Button manual feed
// ─────────────────────────────────────────────
void IRAM_ATTR ISR_BtnMain()
{
    unsigned long now = millis();
    if (now - lastIRQ_main > 300) {
        if (currentState == STATE_DEFAULT) {
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            xSemaphoreGiveFromISR(manualFeedSem, &xHigherPriorityTaskWoken);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
        lastIRQ_main = now;
    }
}