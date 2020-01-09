#include <Arduino.h>
#include <DHT_U.h>

#define DEGREESYMB (char)247


const char errorOpenSPIFFS[] = "Critical Error\nOpening SPIFFS";
const char errorOpenConfigWrite[] = "Critical Error\nOpening config\nfor writing";
const char errorOpenConfigRead[] = "Error\nOpening config\ntry Setup";
const char errorWifiConnect[] = "Error\nConnecting to\nWifi";
const char errorNTP[] = "Error\nGetting NTP\nEnter it\nmanually";
const char serverNotAllArgsPresent[] = "Not all the arguments are present";
const char serverReceivedArgs[] = "OK";
const char gotCredentials[] = "Got credetials";
const char updateStarted[] = "Update Started";
const char updateEnded[] = "Update Ended";
const char updateErrorAuth[] = "Auth Error";
const char updateErrorBegin[] = "Begin Error";
const char updateErrorConnect[] = "Connect Error";
const char updateErrorReceive[] = "Receive Error";
const char updateErrorEnd[] = "End Error";
const char updateWaiting[] = "Waiting for\nupdate";
const char updateProgress[] = "Progress: %u%%\n";
const char normalModeMenuEntry[] = "Normal Op.";
const char setupWifiMenuEntry[] = "Setup Wifi";
const char otaUpdateMenuEntry[] = "OTA Update";
const char startupMenuTitle[] = "Startup Menu";
const char ntpServer[] = "pool.ntp.org";
const int8_t timezoneOffset = 2; // in Romania, the timezone is UTC+2
const int8_t timezoneOffsetMinutes = 0; // for timezones that are not whole numbers, for example UTC+2.5 will be written as timezoneOffset = 2, timezoneOffsetMinutes = 30
const bool timezoneDST = true; // true if the timezone uses Daylight Saving (Summer time, Winter time)
const char setupWifiAPSSID[] = "Termostat";
const char setupWifiAPPassword[] = "Termostat123";
const IPAddress setupWifiServerIP(192, 168, 1, 1);
const char otaHostname[] = "Termostat";
const unsigned long waitingTimeInStartupMenu = 5000; // the time after which the normal operation option is automatically selected if no button is pressed, in milliseconds
const uint8_t pinDC = 21; // DC pin for display
const uint8_t pinCS = 5; // CS pin for display
const uint8_t pinRST = 22; // RST pin for display
const uint8_t displayContrast = 40;
const unsigned long waitingTimeConnectWifi = 10000; // the time we wait for it to connect to the wifi network, in normal operation mode and OTA mode, in milliseconds
const int ntpInterval = 900; // the time interval after which we update the time, in seconds
const int timesTryNTP = 3; // how many times we try to download the time from the internet, before we either enter manual mode, if it happens at startup, or we show an NTP error
const char gotTime[] = "Got Time:";
const char manualTimeString[] = "Manual Time";
const char manualTimeFormatString[] = "%.2d"; // format string used in the manual time menu for displaying the hour, minute, date and month; just puts a 0 in front of them if they are less than 2 digits
const char firebasePath[] = "arduino-test-8c103.firebaseio.com"; // the URL of the Firebase
const char auth[] = "wwy3KljIFEEM5Cv4nEbVGSkeKXG1rcooeKrUPmjO"; // Firebase secret key
const char schedulePath[] = "/Program"; // the path to the schedules
const float tempThreshold = 0.5f; // the temperature difference needed between the set temperature and the current room temperature to trigger the heater
const int timesTryFirebase = 2; // how many times we try to download the schedules from FB, before showing error
const unsigned long intervalRetryErrors = 300000; // the time interval after which we try to solve the error, for example reconnecting to wifi, reconnecting to Firebase
const uint8_t dhtPin = 33; // pin for the One Wire communication with DHT module
const uint8_t dhtType = DHT22; // module type
const unsigned long intervalUpdateTemperature = 10000; // the interval after which we update temperature
const int maxNumberOfSensors = 7; // de scapat de senzori externi deocamdata
const char temporaryScheduleString[] = "Program Temp.";
const char temporaryScheduleSensorString[] = "Sensor:%d\n";
const char temporaryScheduleTempString[] = "Temp:%.1fC\n";
const char temporaryScheduleDuration1String[] = "Duration:%dm\n";
const char temporaryScheduleDuration2String[] = "Duration:%.1fh\n";
const char temporaryScheduleDurationInfiniteString[] = "Duration:inf.\n";
const char temporaryScheduleOkString[] = "OK";
const char temporaryScheduleCancelString[] = "CANCEL";
const char temporaryScheduleDeleteString[] = "DEL";
const unsigned long waitingTimeInTemporaryScheduleMenu = 5000; // if, after opening the temporary schedule menu, no button is pressed for this many milliseconds, we go back to normal operation
const float temporaryScheduleTempResolution = 0.5f; // the minimum increment in temperature in the temporary schedule menu, for example if it if 0.5f, you can set the target temperature to 20 degrees or 20.5, but not 20.2
const char displayHumidityNotAvailableString[] = "N\\A"; //displays 'N\A' if the humidity can't be read
const char displayHumidityString[] = "%.2d%%"; //the %d is a placeholder for the relative humidity (integer), and %% just writes %
const char displayDateLine1String[] = "%s. %d\n"; //the %s is a placeholder for the short weekday (e.g. Mon) and %d for the date (e.g. 2)
const char displayDateLine2String[] = "%.2d.%d"; //the %.2d is a placeholder for the month(with a leading 0 if needed, e.g. 08 or 12) and the %d for the year
const char displayTempLetter = 'C'; //C for Celsius, it is the letter displayed next to the degree sign and the temperature, doesn't actually change the scale
const char displayClockFormatString[] = "%.2d:%.2d";// the first %d is a placeholder for the hour, and the second one, for the minute
const char displayErrorWifiString[] = "!W"; // displays this if wifi doesn't work
const char displayErrorNTPString[] = "!N"; // displays this if NTP doesn't work
const char displayErrorFirebaseString[] = "!F"; // displays this if Firebase doesn't work
const char displayErrorTemperatureString[] = "!T"; // displays this if the temperature sensor doesn't work
const char displayErrorHumidityString[] = "!H"; // displays this if the humidity sensor doesn't work
const int heaterPin = 32; // pin that controls the relay (through a mosfet) which controls the heater
// the buttons are pulled down through software, need to be active high
const int pinUp = 25; // up button
const int pinDown = 26; // down button
const int pinEnter = 27; // enter button


//contains the pixel values that make up the flame icon displayed when the heater is on
const byte flame[] = {
B00010000,
B00111000,
B00100100,
B00100010,
B01001011,
B11011101,
B10011101,
B10111111,
B10111111,
B11111111,
B01111110,
B00111100,
};