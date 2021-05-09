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

#define TS_ENABLE_SSL //Don't forget it for ThingSpeak.h!!
#include <SerialTelnetBridge.h>
#include <Ticker.h>
#include <ESPUI.h>
#include <TM1637Display.h>
#include <BME280Class.h>
#include <Button2.h>
#include <WiFiClientSecure.h>
#include <ThingSpeak.h>
#include <secrets.h>
#include <esp32_touch.hpp>
#include <LED_DisPlay.h>
//For log
#include <esp32-hal-log.h>
//For WiFi Connection
#define HOSTNAME "atom_clock"
#define AP_NAME "ATOM-G-AP"
//For NTP Clock
#define TIME_ZONE "JST-9"
#define NTP_SERVER1 "ntp.nict.jp"
#define NTP_SERVER2 "ntp.jst.mfeed.ad.jp"
#define NTP_SERVER3 ""
//For 7segLED
#define CLK 19
#define DIO 22
//For BME280
#define SDA 25
#define SCL 21
//For Light Sleep(not use)
#define uS_TO_S_FACTOR 1000000ULL /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP 48          /* Time ESP32 will go to sleep (in seconds) */
//For resetting WiFi
#define BUTTON_PIN 39
//For PIR Detection
#define PIR_SENSOR_PIN 23
//For Enable LED Display
#define TOUCH_IO_TOGGLE 8 // GPIO33
#define TOUCH_THRESHOLD 92

ESP32Touch touch;
LED_DisPlay led;

Button2 button = Button2(BUTTON_PIN);
Button2 pir_sensor = Button2(PIR_SENSOR_PIN);
Ticker clocker;
Ticker sensorChecker;
Ticker tempeChecker;
Ticker humidChecker;
Ticker pressChecker;

TM1637Display display(CLK, DIO);

WiFiClientSecure _client;

uint16_t timeLabelId;
uint16_t temperatureLabelId;
uint16_t humidityLabelId;
uint16_t pressureLabelId;

bool detecting = false;
bool sendData = false;

long motionTime;
int motionCount;

unsigned long myChannelNumber = SECRET_CH_ID;
const char *myWriteAPIKey = SECRET_WRITE_APIKEY;
const char *certificate = SECRET_TS_ROOT_CA;

float temperature;
float humidity;
float pressure;

void sendThingSpeakChannel(float temperature, float humidity, float pressure)
{
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

void sendMotionTime(long time)
{
    ThingSpeak.setField(4, time / 1000); //ms to sec

    // write to the ThingSpeak channel
    int code = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
    if (code == 200)
        log_d("Channel update successful.");
    else
        log_d("Problem updating channel. HTTP error code %d", code);
}

void sendMotionCounts(int counts)
{
    ThingSpeak.setField(5, counts);

    // write to the ThingSpeak channel
    int code = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
    if (code == 200)
        log_d("Channel update successful.");
    else
        log_d("Problem updating channel. HTTP error code %d", code);
}

void printTemperatureLED(float value)
{
    char buffer[16] = {0};
    sprintf(buffer, "0x%2.0fC", value);

    display.clear();
    display.showNumberHexEx(strtol(buffer, 0, 16), (0x80 >> 2), false, 3, 1);
}

void printTemperatureESPUI(float value)
{
    char buffer[16] = {0};

    sprintf(buffer, "%2.1f℃", value);
    String tempUI(buffer);
    ESPUI.updateControlValue(temperatureLabelId, tempUI); //TODO
}

void printHumidityLED(float value)
{
    char buffer[16] = {0};

    sprintf(buffer, "%2f", value);
    String humidLed(buffer);
    display.clear();
    display.showNumberDecEx(humidLed.toInt(), (0x80 >> 0), false);
}

void printHumidityESPUI(float value)
{
    char buffer[16] = {0};

    sprintf(buffer, "%2.1f%%", value);
    String humidUI(buffer);
    ESPUI.updateControlValue(humidityLabelId, humidUI);
}

void printPressureLED(float value)
{
    char buffer[16] = {0};

    sprintf(buffer, "%4f", value);
    String pressLed(buffer);
    display.clear();
    display.showNumberDecEx(pressLed.toInt(), (0x80 >> 0), false);
}

void printPressureESPUI(float value)
{
    char buffer[16] = {0};

    sprintf(buffer, "%2.1fhPa", value);
    String pressUI(buffer);
    ESPUI.updateControlValue(pressureLabelId, pressUI);
}

void _checkTemperature(void)
{
    float value = bme280.getTemperature();

    printTemperatureESPUI(value);
}

void _checkHumidity(void)
{
    float value = bme280.getHumidity();

    printHumidityESPUI(value);
}

void _checkPressure(void)
{
    float value = bme280.getPressure();

    printPressureESPUI(value);
}

void _checkSensor(void)
{
    sendData = true;
}

String getLEDTime(void)
{
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);

    char buffer[16] = {0};
    sprintf(buffer, "%02d%02d", tm->tm_hour, tm->tm_min);

    return String(buffer);
}

String getTime(void)
{
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);

    char buffer[128] = {0};
    sprintf(buffer, "%04d-%02d-%02dT%02d:%02d:%02d+0900",
            tm->tm_year + 1900,
            tm->tm_mon + 1,
            tm->tm_mday,
            tm->tm_hour,
            tm->tm_min,
            tm->tm_sec);

    return String(buffer);
}

void _clock(void)
{
    ESPUI.updateControlValue(timeLabelId, getTime());
}

void initClock(void)
{
    configTzTime(TIME_ZONE, NTP_SERVER1, NTP_SERVER2, NTP_SERVER3);
    clocker.attach_ms(500, _clock);
}

