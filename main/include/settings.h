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
const char ntpServer0[] = "0.pool.ntp.org";
const char ntpServer1[] = "1.pool.ntp.org";
const char ntpServer2[] = "2.pool.ntp.org";


// Setup Wifi AP settings
const char setupAPSSID[]     = "Thermostat";
const char setupAPPassword[] = "Thermostat123";


// mDNS hostname used for Setup and OTA Update
const char mDNSHostname[] = "thermostat";


// Display settings
const uint8_t displayContrast = 60;


// Firebase settings
const int timesTryFirebase = 2;  // How many times we try to download the schedules from Firebase, before showing error


// Waiting times and time intervals
const unsigned long waitingTimeInStartupMenu           = 5000;           // (ms) The time after which the Normal Operation option is automatically selected if no button is pressed in Startup Menu
const unsigned long waitingTimeConnectWifi             = 10000;          // (ms) The time we wait for it to connect to the Wifi network, in normal operation mode and OTA mode
const unsigned long waitingTimeInTemporaryScheduleMenu = 5000;           // (ms) The time after which we go back to Normal Operation from Temporary Schedule if no button is pressed
const unsigned long waitingTimeNTP                     = 10000;          // (ms) The time we wait for the first NTP sync, after which we enter Manual Time if the sync was not successful
const unsigned long intervalRetryErrors                = 300000;         // (ms) The time interval at which we attempt to resolve errors (reconnect to Wifi, to Firebase etc.)
const unsigned long intervalUpdateTemperature          = 10000;          // (ms) The time interval at which we read the temperature and humidity from the sensor
const unsigned long intervalUploadState                = 60000;          // (ms) The time interval at which we upload the current temperature, humidity and heater state to Firebase
const unsigned long intervalCheckUpdate                = 24*60*60*1000;  // (ms) The time interval at which we check for firmware updates


// Temperature settings
const float temporaryScheduleTempResolution = 0.5f;  // The minimum increment in temperature in the Temporary Schedule menu, for example if it is 0.5f, you can set the target temperature to 20 degrees or 20.5, but not 20.2
const float tempThreshold                   = 0.5f;  // The temperature difference needed between the set temperature and the current room temperature to trigger the heater


// Button settings
const unsigned long buttonDebounceTime = 200;  // (ms) The minimum time between two button presses, used to prevent registering the same button press multiple times due to bouncing


// Update settings
const char latestReleaseURL[] = "https://clickau.github.io/ThermostatESP32/releases/latest.json";