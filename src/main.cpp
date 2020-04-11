#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
#include <TimeLib.h>
#include <NtpClientLib.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <ArduinoOTA.h>
#include "DSEG7Classic-Bold6pt.h"
#include "firebase_certificate.h"
#include "consts.h"
#include "webpage.h"
#include "FirebaseClient.h"
#include "Logger.h"

enum class Button
{
	None = -1,
	Enter = 0,
	Up = 1,
	Down = 2
};

int operatingMode = -1; // 0 = Normal Operation, 1 = Setup Wifi, 2 = OTAUpdate
bool heaterState = false;
String scheduleString;
bool wifiWorking = true;
bool NTPWorking = true;
bool tempWorking = true;
bool humWorking = true;
float temperature = NAN;
int humidity = -1;
unsigned long lastRetryErrors = 0;
unsigned long lastTemperatureUpdate = 0;
bool temporaryScheduleActive = false;
float temporaryScheduleTemp = NAN;
time_t temporaryScheduleEnd = 0;
bool buttonBeingHeld = false;


Adafruit_PCD8544 display = Adafruit_PCD8544(pinDC, pinCS, pinRST);
FirebaseClient firebaseClient{firebaseRootCA, firebasePath, auth};
WebServer server(80);
DHT_Unified dht(dhtPin, dhtType);
// did this so you can easily change the type of sensor used (if it supports the Unified Sensor library), not sure if it's the best way though
DHT_Unified::Temperature tSens = dht.temperature();
DHT_Unified::Humidity hSens = dht.humidity();
Adafruit_Sensor *tempSensor = &tSens;
Adafruit_Sensor *humSensor = &hSens;

TaskHandle_t setupCore0Handle;
TaskHandle_t loopCore0Handle;


Button buttonPressed();
Button virtualButtonPressed();
void updateTemp();
void updateHum();
void manualTimeSetup();
void simpleDisplay(const String &str);
void manualTimeHelper(int h, int m, int d, int mth, int y, int sel);
void getCredentials(String &ssid, String &password);
bool connectSTAMode();
void showStartupMenu();
void startupMenuHelper(int highlightedOption);
void sendSignalToHeater(bool signal);
void updateDisplay();
void temporaryScheduleSetup();
void temporaryScheduleHelper(int sensor, float temp, int duration, int option, int sel);
void displayTemp(float temp, int cursorX, int cursorY);
void displayClock(int hour, int min, int cursorX, int cursorY);
void displayDate(int day, int mth, int year, int wDay, int cursorX, int cursorY);
void displayFlame(const uint8_t* flameBitmap, int cursorX, int cursorY, int width, int height);
void displayHumidity(int hum, int cursorX, int cursorY);
void displayErrors(int cursorX, int cursorY);
bool evaluateSchedule();
bool scheduleIsActive(const JsonObject& schedule);
bool compareTemperatureWithSetTemperature(float temp, float setTemp);
void setupCore0(void *param);
void loopCore0(void *param);
void setupNormalOperation();
void loopNormalOperation();
void setupSetupWifi();
void loopSetupWifi();
void setupOTAUpdate();
void loopOTAUpdate();
void setupWifiHandleRoot();
void setupWifiHandlePost();
void storeCredentials(const String& ssid, const String& password);
void setupWifiDisplayInfo();

void setup()
{
    LOG_INIT();
    // stopping the heater right at startup
    pinMode(heaterPin, OUTPUT);
    pinMode(pinUp, INPUT_PULLDOWN);
    pinMode(pinDown, INPUT_PULLDOWN);
    pinMode(pinEnter, INPUT_PULLDOWN);
    sendSignalToHeater(false);
    display.clearDisplay();
    display.begin();
    display.setContrast(displayContrast);

    // menu where the user selects which operation mode should be used
    showStartupMenu();
}

void showStartupMenu()
{
    // operation mode:  0 - Normal Operation
    //                  1 - Setup Wifi
    //                  2 - OTA Update
    // first the selected option is Normal Operation
    LOG_T("begin");
	startupMenuHelper(0);
	int currentHighlightedValue = 0;
    LOG_T("currentHighlightedValue=%d", currentHighlightedValue);
	unsigned long previousTime = millis();
	bool autoselect = true;
	Button lastButtonPressed = buttonPressed();
	while (lastButtonPressed != Button::Enter)
	{
		if (lastButtonPressed == Button::Up)
		{
			autoselect = false;
			if (currentHighlightedValue != 0)
				currentHighlightedValue--;
			else
				currentHighlightedValue = 2;
            LOG_T("currentHighlightedValue=%d", currentHighlightedValue);
			startupMenuHelper(currentHighlightedValue);
		}
		else if (lastButtonPressed == Button::Down)
		{
			autoselect = false;
			if (currentHighlightedValue != 2)
				currentHighlightedValue++;
			else
				currentHighlightedValue = 0;
            LOG_T("currentHighlightedValue=%d", currentHighlightedValue);
			startupMenuHelper(currentHighlightedValue);
		}
		if (autoselect && (millis() - previousTime >= waitingTimeInStartupMenu))
		{
            LOG_D("Autoselected normal operation");
			break;
		}
		lastButtonPressed = buttonPressed();
	}
    // we clear the display before entering the chosen setup
    display.clearDisplay();
    display.display();
	if (autoselect)
	{
		operatingMode = 0;
		setupNormalOperation();
		return;
	}
    LOG_D("User selected mode: %d", currentHighlightedValue);
	operatingMode = currentHighlightedValue;
	switch (currentHighlightedValue)
	{
	case 0:
		setupNormalOperation();
		break;
	case 1:
		setupSetupWifi();
		break;
	case 2:
		setupOTAUpdate();
		break;
	}
}

