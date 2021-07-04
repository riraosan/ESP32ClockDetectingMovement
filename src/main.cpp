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
#include <Arduino.h>
#include <AutoConnect.h>
#include <BME280Class.h>
#include <Button2.h>
#include <ESPUI.h>
#include <ESPmDNS.h>
#include <LED_DisPlay.h>
#include <TM1637Display.h>
#include <ThingSpeak.h>
#include <Ticker.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <secrets.h>
#include <timezone.h>

#include <esp32_touch.hpp>
// log
#include <esp32-hal-log.h>
// WiFi Connection
#define HOSTNAME        "atom_clock"
#define AP_NAME         "ATOM-G-AP"
// NTP Clock
#define TIME_ZONE       "JST-9"
#define NTP_SERVER1     "ntp.nict.jp"
#define NTP_SERVER2     "ntp.jst.mfeed.ad.jp"
#define NTP_SERVER3     "asia.pool.ntp.org"
// 7segLED TM1637
#define CLK             19
#define DIO             22
// BME280
#define SDA             25
#define SCL             21
// WiFi reset
#define BUTTON_PIN      39
// PIR Detection
#define PIR_SENSOR_PIN  23
// Enable/Disable LED Display
#define TOUCH_IO_TOGGLE 8  // GPIO33
#define TOUCH_THRESHOLD 92
#define HTTP_PORT       80

WebServer Server;
AutoConnect Portal(Server);
AutoConnectConfig Config;  // Enable autoReconnect supported on v0.9.4
AutoConnectAux Timezone;

Ticker clocker;
Ticker sensorChecker;
Ticker blockOFF;
Ticker blockON;

ESP32Touch touch;
LED_DisPlay led;
Button2 button     = Button2(BUTTON_PIN);
Button2 pir_sensor = Button2(PIR_SENSOR_PIN);
TM1637Display display(CLK, DIO);
WiFiClientSecure _client;

bool sendDataflag    = false;
float motionTime     = 0;
bool motionDetecting = false;
bool blockflag       = true;
bool sendMotionflag  = false;

unsigned long myChannelNumber = SECRET_CH_ID;
const char* myWriteAPIKey     = SECRET_WRITE_APIKEY;
const char* certificate       = SECRET_TS_ROOT_CA;

float temperature;
float humidity;
float pressure;

uint16_t alarm_hour;
uint16_t alarm_min;
uint16_t enable_alarm;

void rootPage(void) {
    String content =
        "<html>"
        "<head>"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<script type=\"text/javascript\">"
        "setTimeout(\"location.reload()\", 1000);"
        "</script>"
        "</head>"
        "<body>"
        "<h2 align=\"center\" style=\"color:blue;margin:20px;\">Hello, world</h2>"
        "<h3 align=\"center\" style=\"color:gray;margin:10px;\">{{DateTime}}</h3>"
        "<p style=\"text-align:center;\">Reload the page to update the time.</p>"
        "<p></p><p style=\"padding-top:15px;text-align:center\">" AUTOCONNECT_LINK(COG_24) "</p>"
                                                                                           "</body>"
                                                                                           "</html>";
    static const char* wd[7] = {"Sun", "Mon", "Tue", "Wed", "Thr", "Fri", "Sat"};
    struct tm* tm;
    time_t t;
    char dateTime[26];

    t  = time(NULL);
    tm = localtime(&t);
    sprintf(dateTime, "%04d/%02d/%02d(%s) %02d:%02d:%02d.",
            tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
            wd[tm->tm_wday],
            tm->tm_hour, tm->tm_min, tm->tm_sec);
    content.replace("{{DateTime}}", String(dateTime));
    Server.send(200, "text/html", content);
}

void startPage(void) {
    // Retrieve the value of AutoConnectElement with arg function of WebServer class.
    // Values are accessible with the element name.
    String tz = Server.arg("timezone");

    for (uint8_t n = 0; n < sizeof(TZ) / sizeof(Timezone_t); n++) {
        String tzName = String(TZ[n].zone);
        if (tz.equalsIgnoreCase(tzName)) {
            configTime(TZ[n].tzoff * 3600, 0, TZ[n].ntpServer);
            log_d("Time zone: %s", tz.c_str());
            log_d("ntp server: %s", String(TZ[n].ntpServer).c_str());
            break;
        }
    }

    // The /start page just constitutes timezone,
    // it redirects to the root page without the content response.
    Server.sendHeader("Location", String("http://") + Server.client().localIP().toString() + String("/"));
    Server.send(302, "text/plain", "");
    Server.client().flush();
    Server.client().stop();
}

