#include <Arduino.h>

static const char AUX_TIMEZONE[] PROGMEM = R"(
{
  "title": "TimeZone",
  "uri": "/timezone",
  "menu": true,
  "element": [
    {
      "name": "caption",
      "type": "ACText",
      "value": "Sets the time zone to get the current local time.",
      "style": "font-family:Arial;font-weight:bold;text-align:center;margin-bottom:10px;color:DarkSlateBlue"
    },
    {
      "name": "timezone",
      "type": "ACSelect",
      "label": "Select TZ name",
      "option": [],
      "selected": 10
    },
    {
      "name": "newline",
      "type": "ACElement",
      "value": "<br>"
    },
    {
      "name": "start",
      "type": "ACSubmit",
      "value": "OK",
      "uri": "/start"
    }
  ]
}
)";

typedef struct {
    const char* zone;
    const char* ntpServer;
    int8_t tzoff;
} Timezone_t;

static const Timezone_t TZ[] = {
    {"Europe/London", "europe.pool.ntp.org", 0},
    {"Europe/Berlin", "europe.pool.ntp.org", 1},
    {"Europe/Helsinki", "europe.pool.ntp.org", 2},
    {"Europe/Moscow", "europe.pool.ntp.org", 3},
    {"Asia/Dubai", "asia.pool.ntp.org", 4},
    {"Asia/Karachi", "asia.pool.ntp.org", 5},
    {"Asia/Dhaka", "asia.pool.ntp.org", 6},
    {"Asia/Jakarta", "asia.pool.ntp.org", 7},
    {"Asia/Manila", "asia.pool.ntp.org", 8},
    {"Asia/Tokyo", "asia.pool.ntp.org", 9},
    {"Australia/Brisbane", "oceania.pool.ntp.org", 10},
    {"Pacific/Noumea", "oceania.pool.ntp.org", 11},
    {"Pacific/Auckland", "oceania.pool.ntp.org", 12},
    {"Atlantic/Azores", "europe.pool.ntp.org", -1},
    {"America/Noronha", "south-america.pool.ntp.org", -2},
    {"America/Araguaina", "south-america.pool.ntp.org", -3},
    {"America/Blanc-Sablon", "north-america.pool.ntp.org", -4},
    {"America/New_York", "north-america.pool.ntp.org", -5},
    {"America/Chicago", "north-america.pool.ntp.org", -6},
    {"America/Denver", "north-america.pool.ntp.org", -7},
    {"America/Los_Angeles", "north-america.pool.ntp.org", -8},
    {"America/Anchorage", "north-america.pool.ntp.org", -9},
    {"Pacific/Honolulu", "north-america.pool.ntp.org", -10},
    {"Pacific/Samoa", "oceania.pool.ntp.org", -11}};