void startupMenuHelper(int highlightedOption)
{
	display.clearDisplay();
	display.setTextSize(1);
	display.setCursor(0, 0);

	display.println(startupMenuTitle);

	if (highlightedOption == 0)
        display.setTextColor(WHITE, BLACK);
    else
        display.setTextColor(BLACK);

	display.println(normalModeMenuEntry);

	if (highlightedOption == 1)
		display.setTextColor(WHITE, BLACK);
    else
        display.setTextColor(BLACK);

	display.println(setupWifiMenuEntry);
    
	if (highlightedOption == 2)
		display.setTextColor(WHITE, BLACK);
    else
        display.setTextColor(BLACK);

	display.println(otaUpdateMenuEntry);

    display.setTextColor(BLACK);

	display.display();
}

void loop()
{
    switch(operatingMode)
    {
        case 0:
            loopNormalOperation();
            break;
        case 1:
            loopSetupWifi();
            break;
        case 2:
            loopOTAUpdate();
            break;
    }
}

// core 1
// just for UI (display and buttons)
void setupNormalOperation()
{
    LOG_T("begin");
    bool doneInit = false;

    LOG_T("Creating task setupCore0");

    xTaskCreatePinnedToCore(
        setupCore0,
        "setupCore0",
        10000, // stack size ??
        &doneInit,
        1, //priority
        &setupCore0Handle,
        0 // core
    );

    LOG_T("Created task setupCore0");

    // we wait until setupCore0 sets doneInit to true, meaning it has initialized wifi and ntp, and we can check for errors and display them
    while (!doneInit)
        delay(10);

    LOG_T("doneInit");

    // if wifi or ntp doesn't work, we enter manual time setup
    if (!wifiWorking)
    {
        LOG_D("Wifi not working, entering Manual Time Setup");
		simpleDisplay(errorWifiConnect);
        delay(3000);
        manualTimeSetup();
    }
    else if (!NTPWorking)
    {
        LOG_D("NTP not working, entering Manual Time Setup");
        Serial.println(errorNTP);
        simpleDisplay(errorNTP);
        delay(3000);
        manualTimeSetup();
    }

    // we are sure we have the current time (either via ntp or manual time)
    LOG_D("Got Time: %s", NTP.getTimeDateString().c_str());

	display.clearDisplay();
	display.setCursor(0, 0);
	display.println(gotTime);
	display.println(NTP.getTimeStr());
	display.println(NTP.getDateStr());
	display.display();

    LOG_T("Creating task loopCore0");

    xTaskCreatePinnedToCore(
        loopCore0,
        "loopCore0",
        10000, // stack size ??
        NULL,
        1, // priority
        &loopCore0Handle,
        0 // core
    );

    LOG_T("Created task loopCore0");
}

void loopNormalOperation()
{
    Button pressed = buttonPressed();
	if (pressed == Button::Enter)
	{
        temporaryScheduleSetup();
	}
	updateDisplay();
}

void setupCore0(void *param)
{
    LOG_T("begin");
    bool *p = static_cast<bool*>(param);
    LOG_T("Starting DHT sensor");
    dht.begin();
    LOG_T("Started DHT sensor");
    if (!connectSTAMode())
	{
        LOG_D("Error connecting to Wifi");
		wifiWorking = false;
	}
    LOG_T("Starting NTP");
    NTP.begin(ntpServer, timezoneOffset, timezoneDST, timezoneOffsetMinutes);
	NTP.setInterval(ntpInterval, ntpInterval);
    LOG_T("Started NTP");
    if (wifiWorking)
	{
		// if wifi works, we try to get NTP time
        LOG_D("Trying to get NTP time");
		int i = 1;
		while (NTP.getLastNTPSync() == 0 && i <= timesTryNTP) // we try timesTryNTP times, before giving up
		{
            LOG_D("Attempt %d/%d", i, timesTryNTP);
			time_t t = NTP.getTime();
			if (t != 0)
				setTime(t);
			i++;
			delay(500);
			// problem here: if i disconnect the internet cable from the router and reconnect it during the while, sometimes the esp is reset by the hardware wdt (??)
		}
		if (NTP.getLastNTPSync() == 0)
		{
            LOG_D("Couldn't get NTP time");
			NTPWorking = false;
		}
        else
        {
            LOG_D("Got NTP time: %ld", now());
        }

        LOG_T("Initializing Firebase stream");
		firebaseClient.initializeStream(schedulePath);
	}
    else
	{
        LOG_D("Bypassed getting NTP time because Wifi isn't working");
		NTPWorking = false;
        LOG_D("Bypassed initializing Firebase stream because Wifi isn't working");
		firebaseClient.setError(true);
	}

    // the mandatory setup is done, core 1 can display the eventual errors and enter manual time setup if needed
    *p = true;
    LOG_T("Set doneInit to true");

    // delete this task
    LOG_T("Deleting this task");
    vTaskDelete(NULL);
}