void otaPage(void) {
    String content = R"(
        <!DOCTYPE html>
        <html>
        <head>
        <meta charset="UTF-8" name="viewport" content="width=device-width, initial-scale=1">
        </head>
        <body>
        Place the root page with the sketch application.&ensp;
        __AC_LINK__
        </body>
        </html>
    )";

    content.replace("__AC_LINK__", String(AUTOCONNECT_LINK(COG_16)));
    Server.send(200, "text/html", content);
}

void displayOn(void) {
    display.clear();
    display.setBrightness(7, true);
}

void displayOff(void) {
    display.clear();
    display.setBrightness(7, false);
}

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

void _checkSensor(void) { sendDataflag = true; }

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
    uint8_t dots        = 0;
    static uint8_t flag = 0;
    flag                = ~flag;

    if (motionDetecting) {
        if (flag) {
            dots = (0x80 >> 2) | (0x80 >> 0);
        } else {
            dots = (0x80 >> 0);
        }
    } else {
        if (flag) {
            dots = (0x80 >> 2);
        } else {
            dots = 0;
        }
    }

    display.showNumberDecEx(getLEDTime().toInt(), dots, true);
}

void initClock(void) {
    configTzTime(TIME_ZONE, NTP_SERVER1, NTP_SERVER2, NTP_SERVER3);
}

void selectAlarmAMPM(Control* sender, int value) {
    log_i("Select: ID: %d Value: %s", sender->id, sender->value);
    if (sender->value == "1") {
        //Set AM
        display.addDots(0x80 >> 1);
    } else {
        //Set PM
        display.addDots(0x80 >> 3);
    }
}

void selectAlarmHour(Control* sender, int value) {
    log_i("Select: ID: %d Value: %s", sender->id, sender->value);
    long hours = sender->value.toInt();
    //set hour
}

void selectAlarmMinuite(Control* sender, int value) {
    log_i("Select: ID: %d Value: %s", sender->id, sender->value);
    long minuets = sender->value.toInt();
    //set minuets
}

void switchAlarmEnable(Control* sender, int value) {
    log_i("Select: ID: %d Value: %s", sender->id, sender->value);
    if (sender->value == "1") {
        //TODO calc second to alrm time
        //TODO create timer attach_once()
    } else {
        //TODO delete timer detach()
    }
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
    ESPUI.addControl(ControlType::Option, "AM", "1", ControlColor::Alizarin, select1);
    ESPUI.addControl(ControlType::Option, "PM", "2", ControlColor::Alizarin, select1);

    uint16_t select2 = ESPUI.addControl(ControlType::Select, "Hours", "12", ControlColor::Alizarin, tab2, &selectAlarmHour);
    ESPUI.addControl(ControlType::Option, "12", "12", ControlColor::Alizarin, select2);
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

    uint16_t select3 = ESPUI.addControl(ControlType::Select, "Minuets", "0", ControlColor::Alizarin, tab2, &selectAlarmMinuite);
    ESPUI.addControl(ControlType::Option, "0", "0", ControlColor::Alizarin, select3);
    ESPUI.addControl(ControlType::Option, "5", "5", ControlColor::Alizarin, select3);
    ESPUI.addControl(ControlType::Option, "10", "10", ControlColor::Alizarin, select3);
    ESPUI.addControl(ControlType::Option, "15", "15", ControlColor::Alizarin, select3);
    ESPUI.addControl(ControlType::Option, "20", "20", ControlColor::Alizarin, select3);
    ESPUI.addControl(ControlType::Option, "25", "25", ControlColor::Alizarin, select3);
    ESPUI.addControl(ControlType::Option, "30", "30", ControlColor::Alizarin, select3);
    ESPUI.addControl(ControlType::Option, "35", "35", ControlColor::Alizarin, select3);
    ESPUI.addControl(ControlType::Option, "40", "40", ControlColor::Alizarin, select3);
    ESPUI.addControl(ControlType::Option, "45", "45", ControlColor::Alizarin, select3);
    ESPUI.addControl(ControlType::Option, "50", "50", ControlColor::Alizarin, select3);
    ESPUI.addControl(ControlType::Option, "55", "55", ControlColor::Alizarin, select3);

    ESPUI.addControl(ControlType::Switcher, "Alarm ON/OFF", "0", ControlColor::Alizarin, tab2, &switchAlarmEnable);

    ESPUI.begin("ATOM NTP Clock");
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
    log_d("--- detected.");
    motionDetecting = true;
}
void pirReleased(Button2& btn) {
    motionTime += btn.wasPressedFor();
    log_d("--- released: %d", motionTime);
    motionDetecting = false;
    sendMotionflag  = true;
}

