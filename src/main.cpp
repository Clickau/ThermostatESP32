#include <Arduino.h>
#include <ArduinoJson.h>
#include <string.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <FS.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>
#include <ArduinoOTA.h>
#include <WiFiUdp.h>
#include <ESP8266mDNS.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
#include <TimeLib.h>
#include <NtpClientLib.h>
#include <ESP8266WebServer.h>
#include "DSEG7Classic-Bold6pt.h"

#define DEGREESYMB (char)247


const char ErrorOpenSPIFFS[] PROGMEM = "Critical Error\nOpening SPIFFS";
const char ErrorOpenConfigWrite[] PROGMEM = "Critical Error\nOpening config\nfor writing";
const char ErrorOpenConfigRead[] PROGMEM = "Error\nOpening config\ntry Setup";
const char ErrorWifiConnect[] PROGMEM = "Error\nConnecting to\nWifi";
const char ErrorNTP[] PROGMEM = "Error\nGetting NTP\nEnter it\nmanually";
const char ServerNotAllArgsPresent[] PROGMEM = "Not all the arguments are present";
const char ServerReceivedArgs[] PROGMEM = "OK";
const char GotCredentials[] PROGMEM = "Got credetials";
const char UpdateStarted[] PROGMEM = "Update Started";
const char UpdateEnded[] PROGMEM = "Update Ended";
const char UpdateError[] PROGMEM = "Update Failed";
const char NormalModeMessage[] PROGMEM = "Normal Op.";
const char SetupWifiMessage[] PROGMEM = "Setup Wifi";
const char OTAUpdateMessage[] PROGMEM = "OTA Update";
const char StartupMenuMessage[] PROGMEM = "Startup Menu";
const char NTPServer[] PROGMEM = "pool.ntp.org";
const int8_t timezoneOffset = 2; // in Romania, the timezone is UTC+2
const int8_t timezoneOffsetMinutes = 0; // for timezones that are not whole numbers, for example UTC+2.5 will be written as timezoneOffset = 2, timezoneOffsetMinutes = 30
const bool timezoneDST = true; // true if the timezone uses Daylight Saving (Summer time, Winter time)
const char ap_ssid[] = "TermostatSetupWifi"; // not progmem because softAP takes char* args
const char ap_password[] = "Termostat123"; // idem
const unsigned long waitingTimeInStartupMenu = 5000; // the time after which the normal operation option is automatically selected if no button is pressed, in milliseconds
const uint8_t pinDC = 16; // DC pin for display
const uint8_t pinCS = 5; // CS pin for display
const uint8_t pinRST = 4; // RST pin for display
const uint8_t displayContrast = 40;
const unsigned long waitingTimeConnectWifi = 10000; // the time we wait for it to connect to the wifi network, in normal operation mode and OTA mode, in milliseconds
const int NTPInterval = 900; // the time interval after which we update the time, in seconds
const int8_t timesTryNTP = 3; // how many times we try to download the time from the internet, before we either enter manual mode, if it happens at startup, or we show an NTP error
const char GotTime[] PROGMEM = "Got Time:";
const char manualTimeString[] PROGMEM = "Manual Time";
const char db_path[] PROGMEM = "arduino-test-8c103.firebaseio.com"; // the URL of the Firebase
const char auth[] PROGMEM = "wwy3KljIFEEM5Cv4nEbVGSkeKXG1rcooeKrUPmjO"; // Firebase secret key
const char programPath[] PROGMEM = "/Program"; // the path to the schedules
const char firebaseFingerprint[] PROGMEM = "B6 F5 80 C8 B1 DA 61 C1 07 9D 80 42 D8 A9 1F AF 9F C8 96 7D";
const float tempThreshold = 0.5f; // the temperature difference needed between the set temperature and the current room temperature to trigger the heater
const uint8_t timesTryFirebase = 2; // how many times we try to download the schedules from FB, before showing error
const unsigned long intervalRetryErrors = 60000; // the time interval after which we try to solve the error, for example reconnecting to wifi, reconnecting to Firebase
const uint8_t dhtPin = 12; // pin for the One Wire communication with DHT module
const uint8_t dhtType = DHT22; // module type
const unsigned long intervalUpdateTemperature = 10000; // the interval after which we update temperature
const int maxNumberOfSensors = 7; // de scapat de senzori externi deocamdata
const char programTemporarString[] PROGMEM = "Program Temp.";
const char programTemporarSensorString[] PROGMEM = "Sensor:%d\n";
const char programTemporarTempString[] PROGMEM = "Temp:%.1fC\n";
const char programTemporarDuration1String[] PROGMEM = "Duration:%dm\n";
const char programTemporarDuration2String[] PROGMEM = "Duration:%.1fh\n";
const char programTemporarDurationInfiniteString[] PROGMEM = "Duration:inf.\n";
const char programTemporarOkString[] PROGMEM = "OK";
const char programTemporarCancelString[] PROGMEM = "CANCEL";
const char programTemporarDeleteString[] PROGMEM = "DEL";
const unsigned long waitingTimeInProgramTemporarMenu = 5000; // if, after opening the temporary schedule menu, no button is pressed for this many milliseconds, we go back to normal operation
const float programTemporarTempResolution = 0.5f; // the minimum increment in temperature in the temporary schedule menu, for example if it if 0.5f, you can set the target temperature to 20 degrees or 20.5, but not 20.2
const char displayHumidityNotAvailableString[] PROGMEM = "N\\A"; //displays 'N\A' if the humidity can't be read
const char displayHumidityString[] PROGMEM = "%d%%"; //the %d is a placeholder for the relative humidity (integer), and %% just writes %
const char displayDateLine1String[] PROGMEM = "%s. %d\n"; //the %s is a placeholder for the short weekday (e.g. Mon) and %d for the date (e.g. 2)
const char displayDateLine2String[] PROGMEM = "%.2d.%d"; //the %.2d is a placeholder for the month(with a leading 0 if needed, e.g. 08 or 12) and the %d for the year
const char displayTempLetter = 'C'; //C for Celsius, it is the letter displayed next to the degree sign and the temperature, doesn't actually change the scale
const char displayClockFormatString[] PROGMEM = "%d:%d";// the first %d is a placeholder for the hour, and the second one, for the minute
const char displayErrorWifiString[] PROGMEM = "!W"; // displays this if wifi doesn't work
const char displayErrorNTPString[] PROGMEM = "!N"; // displays this if NTP doesn't work
const char displayErrorFirebaseString[] PROGMEM = "!F"; // displays this if Firebase doesn't work
const char displayErrorTemperatureString[] PROGMEM = "!T"; // displays this if the temperature sensor doesn't work
const char displayErrorHumidityString[] PROGMEM = "!H"; // displays this if the humidity sensor doesn't work