void loopCore0(void *param)
{
    LOG_T("begin");
    while (1)
    {
        if (!firebaseClient.getError())
        {
            if (firebaseClient.consumeStreamIfAvailable())
            {
                // something in the database changed, we download the whole database
                // we try it for timesTryFirebase times, before we give up
                LOG_D("New change in Firebase stream");
                LOG_D("Trying to get new data");
                for (int i = 1; i <= timesTryFirebase; i++)
                {
                    LOG_D("Attempt %d/%d", i, timesTryFirebase);
                    firebaseClient.getJson(schedulePath, scheduleString);
                    if (!firebaseClient.getError())
                        break;
                }
                if (!firebaseClient.getError())
                {
                    LOG_D("Got new schedules");
                }
                else
                {
                    LOG_D("Failed to get new schedules");
                }
            }
        }
        // we delay multiple times per loop iteration, so that, in case all the code gets executed, it won't block essential tasks for the esp and cause task wdt to reset
        delay(50);
        // we check if wifi works
        wifiWorking = WiFi.isConnected();
        // if we have never succesfully downloaded the time, or if timesTryNTP * ntpInterval time has passed since the last succesfull time sync, we set NTPWorking to false
        NTPWorking = NTP.getLastNTPSync() != 0 && now() - NTP.getLastNTPSync() < timesTryNTP * ntpInterval;
        // once every intervalRetryError milliseconds, we try to solve the errors, i.e. reconnect to wifi, firebase etc
        if (millis() - lastRetryErrors > intervalRetryErrors)
        {
            LOG_D("Trying to fix errors");
            LOG_D( "Wifi working: %d\n"\
                   "Firebase working: %d\n"\
                   "NTP working: %d\n"\
                   "Humidity sensor working: %d\n"\
                   "Temperature sensor working: %d",
                   wifiWorking, !firebaseClient.getError(), NTPWorking, humWorking, tempWorking );

            lastRetryErrors = millis();
            if (!wifiWorking)
            {
                LOG_T("Disconnecting Wifi and trying to reconnect");
                WiFi.disconnect(true);
                wifiWorking = connectSTAMode();
            }
            if (firebaseClient.getError() && wifiWorking)
            {
                LOG_T("Initializing Firebase stream");
                firebaseClient.initializeStream(schedulePath);
            }
            LOG_D( "Wifi working: %d\n"\
                   "Firebase working: %d\n"\
                   "NTP working: %d\n"\
                   "Humidity sensor working: %d\n"\
                   "Temperature sensor working: %d",
                   wifiWorking, !firebaseClient.getError(), NTPWorking, humWorking, tempWorking );
        }
        // we delay multiple times per loop iteration, so that, in case all the code gets executed, it won't block essential tasks for the esp and cause task wdt to reset
        delay(50);
        // we update temperature, humidity every intervalUpdateTemperature milliseconds, and we reevaluate the schedule
        if (millis() - lastTemperatureUpdate >= intervalUpdateTemperature)
        {
            LOG_T("Updating temperature and humidity");
            lastTemperatureUpdate = millis();
            updateTemp();
            updateHum();
            // if a temporary schedule is active, we ignore the normal schedule and follow it
            // otherwise, we evaluate the normal schedule
            if (temporaryScheduleActive)
            {
                if (isnan(temperature)) 
                {
                    // the temperature can't be read (sensor error probably)
                    LOG_W("Temperature is NaN");
                    sendSignalToHeater(false);
                }
                else
                {
                    LOG_T("Temporary schedule is active, ignoring normal schedules");
                    if (now() < temporaryScheduleEnd || temporaryScheduleEnd == -1)
                    {
                        LOG_D("The temporary schedule is valid");
                        bool signal = compareTemperatureWithSetTemperature(temperature, temporaryScheduleTemp);
                        sendSignalToHeater(signal);
                    }
                    else
                    {
                        LOG_D("The temporary schedule has expired");
                        // if the temporary schedule has expired, we deactivate it
                        temporaryScheduleActive = false;
                    }
                }
            }
            else
            {
                LOG_T("Evaluating schedules");
                bool signal = evaluateSchedule();
                sendSignalToHeater(signal);
            }
        }
        // we delay multiple times per loop iteration, so that, in case all the code gets executed, it won't block essential tasks for the esp and cause task wdt to reset
        delay(50);
    }
}

void setupSetupWifi()
{
    LOG_T("begin");
    setupWifiDisplayInfo();

    LOG_T("Starting Wifi AP");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(setupWifiAPSSID, setupWifiAPPassword);
    delay(1000); // delay to let the AP initialize
    WiFi.softAPConfig(setupWifiServerIP, setupWifiServerIP, IPAddress(255, 255, 255, 0));
    LOG_T("Started Wifi AP");

    LOG_T("Starting server");
    server.on("/", setupWifiHandleRoot);
    server.on("/post", HTTPMethod::HTTP_POST, setupWifiHandlePost);
    server.begin();
    LOG_D("Started server");
}

void loopSetupWifi()
{
    server.handleClient();
}