void initButton(void) { button.setReleasedHandler(released); }

void initPIRSensor(void) {
    pir_sensor.setPressedHandler(pirDetected);
    pir_sensor.setReleasedHandler(pirReleased);
}

void fadeInDisplay(uint32_t ms) {
    uint32_t period = ms / 18;

    display.setBrightness(0, false);
    delay(period);

    for (int i = 0; i < 8; i++) {
        display.setBrightness(i, true);
        delay(period);
    }
}

void fadeOutDisplay(uint32_t ms) {
    uint32_t period = ms / 18;

    for (int i = 7; - 1 < i; i--) {
        display.setBrightness(i, true);
        delay(period);
    }

    display.setBrightness(0, false);
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

void sendMotionTime(float time) {
    float min = time / 1000 / 60;

    ThingSpeak.setField(4, min);  // ms to min

    // write to the ThingSpeak channel
    int code = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
    if (code == 200)
        log_d("Channel update successful.");
    else
        log_d("Problem updating channel. HTTP error code %d", code);
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

void initAutoConnect(void) {
    Serial.begin(115200);
    // Enable saved past credential by autoReconnect option,
    // even once it is disconnected.
    Config.autoReconnect = true;
    Config.ota           = AC_OTA_BUILTIN;
    Portal.config(Config);

    // Load aux. page
    Timezone.load(AUX_TIMEZONE);
    // Retrieve the select element that holds the time zone code and
    // register the zone mnemonic in advance.
    AutoConnectSelect& tz = Timezone["timezone"].as<AutoConnectSelect>();
    for (uint8_t n = 0; n < sizeof(TZ) / sizeof(Timezone_t); n++) {
        tz.add(String(TZ[n].zone));
    }

    Portal.join({Timezone});  // Register aux. page

    // Behavior a root path of ESP8266WebServer.
    Server.on("/", rootPage);
    Server.on("/start", startPage);  // Set NTP server trigger handler
    Server.on("/ota", otaPage);

    // Establish a connection with an autoReconnect option.
    if (Portal.begin()) {
        log_i("WiFi connected: %s", WiFi.localIP().toString().c_str());
        if (MDNS.begin(HOSTNAME)) {
            MDNS.addService("http", "tcp", HTTP_PORT);
            log_i("HTTP Server ready! Open http://%s.local/ in your browser\n", HOSTNAME);
        } else
            log_e("Error setting up MDNS responder");
    }
}

void setup(void) {
    initLED();
    led.drawpix(0, CRGB::Red);

    displayOn();

    initAutoConnect();

    displayOff();

    initBME280();

    initClock();
    initButton();
    initPIRSensor();
    initTouchSensor();
    initThingSpeak();
    //initESPUI();

    led.drawpix(0, CRGB::Green);

    setNtpClockNetworkInfo();
    sendThingSpeakData();

    showEnvData();
    displayOn();
    clocker.attach_ms(500, displayClock);
}

void _off(void) {
    log_i("blockflag is false.");
    blockflag = false;
}

void _on(void) {
    log_i("blockflag is true.");
    blockflag = true;
}

void loop(void) {
    Portal.handleClient();
    button.loop();
    pir_sensor.loop();

    //every 60 seconds
    if (sendDataflag) {
        log_d("Clock send BME280 Data.");
        sendThingSpeakData();
        blockOFF.once(15, _off);
        blockON.once(45, _on);

        sendDataflag = false;
    }

    if (blockflag == false && sendMotionflag == true) {
        log_i("Clock can send motion data.");
        sendMotionTime(motionTime);

        motionTime     = 0;
        sendMotionflag = false;
    }

    yield();
}
