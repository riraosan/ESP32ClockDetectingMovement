/*
The MIT License (MIT)

Copyright (c) 2020-2021 riraosan.github.io

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#define TS_ENABLE_SSL  // Don't forget it for ThingSpeak.h!!
#include <BME280Class.h>
#include <Button2.h>
#include <ESPUI.h>
#include <LED_DisPlay.h>
#include <SerialTelnetBridge.h>
#include <TM1637Display.h>
#include <ThingSpeak.h>
#include <Ticker.h>
#include <WiFiClientSecure.h>
#include <secrets.h>

#include <esp32_touch.hpp>
// For log
#include <esp32-hal-log.h>
// For WiFi Connection
#define HOSTNAME        "atom_clock"
#define AP_NAME         "ATOM-G-AP"
// For NTP Clock
#define TIME_ZONE       "JST-9"
#define NTP_SERVER1     "ntp.nict.jp"
#define NTP_SERVER2     "ntp.jst.mfeed.ad.jp"
#define NTP_SERVER3     ""
// For 7segLED
#define CLK             19
#define DIO             22
// For BME280
#define SDA             25
#define SCL             21
// For resetting WiFi
#define BUTTON_PIN      39
// For PIR Detection
#define PIR_SENSOR_PIN  23
// For Enable LED Display
#define TOUCH_IO_TOGGLE 8  // GPIO33
#define TOUCH_THRESHOLD 92

ESP32Touch touch;
LED_DisPlay led;

Button2 button     = Button2(BUTTON_PIN);
Button2 pir_sensor = Button2(PIR_SENSOR_PIN);

Ticker clocker;
Ticker sensorChecker;
Ticker tempeChecker;
Ticker humidChecker;
Ticker pressChecker;

TM1637Display display(CLK, DIO);

WiFiClientSecure _client;

bool detecting = false;
bool sendData  = false;

long motionTime;
int motionCount;

unsigned long myChannelNumber = SECRET_CH_ID;
const char* myWriteAPIKey     = SECRET_WRITE_APIKEY;
const char* certificate       = SECRET_TS_ROOT_CA;

float temperature;
float humidity;
float pressure;

uint16_t alarm_hour;
uint16_t alarm_min;
uint16_t enable_alarm;

void sendThingSpeakChannel(float temperature, float humidity, float pressure) {
    char buffer1[16] = {0};
    char buffer2[16] = {0};
    char buffer3[16] = {0};

    sprintf(buffer1, "%2.1f", temperature);
    sprintf(buffer2, "%2.1f", humidity);
    sprintf(buffer3, "%4.1f", pressure);

    String tempe(buffer1);
    String humid(buffer2);
    String press(buffer3);

    ThingSpeak.setField(1, tempe);
    ThingSpeak.setField(2, humid);
    ThingSpeak.setField(3, press);

    // write to the ThingSpeak channel
    int code = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
    if (code == 200)
        log_d("Channel update successful.");
    else
        log_d("Problem updating channel. HTTP error code %d", code);
}

void sendMotionTime(long time) {
    float min = time / 1000 / 60;

    ThingSpeak.setField(4, min);  // ms to min

    // write to the ThingSpeak channel
    int code = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
    if (code == 200)
        log_d("Channel update successful.");
    else
        log_d("Problem updating channel. HTTP error code %d", code);
}

void sendMotionCounts(int counts) {
    ThingSpeak.setField(5, counts);

    // write to the ThingSpeak channel
    int code = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
    if (code == 200)
        log_d("Channel update successful.");
    else
        log_d("Problem updating channel. HTTP error code %d", code);
}

void printTemperatureLED(float value) {
    char buffer[16] = {0};
    sprintf(buffer, "0x%2.0fC", value);

    display.clear();
    display.showNumberHexEx(strtol(buffer, 0, 16), (0x80 >> 2), false, 3, 1);
}

void printHumidityLED(float value) {
    char buffer[16] = {0};

    sprintf(buffer, "%2f", value);
    String humidLed(buffer);
    display.clear();
    display.showNumberDecEx(humidLed.toInt(), (0x80 >> 0), false);
}

void printPressureLED(float value) {
    char buffer[16] = {0};

    sprintf(buffer, "%4f", value);
    String pressLed(buffer);
    display.clear();
    display.showNumberDecEx(pressLed.toInt(), (0x80 >> 0), false);
}

void _checkSensor(void) { sendData = true; }

String getLEDTime(void) {
    time_t t      = time(NULL);
    struct tm* tm = localtime(&t);

    char buffer[16] = {0};
    sprintf(buffer, "%02d%02d", tm->tm_hour, tm->tm_min);

    return String(buffer);
}

String getTime(void) {
    time_t t      = time(NULL);
    struct tm* tm = localtime(&t);

    char buffer[128] = {0};
    sprintf(buffer, "%04d-%02d-%02dT%02d:%02d:%02d+0900", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);

    return String(buffer);
}

void displayClock(void) {
    static uint8_t flag = 0;
    flag                = ~flag;

    if (flag)
        display.showNumberDecEx(getLEDTime().toInt(), (0x80 >> 2), true);
    else
        display.showNumberDecEx(getLEDTime().toInt(), (0x80 >> 4), true);
}

void initClock(void) {
    configTzTime(TIME_ZONE, NTP_SERVER1, NTP_SERVER2, NTP_SERVER3);
}

void selectAlarmAMPM(Control* sender, int value) {
    log_i("Select: ID: %d Value: %d", sender->id, sender->value);
}

void selectAlarmHour(Control* sender, int value) {
    log_i("Select: ID: %d Value: %d", sender->id, sender->value);
}

void selectAlarmMinuite(Control* sender, int value) {
    log_i("Select: ID: %d Value: %d", sender->id, sender->value);
}

void switchAlarmEnable(Control* sender, int value) {
    log_i("Select: ID: %d Value: %d", sender->id, sender->value);
}

void initESPUI(void) {
    ESPUI.setVerbosity(Verbosity::Quiet);

    uint16_t tab2 = ESPUI.addControl(ControlType::Tab, "Alarm Settings", "Alarm Settings");
    uint16_t tab1 = ESPUI.addControl(ControlType::Tab, "Network Information", "Network Information");

    //Nwtwork Settings infomation
    ESPUI.addControl(ControlType::Label, "WiFi SSID", WiFi.SSID(), ControlColor::Sunflower, tab1);
    ESPUI.addControl(ControlType::Label, "ESP32 MAC Address", WiFi.macAddress(), ControlColor::Sunflower, tab1);
    ESPUI.addControl(ControlType::Label, "ESP32 IP Address", WiFi.localIP().toString(), ControlColor::Sunflower, tab1);
    ESPUI.addControl(ControlType::Label, "ESP32 Host Name", HOSTNAME, ControlColor::Sunflower, tab1);

    //Alarm Settings
    uint16_t select1 = ESPUI.addControl(ControlType::Select, "AM/PM", "1", ControlColor::Alizarin, tab2, &selectAlarmAMPM);
    ESPUI.addControl(ControlType::Option, "AM", "AM", ControlColor::Alizarin, select1);
    ESPUI.addControl(ControlType::Option, "PM", "PM", ControlColor::Alizarin, select1);

    uint16_t select2 = ESPUI.addControl(ControlType::Select, "Hour", "1", ControlColor::Alizarin, tab2, &selectAlarmHour);
    ESPUI.addControl(ControlType::Option, "1", "1", ControlColor::Alizarin, select2);
    ESPUI.addControl(ControlType::Option, "2", "2", ControlColor::Alizarin, select2);
    ESPUI.addControl(ControlType::Option, "3", "3", ControlColor::Alizarin, select2);
    ESPUI.addControl(ControlType::Option, "4", "4", ControlColor::Alizarin, select2);
    ESPUI.addControl(ControlType::Option, "5", "5", ControlColor::Alizarin, select2);
    ESPUI.addControl(ControlType::Option, "6", "6", ControlColor::Alizarin, select2);
    ESPUI.addControl(ControlType::Option, "7", "7", ControlColor::Alizarin, select2);
    ESPUI.addControl(ControlType::Option, "8", "8", ControlColor::Alizarin, select2);
    ESPUI.addControl(ControlType::Option, "9", "9", ControlColor::Alizarin, select2);
    ESPUI.addControl(ControlType::Option, "10", "10", ControlColor::Alizarin, select2);
    ESPUI.addControl(ControlType::Option, "11", "11", ControlColor::Alizarin, select2);
    ESPUI.addControl(ControlType::Option, "12", "12", ControlColor::Alizarin, select2);

    uint16_t select3 = ESPUI.addControl(ControlType::Select, "Minuets", "0", ControlColor::Alizarin, tab2, &selectAlarmMinuite);
    ESPUI.addControl(ControlType::Option, "0", "0", ControlColor::Alizarin, select3);
    ESPUI.addControl(ControlType::Option, "10", "10", ControlColor::Alizarin, select3);
    ESPUI.addControl(ControlType::Option, "20", "20", ControlColor::Alizarin, select3);
    ESPUI.addControl(ControlType::Option, "30", "30", ControlColor::Alizarin, select3);
    ESPUI.addControl(ControlType::Option, "40", "40", ControlColor::Alizarin, select3);
    ESPUI.addControl(ControlType::Option, "50", "50", ControlColor::Alizarin, select3);

    ESPUI.addControl(ControlType::Switcher, "Alarm ON/OFF", "", ControlColor::None, tab2, &switchAlarmEnable);

    ESPUI.begin("ATOM NTP Clock");
}

void displayOn(void) {
    display.setBrightness(7, true);
    display.clear();
}

void displayOff(void) {
    display.setBrightness(7, false);
    display.clear();
}

void initBME280(void) {
    bme280.setup(SDA, SCL, MODE::WEATHER_STATION);
    sensorChecker.attach(60, _checkSensor);
}

void connecting(void) {
    static uint8_t flag = 0;
    flag                = ~flag;

    if (flag)
        display.showNumberDecEx(0, (0x80 >> 3), false);
    else
        display.showNumberDecEx(0, (0x80 >> 4), false);
}

void released(Button2& btn) {
    WiFi.disconnect(true, true);
    ESP.restart();
}

void pirDetected(Button2& btn) {
    detecting = true;
    log_d("--- detected.");
}

void pirReleased(Button2& btn) {
    motionTime = btn.wasPressedFor();
    log_d("released: %d", motionTime);
}

void initButton(void) { button.setReleasedHandler(released); }

void initPIRSensor(void) { pir_sensor.setReleasedHandler(pirReleased); }

void fadeInDisplay(uint32_t ms) {
    uint32_t period = ms / 18;

    display.setBrightnessEx(0, false);
    delay(period);

    for (int i = 0; i < 8; i++) {
        display.setBrightnessEx(i, true);
        delay(period);
    }
}

void fadeOutDisplay(uint32_t ms) {
    uint32_t period = ms / 18;

    for (int i = 7; - 1 < i; i--) {
        display.setBrightnessEx(i, true);
        delay(period);
    }

    display.setBrightnessEx(0, false);
    delay(period);
}

void fadeInOutDisplay(uint32_t ms) {
    fadeInDisplay(ms);
    fadeOutDisplay(ms);
}

void initThingSpeak(void) {
    _client.setCACert(certificate);
    ThingSpeak.begin(_client);
}

void showEnvData(void) {
    printTemperatureLED(temperature);
    fadeInOutDisplay(1.5 * 1000);

    printHumidityLED(humidity);
    fadeInOutDisplay(1.5 * 1000);

    printPressureLED(pressure);
    fadeInOutDisplay(1.5 * 1000);
}

void sendThingSpeakData(void) {
    if (bme280.getTemperature(temperature) && bme280.getHumidity(humidity) && bme280.getPressure(pressure)) {
        sendThingSpeakChannel(temperature, humidity, pressure);
    } else {
        log_e("temperature = %f, humidity = %f, pressure = %f", temperature, humidity, pressure);
    }
}

void setNtpClockNetworkInfo(void) {
    char buffer[255] = {0};

    sprintf(buffer, "%s %s %s", WiFi.SSID().c_str(), WiFi.macAddress().c_str(), WiFi.localIP().toString().c_str());
    String networkInfo(buffer);

    ThingSpeak.setStatus(networkInfo);  //ThingSpeak limits this to 255 bytes.
}

void initTouchSensor(void) {
    static uint8_t toggle = 0;

    touch.configure_input(TOUCH_IO_TOGGLE, TOUCH_THRESHOLD, []() {
        log_d("Toggling Clock LED");

        if (toggle) {
            displayOff();
            showEnvData();
            displayOn();
            clocker.attach_ms(500, displayClock);
        } else {
            clocker.detach();
            displayOff();
        }

        toggle = ~toggle;
    });

    touch.begin();
}

void initLED(void) {
    led.begin(1);  // for ATOM Lite
    led.setTaskName("ATOM_LITE_LED");
    led.setTaskPriority(2);
    led.start();
    delay(50);
    led.setBrightness(30);
}

void setup(void) {
    initLED();
    led.drawpix(0, CRGB::Red);

    displayOn();

    STB.setWiFiConnectChecker(connecting);
    STB.setHostname(HOSTNAME);
    STB.setApName(AP_NAME);
    STB.begin(false, false);

    displayOff();

    initBME280();

    initClock();
    initButton();
    initPIRSensor();
    initTouchSensor();
    initThingSpeak();
    initESPUI();

    led.drawpix(0, CRGB::Green);

    setNtpClockNetworkInfo();
    sendThingSpeakData();

    showEnvData();
    displayOn();
    clocker.attach_ms(500, displayClock);
}

void loop(void) {
    if (STB.handle() == false) {
        button.loop();
        pir_sensor.loop();

        if (motionTime) {
            sendMotionTime(motionTime);
            motionTime = 0;
            delay(15 * 1000);
        }

        if (sendData) {
            sendThingSpeakData();
            sendData = false;
            delay(15 * 1000);
        }
    }

    yield();
}