void setupOTAUpdate()
{
    LOG_T("begin");
    if (!connectSTAMode())
    {
        LOG_E("Error connecting to Wifi");
        simpleDisplay(errorWifiConnect);
        // if wifi doesn't work, we do nothing
        LOG_E("Awaiting reset by user");
        while (true) { delay(100); }
    }
    LOG_D("Waiting for update");
    simpleDisplay(updateWaiting);
    ArduinoOTA.setHostname(otaHostname);
    ArduinoOTA
        .onStart([]()
        {
            LOG_D("Update started");
		    simpleDisplay(updateStarted);
        })
        .onEnd([]()
        {
            LOG_D("Update ended");
		    simpleDisplay(updateEnded);
        })
        .onProgress([](unsigned int progress, unsigned int total)
        {
            display.clearDisplay();
            LOG_D("Update progress: %d%%", progress * 100 / total);
            display.printf(updateProgress, progress * 100 / total);
            display.display();
        })
        .onError([](ota_error_t error)
        {
            display.clearDisplay();
            display.setTextColor(BLACK);
            display.setTextSize(1);
            switch (error)
            {
                case OTA_AUTH_ERROR:
                    LOG_E("Update Auth Error");
                    display.println(updateErrorAuth);
                    break;
                case OTA_BEGIN_ERROR:
                    LOG_E("Update Begin Error");
                    display.println(updateErrorBegin);
                    break;
                case OTA_CONNECT_ERROR:
                    LOG_E("Update Connect Error");
                    display.println(updateErrorConnect);
                    break;
                case OTA_RECEIVE_ERROR:
                    LOG_E("Update Receive Error");
                    display.println(updateErrorReceive);
                    break;
                case OTA_END_ERROR:
                    LOG_E("Update End Error");
                    display.println(updateErrorEnd);
                    break;
            }
            display.display();
        });
    LOG_T("Starting OTA Update client");
    ArduinoOTA.begin();
    LOG_D("Started OTA Update client");
}

void loopOTAUpdate()
{
    ArduinoOTA.handle();
}

void setupWifiDisplayInfo()
{
    LOG_T("begin");
    display.clearDisplay();
	display.setTextSize(1);
	display.setTextColor(BLACK);
	display.setCursor(0, 0);
	display.println("Connect to:");
	display.println(setupWifiAPSSID);
	display.println("With password:");
	display.println(setupWifiAPPassword);
	display.display();
}

// function called when a client accesses the root of the server, sends back to the client a webpage with a form
void setupWifiHandleRoot()
{
    LOG_T("begin");
	server.send(HTTP_CODE_OK, "text/html", setupWifiPage);
}

// function called when a client sends a POST request to the server, at location /post, stores the SSID and password from the request in SPIFFS
void setupWifiHandlePost()
{
    LOG_T("begin");
    if (!server.hasArg("ssid") || !server.hasArg("password"))
	{
		server.send(HTTP_CODE_BAD_REQUEST, "text/plain", serverNotAllArgsPresent);
        LOG_D("Not all arguments were present in POST request");
		return;
	}
	server.send(HTTP_CODE_OK, "text/plain", serverReceivedArgs);
	String ssid = server.arg("ssid");
	String password = server.arg("password");
    LOG_D("Received SSID and password");
    
    storeCredentials(ssid, password);
    // we display a success message and restart the ESP
	display.clearDisplay();
	display.setCursor(0, 0);
    display.setTextColor(BLACK);
	display.println(gotCredentials);
	display.println(ssid);
	display.println(password);
	display.display();
    delay(5000);
    LOG_D("Restarting ESP");
    ESP.restart();
}

// stores the provided Wifi credentials in SPIFFS
void storeCredentials(const String &ssid, const String &password)
{
    LOG_T("begin");
    // we begin SPIFFS with formatOnFail=true, so that, if the initial begin fails, it will format the filesystem and try again
    LOG_T("Starting SPIFFS");
    if (!SPIFFS.begin(true))
	{
        LOG_E("Error starting SPIFFS");
		simpleDisplay(errorOpenSPIFFS);
        LOG_E("Waiting for user intervention");
        while (true) { delay(1000); }
	}
    LOG_T("Opening config.txt for writing");
	File wifiConfigFile = SPIFFS.open("/config.txt", "w+");
	if (!wifiConfigFile)
	{
        LOG_E("Error opening config file for writing");
		simpleDisplay(errorOpenConfigWrite);
        LOG_E("Waiting for user intervention");
        while (true) { delay(1000); }
	}
	wifiConfigFile.println(ssid);
	wifiConfigFile.println(password);
	wifiConfigFile.close();
	SPIFFS.end();
    LOG_D("Stored credentials successfully");
}

// gets the Wifi login credentials from the SPIFFS
void getCredentials(String &ssid, String &password)
{
    LOG_T("begin");
    // we begin SPIFFS with formatOnFail=true, so that, if the initial begin fails, it will format the filesystem and try again
    LOG_T("Starting SPIFFS");
	if (!SPIFFS.begin(true))
	{
        LOG_E("Error starting SPIFFS");
		simpleDisplay(errorOpenSPIFFS);
        ssid = "";
        password = "";
        LOG_D("Set SSID and Password to empty strings");
        delay(3000);
        return;
	}
    LOG_T("Opening config.txt for reading");
	File wifiConfigFile = SPIFFS.open("/config.txt", "r");
	if (!wifiConfigFile)
	{
        LOG_W("Error opening config file for reading, maybe Setup Wifi hasn't run at all");
		simpleDisplay(errorOpenConfigRead);
        ssid = "";
        password = "";
        delay(3000);
        return;
	}
	ssid = wifiConfigFile.readStringUntil('\r');
	wifiConfigFile.read(); // line ending is CR&LF so we read the \n character
	password = wifiConfigFile.readStringUntil('\r');
	wifiConfigFile.read(); // line ending is CR&LF so we read the \n character
	wifiConfigFile.close();
	SPIFFS.end();
    LOG_D("Got SSID and password");
}