//contains the pixel values that make up the flame icon displayed when the heater is on
const byte flame[] PROGMEM = {
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

enum class Button
{
	None = -1,
	Enter = 0,
	Sus = 1,
	Jos = 2
};

bool heaterState = false;
String programString;
bool wifiWorking = true;
bool firebaseWorking = true;
bool NTPWorking = true;
bool tempWorking = true;
bool humWorking = true;
float temperature = NAN;
float temperatures[maxNumberOfSensors + 1];
int humidity = -1;
unsigned long lastRetryErrors = 0;
unsigned long lastTemperatureUpdate = 0;
bool programTempActiv = false;
int programTempSensor;
float programTempTemperature;
time_t programTempEnd;

// custom HTTPClient that can handle Firebase Streaming
class StreamingHttpClient : private HTTPClient
{
public:
	// checks if anything changed in the database
	// if it receives updates on the stream, it checks if the event is of type 'put', and if so, it means something in the database changed, so it clears the stream and returns true
	// otherwise, it returns false
	bool consumeStreamIfAvailable()
	{
		if (!connected())
		{
			Serial.println("Connection lost");
			closeStream();
			firebaseWorking = false;
			return false;
		}
		WiFiClient *client = getStreamPtr();
		if (client == nullptr)
			return false;
		if (!client->available())
			return false;
		/*
		The received stream event has the format:
		event: ...
		data: ...
		(empty line)
		so the event type is in the first line, starting from the 7th character
		*/
		String eventType = client->readStringUntil('\n').substring(7);
		// we clear the stream
		client->readStringUntil('\n');
		client->readStringUntil('\n');
		// the event might be just a keep-alive, so we check if the type is 'put'
		if (eventType != "put")
			return false;
		return true;
	}
	bool initializeStream()
	{
		if (_initialized)
			closeStream();
		_initialized = true;
		// we keep the connection alive after the first request
		setReuse(true);
		setFollowRedirects(true);
		// we open a secure connection
		begin(String(F("https://")) + FPSTR(db_path) + FPSTR(programPath) + F(".json?auth=") + FPSTR(auth), String(FPSTR(firebaseFingerprint)));
		addHeader("Accept", "text/event-stream");
		int status = GET();

		if (status != HTTP_CODE_OK)
			return false;
		return true;
	}
	void closeStream()
	{
		_initialized = false;
		setReuse(false);
		end();
	}
private:
	bool _initialized = false;
};

Adafruit_PCD8544 display = Adafruit_PCD8544(pinDC, pinCS, pinRST);
StreamingHttpClient firebaseClient;
DHT_Unified dht(dhtPin, dhtType);
// did this so you can easily change the type of sensor used (if it supports the Unified Sensor library), not sure if it's the best way though
DHT_Unified::Temperature tSens = dht.temperature();
DHT_Unified::Humidity hSens = dht.humidity();
Adafruit_Sensor *tempSensor = &tSens;
Adafruit_Sensor *humSensor = &hSens;


Button buttonPressed();
Button virtualButtonPressed();
void normalOperationSetup();
void normalOperationLoop();
void decideHeaterFate();
void updateTemp();
void updateHum();
float getTemp(int sensor);
float virtualGetTemp(int sensor);
bool shouldTurnOnHeater(float temp, float setTemp);
bool isActive(const JsonObject &program);
void manualTimeSetup();
void simpleDisplay(const String &str);
void manualTimeHelper(int h, int m, int d, int mth, int y, int sel);
void virtualManualTimeSetup(int h, int m, int d, int mth, int y);
void setupWifiSetup();
void wifiSetupHandleRoot();
void wifiSetupHandlePost();
void getCredentials(String &ssid, String &password);
void storeCredentials(const String &_ssid, const String &_password);
bool connectSTAMode();
void OTAUpdateSetup();
void setupWifiLoop();
void OTAUpdateLoop();
void showWifiSetupMenu();
void virtualShowWifiSetupMenu();
void showStartupMenu();
void displayStartupMenu(int highlightedOption);
void virtualDisplayStartupMenu(int highlightedOption);
void sendSignalToHeater(bool _on);
void virtualSendSignalToHeater(bool _on);
void UpdateDisplay();
void programTemporarSetup();
void programTemporarHelper(int sensor, float temp, int duration, int option, int sel);
void virtualprogramTemporarHelper(int sensor, float temp, int duration, int option, int sel);
void displayTemp(float temp, int cursorX, int cursorY);
void displayClock(int hour, int min, int cursorX, int cursorY);
void displayDate(int day, int mth, int year, int wDay, int cursorX, int cursorY);
void displayFlame(const uint8_t* flameBitmap, int cursorX, int cursorY, int width, int height);
void displayHumidity(int hum, int cursorX, int cursorY);
void displayErrors(int cursorX, int cursorY);

void setup()
{
	// normal operation setup
	Serial.begin(115200); // in serial monitor, trebuie sa fie activata optiunea Both NL & CR
	display.begin();
	display.setContrast(displayContrast);
	// oprim central la reset, pentru orice eventualitate
	sendSignalToHeater(false);

	//functionare normala setup
	dht.begin();
	Serial.println("Mod selectat Functionare Normala");
	if (!connectSTAMode())
	{
		// eroare la conectarea la wifi
		Serial.println(FPSTR(ErrorWifiConnect));
		simpleDisplay(String(FPSTR(ErrorWifiConnect)));
		wifiWorking = false;
	}
	NTP.begin(String(FPSTR(NTPServer)), timezoneOffset, timezoneDST, timezoneOffsetMinutes);
	NTP.setInterval(NTPInterval, NTPInterval);
	if (wifiWorking)
	{
		Serial.println("trying to get time");
		int i = 0;
		while (NTP.getLastNTPSync() == 0 && i < timesTryNTP) // daca nu reusim sa descarcam prima data timpul, incercam inca de timesTryNTP ori
		{
			Serial.printf("attempt nr %d\n", i);
			delay(500); // asteptam 500ms pana cand incercam din nou
			time_t t = NTP.getTime();
			if (t != 0)
				setTime(t);
			i++;
			// aici exista o problema: daca deconectez cablul de internet din router si il conectez in timpul acestui while (pentru test), uneori esp-ul este resetat de wdt (hardware) - investigat
		}
		if (NTP.getLastNTPSync() == 0) // daca nu am reusit, intram in modul manual
		{
			NTPWorking = false;
			Serial.println(FPSTR(ErrorNTP));
			simpleDisplay(String(FPSTR(ErrorNTP)));
			delay(3000);
			manualTimeSetup();
		}
	}
	else
	{
		Serial.println("bypassed ntp because wifi doesnt work");
		NTPWorking = false;
		delay(3000);
		manualTimeSetup();
	}
	Serial.println("got time:");
	Serial.println(NTP.getTimeDateString());
	display.clearDisplay();
	display.setCursor(0, 0);
	display.println(FPSTR(GotTime));
	display.println(NTP.getTimeStr());
	display.println(NTP.getDateStr());
	display.display();
	if (wifiWorking)
	{
		Serial.println("initialize firebase");
		firebaseWorking = firebaseClient.initializeStream();
	}
	else
	{
		Serial.println("bypassed firebase init because wifi doesnt work");
		firebaseWorking = false;
	}
}

void loop()
{
	// normal operation loop
	if (firebaseWorking)
	{
		if (firebaseClient.consumeStreamIfAvailable())
		{
			// something in the database changed, we download the whole database
			// we try it for timesTryFirebase times, before we give up
			Serial.println("new change!");
			int result;
			int i = 0;
			do
			{
				Serial.printf("attempt nr %d\n", i);
				HTTPClient getHttp;
				getHttp.begin(String(F("https://")) + FPSTR(db_path) + FPSTR(programPath) + F(".json?auth=") + FPSTR(auth), String(FPSTR(firebaseFingerprint)));
				result = getHttp.GET();
				if (result != HTTP_CODE_OK)
				{
					Serial.println("error getHttp");
					Serial.println(result);
					getHttp.end();
				}
				else
				{
					// if everything worked corectly, we store the received string, which contains the json representation of the schedules
					programString = getHttp.getString();
					getHttp.end();
					break;
				}
				i++;
			} while (i < timesTryFirebase);
			if (result != HTTP_CODE_OK)
			{
				Serial.println("failed to get program");
				firebaseWorking = false;
			}
			else
			{
				Serial.println("got new program");
			}
		}
	}
	// we check if wifi works
	wifiWorking = WiFi.isConnected();
	// if we have never succesfully downloaded the time, or if timesTryNTP * NTPInterval time has passed since the last succesfull time sync, we set NTPWorking to false
	NTPWorking = NTP.getLastNTPSync() != 0 && now() - NTP.getLastNTPSync() < timesTryNTP * NTPInterval;
	// once every intervalRetryError milliseconds, we try to solve the errors, i.e. reconnect to wifi, firebase etc
	if (millis() - lastRetryErrors > intervalRetryErrors)
	{
		Serial.println("trying to fix errors, if any");
		lastRetryErrors = millis();
		if (!wifiWorking)
		{
			WiFi.disconnect(true);
			wifiWorking = connectSTAMode();
		}
		if (!firebaseWorking && wifiWorking)
		{
			firebaseWorking = firebaseClient.initializeStream();
		}
		Serial.printf("wifiWorking: %d", wifiWorking);
		Serial.printf("NTPWorking: %d", NTPWorking);
		Serial.printf("humWorking: %d", humWorking);
		Serial.printf("tempWorking: %d", tempWorking);
	}
	// we update temperature, humidity every intervalUpdateTemperature milliseconds, and we reevaluate the schedule
	if (millis() - lastTemperatureUpdate >= intervalUpdateTemperature)
	{
		Serial.println("updating temperature");
		lastTemperatureUpdate = millis();
		updateTemp();
		updateHum();
		decideHeaterFate();
	}
	// check if enter was pressed
	// in the future, this will run alone with UpdateDisplay on a separate core, on the esp32
	Button pressed = buttonPressed();
	if (pressed == Button::Enter)
	{
		Serial.println("enter was pressed");
		programTemporarSetup();
	}
	UpdateDisplay();
}


void programTemporarSetup()
{
	int sensor = 0;
	float temp = 20.0f;
	int duration = 30;
	int option = 0;
	int sel = 0;
	if (programTempActiv)
	{
		sensor = programTempSensor;
		temp = programTempTemperature;
		duration = -1;
	}
	unsigned long previousTime = millis();
	bool autoselect = true;
	programTemporarHelper(sensor, temp, duration, option, sel);
	while (sel < 4)
	{
		Button pressed = buttonPressed();
		if (pressed == Button::None)
		{
			if (autoselect && millis() - previousTime > waitingTimeInProgramTemporarMenu)
				break;
		}
		if (pressed == Button::Enter)
		{
			sel++;
			autoselect = false;
		}
		else if (pressed == Button::Sus)
		{
			autoselect = false;
			switch (sel)
			{
			case 0:
				if (sensor < maxNumberOfSensors)
					sensor++;
				else
					sensor = 0;
				break;
			case 1:
				if (temp < 35.0f)
					temp += programTemporarTempResolution;
				else
					temp = 5.0f;
				break;
			case 2:
				if (duration == -1)
					duration = 15;
				else if (duration <= 24 * 60)
				{
					if (duration < 60)
						duration += 15;
					else
						duration += 30;
				}
				else
					duration = 15;
				break;
			case 3:
				if (option < 2)
					option++;
				else
					option = 0;
				break;
			default:
				break;
			}
		}
		else if (pressed == Button::Jos)
		{
			autoselect = false;
			switch (sel)
			{
			case 0:
				if (sensor > 0)
					sensor--;
				else
					sensor = maxNumberOfSensors;
				break;
			case 1:
				if (temp > 5.0f)
					temp -= programTemporarTempResolution;
				else
					temp = 35.0f;
				break;
			case 2:
				if (duration == -1)
					duration = 15;
				else if (duration > 15)
				{
					if (duration <= 60)
						duration -= 15;
					else
						duration -= 30;
				}
				else
					duration = 24 * 60 + 30;
				break;
			case 3:
				if (option > 0)
					option--;
				else
					option = 2;
			default:
				break;
			}
		}
		programTemporarHelper(sensor, temp, duration, option, sel);
	}
	if (option == 1 || autoselect)
		return;
	if (option == 0)
	{
		programTempActiv = true;
		programTempSensor = sensor;
		programTempTemperature = temp;
		if (duration != -1)
		{
			if (duration == 24*60+30)
				programTempEnd = -1;
			else
				programTempEnd = now() + duration * 60;
		}
	}
	else if (option == 2)
	{
		programTempActiv = false;
	}
}

void programTemporarHelper(int sensor, float temp, int duration, int option, int sel)
{
	display.clearDisplay();
	display.setCursor(4, 0);
	display.println(FPSTR(programTemporarString));
	display.println();

	if (sel == 0)
	{
		display.setTextColor(WHITE, BLACK);
	}
	else
	{
		display.setTextColor(BLACK);
	}
	display.printf_P(programTemporarSensorString, sensor);
	if (sel == 1)
	{
		display.setTextColor(WHITE, BLACK);
	}
	else
	{
		display.setTextColor(BLACK);
	}
	display.printf_P(programTemporarTempString, temp);
	if (sel == 2)
	{
		display.setTextColor(WHITE, BLACK);
	}
	else
	{
		display.setTextColor(BLACK);
	}
	if (duration == -1)
	{
		int ptDuration;
		if (programTempEnd == -1)
			ptDuration = -1;
		else	
			ptDuration = (programTempEnd - now())/60;
		if (ptDuration == -1)
		{
			display.print(FPSTR(programTemporarDurationInfiniteString));
		}
		else if (ptDuration < 60)
		{
			display.printf_P(programTemporarDuration1String, ptDuration);
		}
		else 
		{
			display.printf_P(programTemporarDuration2String, ((float)ptDuration) / 60);
		}
	}
	else if (duration < 60)
	{
		display.printf_P(programTemporarDuration1String, duration);
	}
	else
	{
		if (duration == 24 * 60 + 30)
			display.print(FPSTR(programTemporarDurationInfiniteString));
		else
			display.printf_P(programTemporarDuration2String, ((float)duration) / 60);
	}
	if (sel == 3)
	{
		if (option == 0)
		{
			display.setTextColor(WHITE, BLACK);
		}
		else
		{
			display.setTextColor(BLACK);
		}
		display.print(FPSTR(programTemporarOkString));
		display.setTextColor(BLACK);
		display.write(' ');
		if (option == 1)
		{
			display.setTextColor(WHITE, BLACK);
		}
		else
		{
			display.setTextColor(BLACK);
		}
		display.print(FPSTR(programTemporarCancelString));
		display.setTextColor(BLACK);
		display.write(' ');
		if (option == 2)
		{
			display.setTextColor(WHITE, BLACK);
		}
		else
		{
			display.setTextColor(BLACK);
		}
		display.println(FPSTR(programTemporarDeleteString));
	}
	else
	{
		display.setTextColor(BLACK);
		display.print(FPSTR(programTemporarOkString));
		display.write(' ');
		display.print(FPSTR(programTemporarCancelString));
		display.write(' ');
		display.println(FPSTR(programTemporarDeleteString));
	}
	display.setTextColor(BLACK);
	display.display();
	//virtualprogramTemporarHelper(sensor, temp, duration, option, sel);
}

void virtualprogramTemporarHelper(int sensor, float temp, int duration, int option, int sel)
{
	Serial.println(FPSTR(programTemporarString));
	Serial.println();
	Serial.printf_P(programTemporarSensorString, sensor);
	Serial.printf_P(programTemporarTempString, temp);
	if (duration < 60)
	{
		Serial.printf_P(programTemporarDuration1String, duration);
	}
	else
	{
		if (duration == 24 * 60 + 30)
			Serial.print(FPSTR(programTemporarDurationInfiniteString));
		else
			Serial.printf_P(programTemporarDuration2String, ((float)duration) / 60);
	}
	if (option == 0)
		Serial.println(FPSTR(programTemporarOkString));
	else if (option == 1)
		Serial.println(FPSTR(programTemporarCancelString));
	else if (option == 2)
		Serial.println(FPSTR(programTemporarDeleteString));
}

void UpdateDisplay()
{
  display.clearDisplay();
  time_t t = now();
  displayDate(day(t), month(t), year(t), weekday(t), 3, 32);
  displayClock(hour(t), minute(t), 42, 20);
  displayTemp(temperature, 48, 32);
  displayHumidity(humidity, 3, 18);
  displayFlame(flame, 75, 0, 8, 12); //the last two arguments are the width and the height of the flame icon
  displayErrors(0, 0);
  display.display();
}

void displayErrors(int cursorX, int cursorY)
{
  display.setCursor(cursorX, cursorY);
  if (!wifiWorking)
  {
    // error with wifi, no need to check if firebase and ntp work because they don't
    // also no need to display ntp and firebase errors, just wifi error
    display.print(FPSTR(displayErrorWifiString));
    display.write(' ');
  }
  else
  {
    if (!NTPWorking)
      {
        display.print(FPSTR(displayErrorNTPString));
        display.write(' ');
      }
    
    if (!firebaseWorking)
    {
      display.print(FPSTR(displayErrorFirebaseString));
      display.write(' ');
    }
  }
  if (!tempWorking)
    {
      display.print(FPSTR(displayErrorTemperatureString));
      display.write(' ');
    }

  if (!humWorking)
  {
    display.print(FPSTR(displayErrorHumidityString));
    display.write(' ');
  }
}

//cursorX and cursorY are the location of the top left corner
void displayHumidity(int hum, int cursorX, int cursorY)
{
  display.setCursor(cursorX, cursorY);
  display.setFont();
  display.setTextSize(1);
  if (hum == -1)
  {
    display.print(FPSTR(displayHumidityNotAvailableString));
    return;
  }
  display.printf_P(displayHumidityString, hum);
}

//cursorX and cursorY are the location of the top left corner
//only displays the flame if the heater is on
void displayFlame(const uint8_t* flameBitmap, int cursorX, int cursorY, int width, int height)
{
  if (heaterState)
    display.drawBitmap(cursorX, cursorY, flameBitmap, width, height, BLACK);
}

//cursorX and cursorY are the location of the top left corner
//sunday is wDay one
void displayDate(int day, int mth, int year, int wDay, int cursorX, int cursorY)
{
  display.setCursor(cursorX, cursorY);
  display.setFont();
  display.setTextSize(1);
  display.printf_P(displayDateLine1String, dayShortStr(wDay), day);
  display.setCursor(cursorX, display.getCursorY());
  display.printf_P(displayDateLine2String, mth, year);
}

//cursorX and cursorY are the location of the middle left point
void displayClock(int hour, int min, int cursorX, int cursorY)
{
  display.setCursor(cursorX, cursorY);
  display.setFont(&DSEG7Classic_Bold6pt7b);
  display.setTextSize(1);
  display.printf_P(displayClockFormatString, hour, min);
}

//cursorX and cursorY are the location of the top left corner
// needs at least 2 free pixels above(for the degree symbol and C)
void displayTemp(float temp, int cursorX, int cursorY)
{
  display.setFont();
  if (isnan(temp))
  {
    display.setCursor(cursorX, cursorY);
    display.setTextSize(2);
    display.write('N');
    display.setTextSize(1, 2);
    display.write('/');
    display.setTextSize(2);
    display.write('A');
    return;
  }
  display.setCursor(cursorX, cursorY);
  display.setTextSize(2);
  display.print((int)temp);
  display.setTextSize(1);
  int x = display.getCursorX()-1;
  int y = display.getCursorY();
  display.setCursor(x, y-2);
  display.printf_P(PSTR("%c%c"), DEGREESYMB, displayTempLetter);
  display.setCursor(x, y+7);
  float decimals = temp - floorf(temp);
  decimals *= 10;
  display.printf_P(PSTR(".%d"), (int)(decimals + 0.5f));
}

void decideHeaterFate()
{
	if (programTempActiv)
	{
		Serial.println("program temp activ");
		Serial.printf("sensor: %d\ntemp: %f\nend: %ld\n", programTempSensor, programTempTemperature, programTempEnd);
		if (programTempEnd == -1 || now() < programTempEnd)
		{
			bool signal = shouldTurnOnHeater(getTemp(programTempSensor), programTempTemperature);
			sendSignalToHeater(signal);
			return;
		}
		else
		{
			programTempActiv = false;
		}
	}
	uint16_t lastPriority = 0;
	bool signal = false;
	int lastIndex = 0;
	lastIndex = programString.indexOf(':', lastIndex);
	while (lastIndex != -1)
	{
		lastIndex++;
		int endIndex = programString.indexOf('}', lastIndex);
		String str = programString.substring(lastIndex, endIndex + 1);
		lastIndex = programString.indexOf(':', endIndex);
		DynamicJsonDocument doc(400); // marime suficienta pentru un program mare (adica unul care nu se repeta si cu toate smecheriile)
		deserializeJson(doc, str);	// se va opri din deserializat cand se va termina obiectul (cand da peste '}'), deci putem continua
		serializeJson(doc, Serial);
		JsonObject program = doc.as<JsonObject>();
		if (isActive(program))
		{
			float _temp = getTemp(program["sensor"]);
			if (isnan(_temp)) // daca temperatura nu poate fi citita (eroare de senzor probabil), sarim peste program
				continue;
			if (program["repeat"] == "None")
			{
				// este activ un program care nu se repeta, acesta are prioritate absoluta deci oprim aici while-ul
				signal = shouldTurnOnHeater(_temp, program["setTemp"]);
				Serial.println("\n\nOne-time program active: ");
				serializeJson(program, Serial);
				break;
			}
			else if (program["priority"].as<int>() >= lastPriority)
			{
				signal = shouldTurnOnHeater(_temp, program["setTemp"]);
				lastPriority = program["priority"];
				Serial.printf("lastPriority changed: %d\n", lastPriority);
			}
		}
	}
	Serial.printf("Highest priority: %d\n", lastPriority);
	sendSignalToHeater(signal);
}

float getTemp(int sensor)
{
	// pentru test
	//return virtualGetTemp(sensor);
	if (sensor == 0)
	{
		Serial.printf("temp: %fC", temperature);
		return temperature;
	}
	if (sensor > maxNumberOfSensors || sensor < 1)
	{
		Serial.println("Error sensor value out of bounds");
		return NAN;
	}
	Serial.printf("temp: %fC", temperatures[sensor]);
	return temperatures[sensor];
}

void updateTemp()
{
	sensors_event_t event;
	tempSensor->getEvent(&event);
	float _temperature = event.temperature;
	tempWorking = !isnan(_temperature);
	temperature = _temperature;
	Serial.printf("temperature: %f\n", temperature);
}

void updateHum()
{
	sensors_event_t event;
	humSensor->getEvent(&event);
	float _humidity = event.relative_humidity;
	humWorking = !isnan(_humidity);
	if (isnan(_humidity))
		humidity = -1;
	else
		humidity = (int) _humidity;
	//Serial.printf("humidity: %d\n", humidity);
}

float virtualGetTemp(int sensor = 0)
{
	Serial.print("Enter temperature for sensor ");
	Serial.println(sensor);
	while (!Serial.available())
		delay(100);
	String temperature = Serial.readStringUntil('\r');
	if (Serial.peek() == '\n')
		Serial.read();
	Serial.println(temperature.toFloat());
	return temperature.toFloat();
}

bool shouldTurnOnHeater(float temp, float setTemp)
{
	if (heaterState)
	{
		return temp < setTemp + tempThreshold;
	}
	return temp <= setTemp - tempThreshold;
}

bool isActive(const JsonObject &program)
{
	if (program["repeat"] == "Daily")
	{
		Serial.println("Daily!");
		/*Serial.println(program["sH"].as<int>() * 60 + program["sM"].as<int>());
		Serial.println(hour() * 60 + minute());
		Serial.println(program["eH"].as<int>() * 60 + program["eM"].as<int>());*/
		if (program["sH"].as<int>() * 60 + program["sM"].as<int>() <= hour() * 60 + minute() && hour() * 60 + minute() < program["eH"].as<int>() * 60 + program["eM"].as<int>())
		{
			Serial.println("active!");
			return true;
		}
		Serial.println("not active!");
		return false;
	}
	else if (program["repeat"] == "Weekly")
	{
		Serial.println("Weekly!");
		int wDay = 0;
		if (program["weekDay"].as<String>() == "Sunday")
			wDay = 1;
		else if (program["weekDay"].as<String>() == "Monday")
			wDay = 2;
		else if (program["weekDay"].as<String>() == "Tuesday")
			wDay = 3;
		else if (program["weekDay"].as<String>() == "Wednesday")
			wDay = 4;
		else if (program["weekDay"].as<String>() == "Thursday")
			wDay = 5;
		else if (program["weekDay"].as<String>() == "Friday")
			wDay = 6;
		else if (program["weekDay"].as<String>() == "Saturday")
			wDay = 7;

		if (wDay == weekday())
		{
			if (program["sH"].as<int>() * 60 + program["sM"].as<int>() <= hour() * 60 + minute() && hour() * 60 + minute() < program["eH"].as<int>() * 60 + program["eM"].as<int>())
			{
				Serial.println("active!");
				return true;
			}
		}
		Serial.println("not active!");
		return false;
	}
	else
	{
		Serial.println("None!");
		TimeElements start;
		TimeElements end;
		start.Second = 0;
		start.Minute = program["sM"].as<int>();
		start.Hour = program["sH"].as<int>();
		start.Day = program["sD"].as<int>();
		start.Month = program["sMth"].as<int>();
		start.Year = CalendarYrToTm(program["sY"].as<int>());
		end.Second = 0;
		end.Minute = program["eM"].as<int>();
		end.Hour = program["eH"].as<int>();
		end.Day = program["eD"].as<int>();
		end.Month = program["eMth"].as<int>();
		end.Year = CalendarYrToTm(program["eY"].as<int>());
		time_t startT = makeTime(start);
		time_t endT = makeTime(end);
		if (startT <= now() && now() < endT)
		{
			Serial.println("active!");
			return true;
		}
		Serial.println("not active!");
		return false;
	}
}

void manualTimeSetup()
{
	int manualTime[] = {0, 0, 1, 1, 2019}; // int h=0, m=0, d=1, mth=1, y=2010;
	int maxValue[] = {23, 59, 31, 12, 2100};
	int minValue[] = {0, 0, 1, 1, 1971};
	int sel = 0;
	manualTimeHelper(manualTime[0], manualTime[1], manualTime[2], manualTime[3], manualTime[4], sel);
	while (sel < 5)
	{
		Button lastPressed = buttonPressed();
		switch (lastPressed)
		{
		case Button::Sus:
			if (manualTime[sel] < maxValue[sel])
				manualTime[sel]++;
			else
				manualTime[sel] = minValue[sel];
			break;
		case Button::Jos:
			if (manualTime[sel] > minValue[sel])
				manualTime[sel]--;
			else
				manualTime[sel] = maxValue[sel];
			break;
		case Button::Enter:
			sel++;
			break;
		default:
			break;
		}
		manualTimeHelper(manualTime[0], manualTime[1], manualTime[2], manualTime[3], manualTime[4], sel);
	}

	for (int i = 0; i < 5; i++)
		Serial.println(manualTime[i]);
	setTime(manualTime[0], manualTime[1], 0, manualTime[2], manualTime[3], manualTime[4]);
}

void simpleDisplay(const String &str)
{
	display.clearDisplay();
	display.setTextColor(BLACK);
	display.setCursor(0, 0);
	display.println(str);
	display.display();
}

// functie ajutatoare. parametrii: ora, minutul, ziua, luna, anul si care dintre acestea e selectata
void manualTimeHelper(int h, int m, int d, int mth, int y, int sel)
{
	String sh = "", sm = "", sd = "", smth = "", sy;
	if (h < 10)
		sh += "0";
	sh += String(h);
	if (m < 10)
		sm += "0";
	sm += String(m);
	if (d < 10)
		sd += "0";
	sd += String(d);
	if (mth < 10)
		smth += "0";
	smth += String(mth);
	sy = String(y);

	display.clearDisplay();
	display.setTextSize(1);
	display.setTextColor(BLACK);
	display.setCursor(10, 0);
	display.println(FPSTR(manualTimeString));

	display.setCursor(27, 15);
	if (sel == 0)
	{
		display.setTextColor(WHITE, BLACK);
		display.print(sh);
		display.setTextColor(BLACK);
	}
	else
	{
		display.setTextColor(BLACK);
		display.print(sh);
	}
	display.print(':');
	if (sel == 1)
	{
		display.setTextColor(WHITE, BLACK);
		display.println(sm);
		display.setTextColor(BLACK);
	}
	else
	{
		display.setTextColor(BLACK);
		display.println(sm);
	}

	display.setCursor(14, 30);
	if (sel == 2)
	{
		display.setTextColor(WHITE, BLACK);
		display.print(sd);
		display.setTextColor(BLACK);
	}
	else
	{
		display.setTextColor(BLACK);
		display.print(sd);
	}
	display.print('.');
	if (sel == 3)
	{
		display.setTextColor(WHITE, BLACK);
		display.print(smth);
		display.setTextColor(BLACK);
	}
	else
	{
		display.setTextColor(BLACK);
		display.print(smth);
	}
	display.print('.');
	if (sel == 4)
	{
		display.setTextColor(WHITE, BLACK);
		display.println(sy);
		display.setTextColor(BLACK);
	}
	else
	{
		display.setTextColor(BLACK);
		display.println(sy);
	}

	display.display();
}

void virtualManualTimeSetup(int h, int m, int d, int mth, int y)
{
	// virtual
	Serial.println("Enter hour:");
	while (!(Serial.available() > 0))
	{
		delay(10);
	}
	h = Serial.readStringUntil('\r').toInt();
	if (Serial.peek() == '\n')
		Serial.read();
	Serial.println(h);
	Serial.println("Enter minute:");
	while (!(Serial.available() > 0))
	{
		delay(10);
	}
	m = Serial.readStringUntil('\r').toInt();
	if (Serial.peek() == '\n')
		Serial.read();
	Serial.println(m);
	Serial.println("Enter day:");
	while (!(Serial.available() > 0))
	{
		delay(10);
	}
	d = Serial.readStringUntil('\r').toInt();
	if (Serial.peek() == '\n')
		Serial.read();
	Serial.println(d);
	Serial.println("Enter month:");
	while (!(Serial.available() > 0))
	{
		delay(10);
	}
	mth = Serial.readStringUntil('\r').toInt();
	if (Serial.peek() == '\n')
		Serial.read();
	Serial.println(mth);
	Serial.println("Enter year:");
	while (!(Serial.available() > 0))
	{
		delay(10);
	}
	y = Serial.readStringUntil('\r').toInt();
	if (Serial.peek() == '\n')
		Serial.read();
	Serial.println(y);
}

void getCredentials(String &ssid, String &password)
{
	ssid = "Cosmin2.4";
	password = "Bug1!1o605";
}

bool connectSTAMode()
{
	Serial.println("trying to connect to wifi");
	String ssid;
	String password;
	getCredentials(ssid, password);
	WiFi.mode(WIFI_STA);
	WiFi.setAutoReconnect(true);
	WiFi.begin(ssid, password);
	unsigned long previousTime = millis();
	while (WiFi.status() != WL_CONNECTED && millis() - previousTime < waitingTimeConnectWifi)
	{
		delay(100);
	}
	if (WiFi.status() != WL_CONNECTED)
	{
		Serial.println("failed to connect");
		return false;
	}
	Serial.println("Connected!");
	return true;
}

Button buttonPressed()
{
	// de implementat - daca nu e apasat nimic returneaza -1 (Button.None)
	//                - daca e apasat enter, returneaza 0 (Button.Enter)
	//                - daca e apasat sageata sus, returneaza 1 (Button.Sus)
	//                - daca e apasat sageata jos, returneaza 2 (Button.Jos)
	// temporar pentru test
	return virtualButtonPressed();
}

Button virtualButtonPressed()
{
	// metoda pentru a testa functionalitate butoanelor, din software
	// citeste valoarea butoanelor din serial monitor: -1 daca stringul citit e gol sau nu se primeste niciun string, 0 daca se trimite 0,ENTER sau enter, 1 daca se trimite 1, sus sau SUS, 2 daca se trimite 2, jos sau JOS
	if (!(Serial.available() > 0))
		return Button::None;				   // daca nu se primeste niciun string, presupunem ca nu s-a apasat niciun buton
	String str = Serial.readStringUntil('\r'); // citeste string-ul pana la \r
	if (Serial.peek() == '\n')
		Serial.read(); // citeste si caracterul \n pentru a evita confuzia
	if (str == "0" || str == "ENTER" || str == "enter")
	{
		Serial.println("Enter was pressed");
		return Button::Enter;
	}
	if (str == "1" || str == "SUS" || str == "sus")
	{
		Serial.println("Sus was pressed");
		return Button::Sus;
	}
	if (str == "2" || str == "JOS" || str == "jos")
	{
		Serial.println("Jos was pressed");
		return Button::Jos;
	}
	return Button::None;
}

void sendSignalToHeater(bool _on)
{
	// de implementat
	heaterState = _on;
	virtualSendSignalToHeater(_on);
}

void virtualSendSignalToHeater(bool _on)
{
	Serial.print("Heater: ");
	if (_on)
		Serial.println("on");
	else
		Serial.println("off");
}