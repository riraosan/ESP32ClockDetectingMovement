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

#include <BME280Class.h>
#include <esp32-hal-log.h>

BME280Class::BME280Class()
{
    _sensor_ID = 0;
    _bme = new Adafruit_BME280();
}

BME280Class::~BME280Class() {}

bool BME280Class::getTemperature(float &value)
{
    if (_bme->takeForcedMeasurement())
    {
        value = _bme->readTemperature();
        log_d("Temperature = %2.1f*C", value);

        return true;
    }

    return false;
}

bool BME280Class::getPressure(float &value)
{
    if (_bme->takeForcedMeasurement())
    {
        float pascals = _bme->readPressure();
        log_d("Pressure = %4.1f(hPa)", pascals / 100.0f);

        value = pascals / 100.0f;

        return true;
    }

    return false;
}

bool BME280Class::getHumidity(float &value)
{
    if (_bme->takeForcedMeasurement())
    {
        value = _bme->readHumidity();
        log_d("Humidity = %2.1f%%", value);

        return true;
    }

    return false;
}

bool BME280Class::getAltitude(float &seaLevel)
{
    if (_bme->takeForcedMeasurement())
    {
        seaLevel = _bme->readAltitude(SENSORS_PRESSURE_SEALEVELHPA);
        log_d("Altitude = %4.1f m", seaLevel);

        return true;
    }

    return false;
}

uint32_t BME280Class::getSensorID(void)
{
    uint32_t id = _bme->sensorID();
    log_d("Sensor ID = %d", id);

    return id;
}

// See Also. https://github.com/adafruit/Adafruit_BME280_Library/blob/master/examples/advancedsettings/advancedsettings.ino
// weather monitoring
void BME280Class::initBME280WeatherStation(void)
{
    log_i("-- Weather Station Scenario --");
    log_i("forced mode, 1x temperature / 1x humidity / 1x pressure oversampling,");
    log_i("filter off");
    // suggested rate is 1/60Hz (1m)
    _bme->setSampling(Adafruit_BME280::MODE_FORCED,
                      Adafruit_BME280::SAMPLING_X1, // temperature
                      Adafruit_BME280::SAMPLING_X1, // pressure
                      Adafruit_BME280::SAMPLING_X1, // humidity
                      Adafruit_BME280::FILTER_OFF);
}

// humidity sensing
void BME280Class::initBME280HumiditySensing(void)
{
    log_i("-- Humidity Sensing Scenario --");
    log_i("forced mode, 1x temperature / 1x humidity / 0x pressure oversampling");
    log_i("= pressure off, filter off");
    // suggested rate is 1Hz (1s)
    _bme->setSampling(Adafruit_BME280::MODE_FORCED,
                      Adafruit_BME280::SAMPLING_X1,   // temperature
                      Adafruit_BME280::SAMPLING_NONE, // pressure
                      Adafruit_BME280::SAMPLING_X1,   // humidity
                      Adafruit_BME280::FILTER_OFF);
}

// indoor navigation
void BME280Class::initBME280IndoorNavigation(void)
{
    log_i("-- Indoor Navigation Scenario --");
    log_i("normal mode, 16x pressure / 2x temperature / 1x humidity oversampling,");
    log_i("0.5ms standby period, filter 16x");
    // suggested rate is 25Hz
    _bme->setSampling(Adafruit_BME280::MODE_NORMAL,
                      Adafruit_BME280::SAMPLING_X2,  // temperature
                      Adafruit_BME280::SAMPLING_X16, // pressure
                      Adafruit_BME280::SAMPLING_X1,  // humidity
                      Adafruit_BME280::FILTER_X16,
                      Adafruit_BME280::STANDBY_MS_0_5);
}

// gaming
void BME280Class::initBME280Gaming(void)
{
    log_i("-- Gaming Scenario --");
    log_i("normal mode, 4x pressure / 1x temperature / 0x humidity oversampling,");
    log_i("= humidity off, 0.5ms standby period, filter 16x");
    // Suggested rate is 83Hz
    _bme->setSampling(Adafruit_BME280::MODE_NORMAL,
                      Adafruit_BME280::SAMPLING_X1,   // temperature
                      Adafruit_BME280::SAMPLING_X4,   // pressure
                      Adafruit_BME280::SAMPLING_NONE, // humidity
                      Adafruit_BME280::FILTER_X16,
                      Adafruit_BME280::STANDBY_MS_0_5);
}

void BME280Class::setup(int sdaPin, int sclPin, MODE mode)
{
    Wire.setPins(sdaPin, sclPin);

    if (!_bme->begin(BME280_ADDRESS_ALTERNATE))
    {
        log_e("Could not find a valid BME280 sensor, check wiring, address, sensor ID!");
        log_e("SensorID was: 0x%x", _bme->sensorID());
        log_e("        ID of 0xFF probably means a bad address, a BMP 180 or BMP 085");
        log_e("   ID of 0x56-0x58 represents a BMP 280,");
        log_e("        ID of 0x60 represents a BME 280.");
        log_e("        ID of 0x61 represents a BME 680.");
    }
    else
    {
        log_d("ESP could find a BME280 sensor!");
        log_d("SensorID was: 0x%x", _bme->sensorID());

        switch(mode){
        case MODE::WEATHER_STATION:
            initBME280WeatherStation();
        break;
        case MODE::HUMIDITY_SENSING:
            initBME280HumiditySensing();
        break;
        case MODE::INDOOR_NAVIGATION:
            initBME280IndoorNavigation();
        break;
        case MODE::GAMING:
            initBME280Gaming();
        break;
        default:
                ;
        }
    }
}

#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_BME280CLASS)
BME280Class bme280;
#endif