// function which looks at each schedule in the scheduleString, decides which is active and has the top priority, and returns the signal to send to the heater
bool evaluateSchedule()
{
    LOG_T("begin");
    LOG_D("Current time: %s", NTP.getTimeDateString().c_str());
	if (isnan(temperature))
	{
		// the temperature can't be read (sensor error probably)
        LOG_W("Temperature is NaN");
		return false;
	}
    // we find the active schedule from each category (daily, weekly, nonrepeating) if it exists
    // in the end we give priority to the nonrepeating one, then to the weekly, then daily
	bool onceScheduleActive = false, weeklyScheduleActive = false, dailyScheduleActive = false;
    bool onceScheduleSignal = false, weeklyScheduleSignal = false, dailyScheduleSignal = false;
	int beginIndex = 0;
	beginIndex = scheduleString.indexOf('{', 1); // we find the first occurence of the character '{', excluding the first character; this is the beggining of the first schedule object
	while (beginIndex != -1)
	{
		int endIndex = scheduleString.indexOf('}', beginIndex + 1); // we find the next occurence of '}', starting at beginIndex; this is the end of the first schedule object
		String str = scheduleString.substring(beginIndex, endIndex + 1); // we get the schedule object as a string
        LOG_D(str.c_str());
		DynamicJsonDocument doc(400); // we allocate the document enough memory to store even the biggest kind of schedule object
		deserializeJson(doc, str);
		JsonObject schedule = doc.as<JsonObject>();
		if (scheduleIsActive(schedule))
		{
			float setTemp = schedule["setTemp"];
            const char *repeat = schedule["repeat"];
            bool signal = compareTemperatureWithSetTemperature(temperature, setTemp);

			// if a one-time schedule is active, then it has the highest priority, so we stop the loop after it
			if (strcmp(repeat, "Once") == 0)
			{
                onceScheduleActive = true;
				onceScheduleSignal = signal;
				break;
			}
            
            if (strcmp(repeat, "Weekly") == 0)
            {
                weeklyScheduleActive = true;
                weeklyScheduleSignal = signal;
            } 
            else if (strcmp(repeat, "Daily") == 0)
            {
                dailyScheduleActive = true;
                dailyScheduleSignal = signal;
            }
		}
		beginIndex = scheduleString.indexOf('{', endIndex + 1); // we find the next occurence of '{', starting at endIndex; this is the beggining of the next schedule object; we repeat until there are no more objects
	}

    // if there was a nonrepeating schedule active, we pass its signal on
	if (onceScheduleActive)
    {
        LOG_D("Following a one time schedule");
        return onceScheduleSignal;
    }

    // otherwise, if there was a weekly schedule active, we pass its signal on
    if (weeklyScheduleActive)
    {
        LOG_D("Following a weekly schedule");
        return weeklyScheduleSignal;
    }

    // otherwise, if there was a daily schedule active, we pass its signal on
    if (dailyScheduleActive)
    {
        LOG_D("Following a daily schedule");
        return dailyScheduleSignal;
    }

    // if there was no schedule active, we don't turn on the heater
    // TODO: maybe add antifreeze feature
    LOG_D("No schedule is active");
    return false;
}

// function that checks if the supplied schedule object is active at the moment
bool scheduleIsActive(const JsonObject& schedule)
{
    LOG_T("begin");
	const char* repeat = schedule["repeat"];
	// if it is a daily schedule, we check if its start time is before the current time and if its end time is after the current time
	if (strcmp(repeat, "Daily") == 0)
	{
        LOG_D("Daily");
		int startTime = schedule["sH"].as<int>() * 60 + schedule["sM"].as<int>();
		int endTime = schedule["eH"].as<int>() * 60 + schedule["eM"].as<int>();
		time_t t = now();
		int time = hour(t) * 60 + minute(t);
        bool active = startTime <= time && time < endTime;
        LOG_D("%s", active ? "Active" : "Not active");
		return active;
	}
	// weekly schedule, we check if we are on the same weekday and between the start and end time
	if (strcmp(repeat, "Weekly") == 0)
	{
        LOG_D("Weekly");
        JsonArray wDays = schedule["weekDays"]; // Sunday is day 1
		int startTime = schedule["sH"].as<int>() * 60 + schedule["sM"].as<int>();
		int endTime = schedule["eH"].as<int>() * 60 + schedule["eM"].as<int>();
		time_t t = now();
		int currWeekDay = weekday(t); // Sunday is day 1
		int time = hour(t) * 60 + minute(t);
        for (int wDay : wDays)
            if (wDay == currWeekDay)
            {
                LOG_T("Active on this weekday");
                bool active = startTime <= time && time < endTime;
                LOG_D("%s", active ? "Active" : "Not active");
                return active;
            }
        LOG_T("Not active on this weekday");
        LOG_D("Not active");
        return false;
	}
	// the schedule doesn't repeat, we check if we are between the start and end time
	if (strcmp(repeat, "Once") == 0)
	{
        LOG_D("Once");
		tmElements_t startTime;
		tmElements_t endTime;
		startTime.Second = 0;
		startTime.Minute = schedule["sM"];
		startTime.Hour = schedule["sH"];
		startTime.Day = schedule["sD"];
		startTime.Month = schedule["sMth"];
		startTime.Year = CalendarYrToTm(schedule["sY"].as<int>());
		endTime.Second = 0;
		endTime.Minute = schedule["eM"];
		endTime.Hour = schedule["eH"];
		endTime.Day = schedule["eD"];
		endTime.Month = schedule["eMth"];
		endTime.Year = CalendarYrToTm(schedule["eY"].as<int>());
		time_t startT = makeTime(startTime);
		time_t endT = makeTime(endTime);
		time_t t = now();
        bool active = startT <= t && t < endT;
        LOG_D("%s", active ? "Active" : "Not active");
		return active;
	}
    LOG_W("Schedule does not have a correct repeat");
	return false;
}