void initESPUI(void)
{
    ESPUI.setVerbosity(Verbosity::Quiet);

    uint16_t tab1 = ESPUI.addControl(ControlType::Tab, "Date & Time", "Date & Time");
    uint16_t tab2 = ESPUI.addControl(ControlType::Tab, "Weather Station", "Weather Station");

    timeLabelId = ESPUI.addControl(ControlType::Label, "[ Date & Time ]", "0", ControlColor::Emerald, tab1);
    temperatureLabelId = ESPUI.addControl(ControlType::Label, "[ Temperature ]", "0", ControlColor::Sunflower, tab2);
    humidityLabelId = ESPUI.addControl(ControlType::Label, "[ Humidity ]", "0", ControlColor::Sunflower, tab2);
    pressureLabelId = ESPUI.addControl(ControlType::Label, "[ Pressure ]", "0", ControlColor::Sunflower, tab2);

    ESPUI.begin("ESP32 NTP Clock");
}

void displayOn(void)
{
    display.setBrightness(7, true);
    display.clear();
}

void displayOff(void)
{
    display.setBrightness(7, false);
    display.clear();
}

void initBME280(void)
{
    bme280.setup(SDA, SCL);
    sensorChecker.attach(60, _checkSensor);
}

void initLightSleep(void)
{
    esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
    log_d("Setup ESP32 to sleep for every %s Seconds", String(TIME_TO_SLEEP).c_str());
}

void connecting(void)
{
    static uint8_t flag = 0;
    flag = ~flag;

    if (flag)
        display.showNumberDecEx(0, (0x80 >> 3), false);
    else
        display.showNumberDecEx(0, (0x80 >> 4), false);
}

void released(Button2 &btn)
{
    WiFi.disconnect(true, true);
    ESP.restart();
}

void pirDetected(Button2 &btn)
{
    log_d("--- detected.");
    detecting = true;
}

void pirReleased(Button2 &btn)
{
    motionTime = btn.wasPressedFor();
    log_d("released: %d", motionTime);
}

void initButton(void)
{
    button.setReleasedHandler(released);
}

void initPIRSensor(void)
{
    pir_sensor.setPressedHandler(pirDetected);
    pir_sensor.setReleasedHandler(pirReleased);
}

void fadeInDisplay(uint32_t ms)
{
    uint32_t period = ms / 18;

    display.setBrightnessEx(0, false);
    delay(period);

    //log_d("%d", period);

    for (int i = 0; i < 8; i++)
    {
        display.setBrightnessEx(i, true);
        delay(period);
        //log_d("%d %d", period, i);
    }
}

void fadeOutDisplay(uint32_t ms)
{
    uint32_t period = ms / 18;

    for (int i = 7; - 1 < i; i--)
    {
        display.setBrightnessEx(i, true);
        delay(period);
        //log_d("%d %d", period, i);
    }

    display.setBrightnessEx(0, false);
    delay(period);
    //log_d("%d", period);
}

void fadeInOutDisplay(uint32_t ms)
{
    fadeInDisplay(ms);
    fadeOutDisplay(ms);
}

void initThingSpeak(void)
{
    _client.setCACert(certificate);
    ThingSpeak.begin(_client);
}

void showClock(void)
{
    display.showNumberDecEx(getLEDTime().toInt(), (0x80 >> 2), true);
    fadeInOutDisplay(2 * 1000);
    printTemperatureLED(temperature);
    fadeInOutDisplay(1.5 * 1000);
    printHumidityLED(humidity);
    fadeInOutDisplay(1.5 * 1000);
    printPressureLED(pressure);
    fadeInOutDisplay(1.5 * 1000);
}

void sendThingSpeakData(void)
{
    temperature = bme280.getTemperature();
    humidity = bme280.getHumidity();
    pressure = bme280.getPressure();

    if (temperature != NAN && humidity != NAN && pressure != NAN)
    {
        printTemperatureESPUI(temperature);
        printHumidityESPUI(humidity);
        printPressureESPUI(pressure);

        sendThingSpeakChannel(temperature, humidity, pressure);
    }
    else
    {
        log_e("temperature = %f, humidity = %f, pressure = %f", temperature, humidity, pressure);
    }
}

void initTouchSensor(void)
{
    touch.configure_input(TOUCH_IO_TOGGLE, TOUCH_THRESHOLD, []() {
        log_d("Toggling the LED");
        showClock();
    });

    touch.begin();
}

void initLED(void)
{
    led.begin(1); //for ATOM Lite
    led.setTaskName("ATOM_LITE_LED");
    led.setTaskPriority(2);
    led.start();
    delay(50);
    led.setBrightness(30);
}

void setup(void)
{
    initLED();
    led.drawpix(0, CRGB::Red);

    displayOn();

    STB.setWiFiConnectChecker(connecting);
    STB.setHostname(HOSTNAME);
    STB.setApName(AP_NAME);
    STB.begin(false, false);

    displayOff();

    initClock();
    initESPUI();
    initBME280();
    initButton();
    initPIRSensor();
    //initTouchSensor();
    initThingSpeak();

    led.drawpix(0, CRGB::Green);

    sendThingSpeakData();
    showClock();
}

void loop(void)
{
    if (STB.handle() == false)
    {
        button.loop();
        pir_sensor.loop();

        if (sendData)
        {
            sendThingSpeakData();
            sendData = false;
            motionCount = 0;
        }

        // if (detecting)
        // {
        //     display.showDotsEx(0x80 >> 0);
        //     display.setBrightnessEx(1, true);

        //     motionCount++;
        //     sendMotionCounts(motionCount);
        //     detecting = false;
        // }

        if (motionTime)
        {
            display.showDotsEx(0x80 >> 4);
            display.setBrightnessEx(1, true);

            sendMotionTime(motionTime);
            motionTime = 0;
        }
    }

    yield();
}
