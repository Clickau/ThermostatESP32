#include <Arduino.h>
#include <DHTesp.h>

// Pins
// Display
const uint8_t pinDC  = 21;
const uint8_t pinCS  = 5;
const uint8_t pinRST = 22;
// CLK = 18
// DIN = 23

// Temperature sensor
const uint8_t pinDHT = 33;

// To MOSFET which controls the relay
const uint8_t pinHeater = 32;

// Buttons (active high)
const uint8_t pinUp    = 25;
const uint8_t pinDown  = 26;
const uint8_t pinEnter = 27;


// Sensor type
const DHTesp::DHT_MODEL_t dhtType = DHTesp::DHT22;


// NTP settings
const char ntpServer[]             = "pool.ntp.org";
const int8_t timezoneOffset        = 2;               // In Romania, the timezone is UTC+2
const int8_t timezoneOffsetMinutes = 0;               // For timezones that are not whole numbers, for example UTC+2.5 will be written as timezoneOffset = 2, timezoneOffsetMinutes = 30
const bool timezoneDST             = true;            // true if the timezone uses Daylight Saving (Summer time, Winter time)
const int timesTryNTP              = 3;               // How many times we try to download the time from the internet, before we either enter manual mode, if it happens at startup, or we show an NTP error


// Setup Wifi AP settings
const char setupWifiAPSSID[]     = "Thermostat";
const char setupWifiAPPassword[] = "Thermostat123";
const IPAddress setupWifiServerIP{192, 168, 1, 1};


// mDNS hostname used for OTA Update
const char otaHostname[] = "Thermostat";


// Display settings
const uint8_t displayContrast = 50;


// Firebase settings
const char firebasePath[]   = "arduino-test-8c103.firebaseio.com";
const char firebaseSecret[] = "wwy3KljIFEEM5Cv4nEbVGSkeKXG1rcooeKrUPmjO";
const int timesTryFirebase  = 2;                                           // How many times we try to download the schedules from Firebase, before showing error


// Waiting times and time intervals
const unsigned long waitingTimeInStartupMenu           = 5000;    // (ms) The time after which the Normal Operation option is automatically selected if no button is pressed in Startup Menu
const unsigned long waitingTimeConnectWifi             = 10000;   // (ms) The time we wait for it to connect to the Wifi network, in normal operation mode and OTA mode
const unsigned long waitingTimeInTemporaryScheduleMenu = 5000;    // (ms) The time after which we go back to Normal Operation from Temporary Schedule if no button is pressed
const unsigned long intervalRetryErrors                = 300000;  // (ms) The time interval at which we attempt to resolve errors (reconnect to Wifi, to Firebase etc.)
const unsigned long intervalUpdateTemperature          = 10000;   // (ms) The time interval at which we read the temperature and humidity from the sensor
const int           intervalNTPSync                    = 900;     //  (s) The time interval at which we sync with the NTP server


// Temperature settings
const float temporaryScheduleTempResolution = 0.5f;  // The minimum increment in temperature in the Temporary Schedule menu, for example if it is 0.5f, you can set the target temperature to 20 degrees or 20.5, but not 20.2
const float tempThreshold                   = 0.5f;  // The temperature difference needed between the set temperature and the current room temperature to trigger the heater