bool compareTemperatureWithSetTemperature(float temp, float setTemp)
{
	if (heaterState)
	{
		return temp < setTemp + tempThreshold;
	}
	return temp <= setTemp - tempThreshold;
}

void temporaryScheduleSetup()
{
    LOG_T("begin");
    LOG_D("Entering Temporary Schedule Setup");
	// the sensor setting will be used later, to decide which temperature sensor dictates the set temperature, until i implement that, it does nothing
	int sensor = 0;
	float temp = 20.0f;
	int duration = 30;
	int option = 0;
	int sel = 0;
	if (temporaryScheduleActive)
	{
        LOG_T("Modifying current temporary schedule");
		temp = temporaryScheduleTemp;
		duration = -1;
	}
	unsigned long previousTime = millis();
	bool autoselect = true;
    LOG_T( "sensor=%d\n"\
           "temp=%f\n"\
           "duration=%d\n"\
           "option=%d\n"\
           "sel=%d",
           sensor, temp, duration, option, sel );
	temporaryScheduleHelper(sensor, temp, duration, option, sel);
	while (sel < 4)
	{
		Button pressed = buttonPressed();
		if (pressed == Button::None)
		{
			if (autoselect && millis() - previousTime > waitingTimeInTemporaryScheduleMenu)
            {
                LOG_D("Exiting menu because nothing was pressed");
				break;
            }
		}
		if (pressed == Button::Enter)
		{
			sel++;
			autoselect = false;
		}
		else if (pressed == Button::Up)
		{
			autoselect = false;
			switch (sel)
			{
			case 0:
				if (sensor < maxNumberOfSensors)
					sensor++;
				else
					sensor = 0;
                LOG_T("sensor=%d", sensor);
				break;
			case 1:
				if (temp < 35.0f)
					temp += temporaryScheduleTempResolution;
				else
					temp = 5.0f;
                LOG_T("temp=%f", temp);
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
                LOG_T("duration=%d", duration);
				break;
			case 3:
				if (option < 2)
					option++;
				else
					option = 0;
                LOG_T("option=%d", option);
				break;
			}
		}
		else if (pressed == Button::Down)
		{
			autoselect = false;
			switch (sel)
			{
			case 0:
				if (sensor > 0)
					sensor--;
				else
					sensor = maxNumberOfSensors;
                LOG_T("sensor=%d", sensor);
				break;
			case 1:
				if (temp > 5.0f)
					temp -= temporaryScheduleTempResolution;
				else
					temp = 35.0f;
                LOG_T("temp=%f", temp);
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
                LOG_T("duration=%d", duration);
				break;
			case 3:
				if (option > 0)
					option--;
				else
					option = 2;
                LOG_T("option=%d", option);
			default:
				break;
			}
		}
		temporaryScheduleHelper(sensor, temp, duration, option, sel);
	}
	if (option == 1 || autoselect)
    {
        LOG_D("Return without changing anything");
		return;
    }
	if (option == 0)
	{
		temporaryScheduleActive = true;
		//programTempSensor = sensor;
		temporaryScheduleTemp = temp;
		if (duration != -1)
		{
			if (duration == 24*60+30)
				temporaryScheduleEnd = -1;
			else
				temporaryScheduleEnd = now() + duration * 60;
		}
        LOG_D("Saved temporary schedule");
	}
	else if (option == 2)
	{
		temporaryScheduleActive = false;
        LOG_D("Deleted temporary schedule");
	}
}

// helper function that displays the selected values on the screen
void temporaryScheduleHelper(int sensor, float temp, int duration, int option, int sel)
{
	display.clearDisplay();
	display.setCursor(4, 0);
	display.println(temporaryScheduleString);
	display.println();

	if (sel == 0)
	{
		display.setTextColor(WHITE, BLACK);
	}
	else
	{
		display.setTextColor(BLACK);
	}
	display.printf(temporaryScheduleSensorString, sensor);
	if (sel == 1)
	{
		display.setTextColor(WHITE, BLACK);
	}
	else
	{
		display.setTextColor(BLACK);
	}
	display.printf(temporaryScheduleTempString, temp);
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
		if (temporaryScheduleEnd == -1)
			ptDuration = -1;
		else	
			ptDuration = (temporaryScheduleEnd - now())/60;
		if (ptDuration == -1)
		{
			display.print(temporaryScheduleDurationInfiniteString);
		}
		else if (ptDuration < 60)
		{
			display.printf(temporaryScheduleDuration1String, ptDuration);
		}
		else 
		{
			display.printf(temporaryScheduleDuration2String, ((float)ptDuration) / 60);
		}
	}
	else if (duration < 60)
	{
		display.printf(temporaryScheduleDuration1String, duration);
	}
	else
	{
		if (duration == 24 * 60 + 30)
			display.print(temporaryScheduleDurationInfiniteString);
		else
			display.printf(temporaryScheduleDuration2String, ((float)duration) / 60);
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
		display.print(temporaryScheduleOkString);
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
		display.print(temporaryScheduleCancelString);
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
		display.println(temporaryScheduleDeleteString);
	}
	else
	{
		display.setTextColor(BLACK);
		display.print(temporaryScheduleOkString);
		display.write(' ');
		display.print(temporaryScheduleCancelString);
		display.write(' ');
		display.println(temporaryScheduleDeleteString);
	}
	display.setTextColor(BLACK);
	display.display();
}

void updateDisplay()
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

// cursorX and cursorY are the location of the top left corner
void displayErrors(int cursorX, int cursorY)
{
  display.setCursor(cursorX, cursorY);
  if (!wifiWorking)
  {
    // error with wifi, no need to check if firebase and ntp work because they don't
    // also no need to display ntp and firebase errors, just wifi error
    display.print(displayErrorWifiString);
    display.write(' ');
  }
  else
  {
    if (!NTPWorking)
      {
        display.print(displayErrorNTPString);
        display.write(' ');
      }
    
    if (firebaseClient.getError())
    {
      display.print(displayErrorFirebaseString);
      display.write(' ');
    }
  }
  if (!tempWorking)
    {
      display.print(displayErrorTemperatureString);
      display.write(' ');
    }

  if (!humWorking)
  {
    display.print(displayErrorHumidityString);
    display.write(' ');
  }
}

// cursorX and cursorY are the location of the top left corner
void displayHumidity(int hum, int cursorX, int cursorY)
{
  display.setCursor(cursorX, cursorY);
  display.setFont();
  display.setTextSize(1);
  if (hum == -1)
  {
    display.print(displayHumidityNotAvailableString);
    return;
  }
  display.printf(displayHumidityString, hum);
}

// cursorX and cursorY are the location of the top left corner
// only displays the flame if the heater is on
void displayFlame(const uint8_t* flameBitmap, int cursorX, int cursorY, int width, int height)
{
  if (heaterState)
    display.drawBitmap(cursorX, cursorY, flameBitmap, width, height, BLACK);
}

// cursorX and cursorY are the location of the top left corner
// sunday is wDay one
void displayDate(int day, int mth, int year, int wDay, int cursorX, int cursorY)
{
  display.setCursor(cursorX, cursorY);
  display.setFont();
  display.setTextSize(1);
  display.printf(displayDateLine1String, dayShortStr(wDay), day);
  display.setCursor(cursorX, display.getCursorY());
  display.printf(displayDateLine2String, mth, year);
}

// cursorX and cursorY are the location of the middle left point
// it uses the DSEG7 Classic Bold font
void displayClock(int hour, int min, int cursorX, int cursorY)
{
  display.setCursor(cursorX, cursorY);
  display.setFont(&DSEG7Classic_Bold6pt7b);
  display.setTextSize(1);
  display.printf(displayClockFormatString, hour, min);
}

// cursorX and cursorY are the location of the top left corner
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
  display.printf("%c%c", DEGREESYMB, displayTempLetter);
  display.setCursor(x, y+7);
  float decimals = temp - floorf(temp);
  decimals *= 10;
  display.printf(".%d", (int)(decimals + 0.5f));
}

void updateTemp()
{
    LOG_T("begin");
	sensors_event_t event;
	tempSensor->getEvent(&event);
	float _temperature = event.temperature;
	tempWorking = !isnan(_temperature);
	temperature = _temperature;
    LOG_D("Temperature: %f", temperature);
}

void updateHum()
{
    LOG_T("begin");
	sensors_event_t event;
	humSensor->getEvent(&event);
	float _humidity = event.relative_humidity;
	humWorking = !isnan(_humidity);
	if (isnan(_humidity))
		humidity = -1;
	else
		humidity = (int) _humidity;
	LOG_D("Humidity: %d", humidity);

}

// prompts the user to enter the current date and time
void manualTimeSetup()
{
    LOG_T("begin");
	int manualTime[] = {0, 0, 1, 1, 2020}; // int h=0, m=0, d=1, mth=1, y=2020;
	int maxValue[] = {23, 59, 31, 12, 2100};
	int minValue[] = {0, 0, 1, 1, 2020};
	int sel = 0;
	manualTimeHelper(manualTime[0], manualTime[1], manualTime[2], manualTime[3], manualTime[4], sel);
    LOG_T( "hour=%d\n"\
           "minute=%d\n"\
           "day=%d\n"\
           "month=%d\n"\
           "year=%d\n"\
           "Selected=%d",
           manualTime[0], manualTime[1], manualTime[2], manualTime[3], manualTime[4], sel );
	while (sel < 5)
	{
		Button lastPressed = buttonPressed();
		switch (lastPressed)
		{
		case Button::Up:
			if (manualTime[sel] < maxValue[sel])
				manualTime[sel]++;
			else
				manualTime[sel] = minValue[sel];
            LOG_T("Value=%d", manualTime[sel]);
			break;
		case Button::Down:
			if (manualTime[sel] > minValue[sel])
				manualTime[sel]--;
			else
				manualTime[sel] = maxValue[sel];
            LOG_T("Value=%d", manualTime[sel]);
			break;
		case Button::Enter:
			sel++;
            LOG_T("Selected=%d", sel);
			break;
		default:
			break;
		}
		manualTimeHelper(manualTime[0], manualTime[1], manualTime[2], manualTime[3], manualTime[4], sel);
	}

    LOG_T( "hour=%d\n"\
        "minute=%d\n"\
        "day=%d\n"\
        "month=%d\n"\
        "year=%d\n",
        manualTime[0], manualTime[1], manualTime[2], manualTime[3], manualTime[4] );
	setTime(manualTime[0], manualTime[1], 0, manualTime[2], manualTime[3], manualTime[4]);
}

// helper function that clears the display and prints the String parameter in the top left corner
void simpleDisplay(const String &str)
{
	display.clearDisplay();
	display.setTextColor(BLACK);
	display.setCursor(0, 0);
	display.println(str);
	display.display();
}

// helper function that displays the current selected date and time in manual time mode
void manualTimeHelper(int h, int m, int d, int mth, int y, int sel)
{
	display.clearDisplay();
	display.setTextSize(1);
	display.setTextColor(BLACK);
	display.setCursor(10, 0);
	display.println(manualTimeString);

	display.setCursor(27, 15);
	if (sel == 0)
	{
		display.setTextColor(WHITE, BLACK);
		display.printf(manualTimeFormatString, h);
		display.setTextColor(BLACK);
	}
	else
	{
		display.setTextColor(BLACK);
		display.printf(manualTimeFormatString, h);
	}
	display.print(':');
	if (sel == 1)
	{
		display.setTextColor(WHITE, BLACK);
		display.printf(manualTimeFormatString, m);
		display.setTextColor(BLACK);
	}
	else
	{
		display.setTextColor(BLACK);
		display.printf(manualTimeFormatString, m);
	}

	display.setCursor(14, 30);
	if (sel == 2)
	{
		display.setTextColor(WHITE, BLACK);
		display.printf(manualTimeFormatString, d);
		display.setTextColor(BLACK);
	}
	else
	{
		display.setTextColor(BLACK);
		display.printf(manualTimeFormatString, d);
	}
	display.print('.');
	if (sel == 3)
	{
		display.setTextColor(WHITE, BLACK);
		display.printf(manualTimeFormatString, mth);
		display.setTextColor(BLACK);
	}
	else
	{
		display.setTextColor(BLACK);
		display.printf(manualTimeFormatString, mth);
	}
	display.print('.');
	if (sel == 4)
	{
		display.setTextColor(WHITE, BLACK);
		display.println(y);
		display.setTextColor(BLACK);
	}
	else
	{
		display.setTextColor(BLACK);
		display.println(y);
	}

	display.display();
}



// connects to wifi
// if it can't connect in waitingTimeConnectWifi milliseconds, it aborts
bool connectSTAMode()
{
    LOG_T("begin");
    LOG_D("Trying to connect to Wifi");
	String ssid;
	String password;
	getCredentials(ssid, password);
	WiFi.mode(WIFI_STA);
	WiFi.setAutoReconnect(true);
	WiFi.begin(ssid.c_str(), password.c_str());
	unsigned long previousTime = millis();
	while (WiFi.status() != WL_CONNECTED && millis() - previousTime < waitingTimeConnectWifi)
	{
		delay(100);
	}
	if (WiFi.status() != WL_CONNECTED)
	{
        LOG_D("Failed to connect");
		return false;
	}
    LOG_D("Connected");
	return true;
}

// if no button is pressed, returns Button::None
// if a button is pressed, it returns Button::Enter, Button::Up or Button::Down respectively only once, and returns Button::None while the button is being held down
Button buttonPressed()
{
	// temporary for testing
	//return virtualButtonPressed();
    if (digitalRead(pinEnter))
    {
        if (!buttonBeingHeld)
        {
            LOG_T("Enter was pressed");
            buttonBeingHeld = true;
            // delay for debouncing
            delay(200);
            return Button::Enter;
        }
        else
            return Button::None;
    }
    if (digitalRead(pinUp))
    {
        if (!buttonBeingHeld)
        {
            LOG_T("Up was pressed");
            buttonBeingHeld = true;
            // delay for debouncing
            delay(200);
            return Button::Up;
        }
        else
            return Button::None;
        
    }
    if (digitalRead(pinDown))
    {
        if (!buttonBeingHeld)
        {
            LOG_T("Down was pressed");
            buttonBeingHeld = true;
            // delay for debouncing
            delay(200);
            return Button::Down;
        }
        else
            return Button::None;
        
    }
    buttonBeingHeld = false;
    return Button::None;
}

// gets the button from the Serial
Button virtualButtonPressed()
{
	if (!(Serial.available() > 0))
		return Button::None;// if no string is received, we return Button::None
	String str = Serial.readStringUntil('\r');
	if (Serial.peek() == '\n')
		Serial.read();// also reads the \n character at the end, if it exists
	if (str == "0" || str == "ENTER" || str == "enter")
	{
        LOG_T("Enter was pressed");
		return Button::Enter;
	}
	if (str == "1" || str == "SUS" || str == "sus")
	{
        LOG_T("Up was pressed");
		return Button::Up;
	}
	if (str == "2" || str == "JOS" || str == "jos")
	{
        LOG_T("Down was pressed");
		return Button::Down;
	}
	return Button::None;
}

void sendSignalToHeater(bool signal)
{
    LOG_D("Sending signal to heater: %s", signal ? "on" : "off");
	heaterState = signal;
    digitalWrite(heaterPin, signal);
}