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
bool firebaseWorking = true;
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
        // the esp32 version of httpclient doesn't manage redirects automatically
		//setFollowRedirects(true);
		// we open a secure connection
        begin(_client_secure, String(F("https://")) + FPSTR(firebasePath) + FPSTR(schedulePath) + F(".json?auth=") + FPSTR(auth));
		addHeader("Accept", "text/event-stream");

        //manage redirects manually
        const char* headers[] = {"Location"};
        collectHeaders(headers, 1);

		int status = GET();

        //manage redirects manually
        while (status == HTTP_CODE_TEMPORARY_REDIRECT)
        {
            String location = header("Location");
            setReuse(false);
            end();
            setReuse(true);
            begin(location);
            status = GET();
        }

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
    StreamingHttpClient()
    {
        _client_secure.setCACert(firebaseRootCA);
    }
private:
	bool _initialized = false;
    WiFiClientSecure _client_secure;
};

Adafruit_PCD8544 display = Adafruit_PCD8544(pinDC, pinCS, pinRST);
StreamingHttpClient firebaseClient;
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
void virtualSendSignalToHeater(bool signal);
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
    // stopping the heater right at startup
    pinMode(heaterPin, OUTPUT);
    pinMode(pinUp, INPUT_PULLDOWN);
    pinMode(pinDown, INPUT_PULLDOWN);
    pinMode(pinEnter, INPUT_PULLDOWN);
    sendSignalToHeater(false);
    Serial.begin(115200);
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
	startupMenuHelper(0);
	int currentHighlightedValue = 0;
	unsigned long previousTime = millis();
	bool autoselect = true;
	Button lastButtonPressed = buttonPressed();
	while (lastButtonPressed != Button::Enter)
	{
		if (lastButtonPressed == Button::Up)
		{
			Serial.println("Sus a fost apasat in metoda showStartupMenu");
			autoselect = false;
			if (currentHighlightedValue != 0)
				currentHighlightedValue--;
			else
				currentHighlightedValue = 2;
			startupMenuHelper(currentHighlightedValue);
		}
		else if (lastButtonPressed == Button::Down)
		{
			Serial.println("Jos a fost apasat in metoda showStartupMenu");
			autoselect = false;
			if (currentHighlightedValue != 2)
				currentHighlightedValue++;
			else
				currentHighlightedValue = 0;
			startupMenuHelper(currentHighlightedValue);
		}
		if (autoselect && (millis() - previousTime >= waitingTimeInStartupMenu))
		{
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
	Serial.println("Enter a fost apasat in metoda showStartupMenu");
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

	display.println(FPSTR(startupMenuTitle));

	if (highlightedOption == 0)
        display.setTextColor(WHITE, BLACK);
    else
        display.setTextColor(BLACK);

	display.println(FPSTR(normalModeMenuEntry));

	if (highlightedOption == 1)
		display.setTextColor(WHITE, BLACK);
    else
        display.setTextColor(BLACK);

	display.println(FPSTR(setupWifiMenuEntry));
    
	if (highlightedOption == 2)
		display.setTextColor(WHITE, BLACK);
    else
        display.setTextColor(BLACK);

	display.println(FPSTR(otaUpdateMenuEntry));

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
    bool doneInit = false;

    xTaskCreatePinnedToCore(
        setupCore0,
        "setupCore0",
        10000, // stack size ??
        &doneInit,
        1, //priority
        &setupCore0Handle,
        0 // core
    );

    // we wait until setupCore0 sets doneInit to true, meaning it has initialized wifi and ntp, and we can check for errors and display them
    while (!doneInit)
        delay(10);

    // if wifi or ntp doesn't work, we enter manual time setup
    if (!wifiWorking)
    {
        Serial.println(FPSTR(errorWifiConnect));
		simpleDisplay(String(FPSTR(errorWifiConnect)));
        delay(3000);
        manualTimeSetup();
    }
    else if (!NTPWorking)
    {
        Serial.println(FPSTR(errorNTP));
        simpleDisplay(String(FPSTR(errorNTP)));
        delay(3000);
        manualTimeSetup();
    }

    // we are sure we have the current time (either via ntp or manual time)
    Serial.printf("core %u: got time:\n", xPortGetCoreID());
	Serial.println(NTP.getTimeDateString());

	display.clearDisplay();
	display.setCursor(0, 0);
	display.println(FPSTR(gotTime));
	display.println(NTP.getTimeStr());
	display.println(NTP.getDateStr());
	display.display();

    xTaskCreatePinnedToCore(
        loopCore0,
        "loopCore0",
        10000, // stack size ??
        NULL,
        1, // priority
        &loopCore0Handle,
        0 // core
    );

}

void loopNormalOperation()
{
    Button pressed = buttonPressed();
	if (pressed == Button::Enter)
	{
		Serial.println("enter was pressed");
        temporaryScheduleSetup();
	}
	updateDisplay();
}

void setupCore0(void *param)
{
    Serial.printf("setup started on core %u", xPortGetCoreID());
    bool *p = (bool *) param;
    dht.begin();
    if (!connectSTAMode())
	{
		// error connecting to wifi
		wifiWorking = false;
	}
    NTP.begin(String(FPSTR(ntpServer)), timezoneOffset, timezoneDST, timezoneOffsetMinutes);
	NTP.setInterval(ntpInterval, ntpInterval);
    if (wifiWorking)
	{
		// if wifi works, we try to get NTP time
		Serial.printf("core %u: trying to get time\n", xPortGetCoreID());
		int i = 0;
		while (NTP.getLastNTPSync() == 0 && i < timesTryNTP) // we try timesTryNTP times, before giving up
		{
			Serial.printf("core %u: attempt nr %d\n", xPortGetCoreID(), i);
			time_t t = NTP.getTime();
			if (t != 0)
				setTime(t);
			i++;
			delay(500);
			// problem here: if i disconnect the internet cable from the router and reconnect it during the while, sometimes the esp is reset by the hardware wdt (??)
		}
		if (NTP.getLastNTPSync() == 0)
		{
			NTPWorking = false;
		}

        Serial.printf("core %u: initialize firebase\n", xPortGetCoreID());
		firebaseWorking = firebaseClient.initializeStream();
	}
    else
	{
		Serial.printf("core %u: bypassed ntp because wifi doesnt work\n", xPortGetCoreID());
		NTPWorking = false;
        Serial.printf("core %u: bypassed firebase init because wifi doesnt work\n", xPortGetCoreID());
		firebaseWorking = false;
	}

    // the mandatory setup is done, core 1 can display the eventual errors and enter manual time setup if needed
    *p = true;

    // delete this task
    vTaskDelete(NULL);
}

void loopCore0(void *param)
{
    while (1)
    {
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
                    WiFiClientSecure client;
                    client.setCACert(firebaseRootCA);
                    getHttp.begin(client, String(F("https://")) + FPSTR(firebasePath) + FPSTR(schedulePath) + F(".json?auth=") + FPSTR(auth));
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
                        scheduleString = getHttp.getString();
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
        // we delay multiple times per loop iteration, so that, in case all the code gets executed, it won't block essential tasks for the esp and cause task wdt to reset
        delay(50);
        // we check if wifi works
        wifiWorking = WiFi.isConnected();
        // if we have never succesfully downloaded the time, or if timesTryNTP * ntpInterval time has passed since the last succesfull time sync, we set NTPWorking to false
        NTPWorking = NTP.getLastNTPSync() != 0 && now() - NTP.getLastNTPSync() < timesTryNTP * ntpInterval;
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
            Serial.printf("wifiWorking:%d ", wifiWorking);
            Serial.printf("NTPWorking:%d ", NTPWorking);
            Serial.printf("humWorking:%d ", humWorking);
            Serial.printf("tempWorking: %d\n", tempWorking);
        }
        // we delay multiple times per loop iteration, so that, in case all the code gets executed, it won't block essential tasks for the esp and cause task wdt to reset
        delay(50);
        // we update temperature, humidity every intervalUpdateTemperature milliseconds, and we reevaluate the schedule
        if (millis() - lastTemperatureUpdate >= intervalUpdateTemperature)
        {
            lastTemperatureUpdate = millis();
            updateTemp();
            updateHum();
            // if a temporary schedule is active, we ignore the normal schedule and follow it
            // otherwise, we evaluate the normal schedule
            if (temporaryScheduleActive)
            {
                if (now() < temporaryScheduleEnd || temporaryScheduleEnd == -1)
                {
                    bool signal = compareTemperatureWithSetTemperature(temperature, temporaryScheduleTemp);
                    sendSignalToHeater(signal);
                }
                else
                {
                    // if the temporary schedule has expired, we deactivate it
                    temporaryScheduleActive = false;
                }
                
            }
            else
            {
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
    Serial.println("SetupWifi setup");
    setupWifiDisplayInfo();

    WiFi.mode(WIFI_AP);
    WiFi.softAP(setupWifiAPSSID, setupWifiAPPassword);
    delay(1000); // delay to let the AP initialize
    WiFi.softAPConfig(setupWifiServerIP, setupWifiServerIP, IPAddress(255, 255, 255, 0));

    server.on("/", setupWifiHandleRoot);
    server.on("/post", HTTPMethod::HTTP_POST, setupWifiHandlePost);
    server.begin();
    Serial.println("Server started");
}

void loopSetupWifi()
{
    server.handleClient();
}

void setupOTAUpdate()
{
    Serial.println("OTAUpdate setup");

    if (!connectSTAMode())
    {
        Serial.println(FPSTR(errorWifiConnect));
        simpleDisplay(String(FPSTR(errorWifiConnect)));
        // if wifi doesn't work, we do nothing
        while (true) { delay(100); }
    }
    Serial.println(FPSTR(updateWaiting));
    simpleDisplay(FPSTR(updateWaiting));
    ArduinoOTA.setHostname(otaHostname);
    ArduinoOTA
        .onStart([]()
        {
            Serial.println(FPSTR(updateStarted));
		    simpleDisplay(String(FPSTR(updateStarted)));
        })
        .onEnd([]()
        {
            Serial.println(FPSTR(updateEnded));
		    simpleDisplay(String(FPSTR(updateEnded)));
        })
        .onProgress([](unsigned int progress, unsigned int total)
        {
            display.clearDisplay();
            Serial.printf_P(updateProgress, progress * 100 / total);
            display.printf_P(updateProgress, progress * 100 / total);
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
                    Serial.println(FPSTR(updateErrorAuth));
                    display.println(FPSTR(updateErrorAuth));
                    break;
                case OTA_BEGIN_ERROR:
                    Serial.println(FPSTR(updateErrorBegin));
                    display.println(FPSTR(updateErrorBegin));
                    break;
                case OTA_CONNECT_ERROR:
                    Serial.println(FPSTR(updateErrorConnect));
                    display.println(FPSTR(updateErrorConnect));
                    break;
                case OTA_RECEIVE_ERROR:
                    Serial.println(FPSTR(updateErrorReceive));
                    display.println(FPSTR(updateErrorReceive));
                    break;
                case OTA_END_ERROR:
                    Serial.println(FPSTR(updateErrorEnd));
                    display.println(FPSTR(updateErrorEnd));
                    break;
            }
            display.display();
        });
        ArduinoOTA.begin();
}

void loopOTAUpdate()
{
    ArduinoOTA.handle();
}

void setupWifiDisplayInfo()
{
    display.clearDisplay();
	display.setTextSize(1);
	display.setTextColor(BLACK);
	display.setCursor(0, 0);
	display.println(F("Connect to:"));
	display.println(setupWifiAPSSID);
	display.println(F("With password:"));
	display.println(setupWifiAPPassword);
	display.display();
}

// function called when a client accesses the root of the server, sends back to the client a webpage with a form
void setupWifiHandleRoot()
{
	server.send(HTTP_CODE_OK, "text/html", String(FPSTR(setupWifiPage)));
}

// function called when a client sends a POST request to the server, at location /post, stores the SSID and password from the request in SPIFFS
void setupWifiHandlePost()
{
    if (!server.hasArg("ssid") || !server.hasArg("password"))
	{
		server.send(HTTP_CODE_BAD_REQUEST, "text/plain", String(FPSTR(serverNotAllArgsPresent)));
		Serial.println(FPSTR(serverNotAllArgsPresent));
		return;
	}
	server.send(HTTP_CODE_OK, "text/plain", String(FPSTR(serverReceivedArgs)));
	String ssid = server.arg("ssid");
	String password = server.arg("password");
	Serial.printf("[%s]", ssid.c_str());
    Serial.printf("[%s]", password.c_str());
    
    storeCredentials(ssid, password);
    // we display a success message and restart the ESP
	display.clearDisplay();
	display.setCursor(0, 0);
    display.setTextColor(BLACK);
	display.println(FPSTR(gotCredentials));
	display.println(ssid);
	display.println(password);
	display.display();
    delay(5000);
    ESP.restart();
}

// stores the provided Wifi credentials in SPIFFS
void storeCredentials(const String &ssid, const String &password)
{
    if (!SPIFFS.begin())
	{
		Serial.println(FPSTR(errorOpenSPIFFS));
		simpleDisplay(String(FPSTR(errorOpenSPIFFS)));
        delay(5000);
        ESP.restart();
	}
	File wifiConfigFile = SPIFFS.open("/config.txt", "w+");
	if (!wifiConfigFile)
	{
        // error opening the file
		Serial.println(FPSTR(errorOpenConfigWrite));
		simpleDisplay(String(FPSTR(errorOpenConfigWrite)));
        ESP.restart();
	}
	wifiConfigFile.println(ssid);
	wifiConfigFile.println(password);
	wifiConfigFile.close();
	SPIFFS.end();
	Serial.println("Stored credentials successfully");
}

// gets the Wifi login credentials from the SPIFFS
void getCredentials(String &ssid, String &password)
{
	if (!SPIFFS.begin())
	{
		Serial.println(FPSTR(errorOpenSPIFFS));
		simpleDisplay(String(FPSTR(errorOpenSPIFFS)));
        delay(5000);
        ESP.restart();
	}
	File wifiConfigFile = SPIFFS.open("/config.txt", "r");
	if (!wifiConfigFile)
	{
        // error opening the file
		Serial.println(FPSTR(errorOpenConfigRead));
		simpleDisplay(String(FPSTR(errorOpenConfigRead)));
        delay(5000);
        ESP.restart();
	}
	ssid = wifiConfigFile.readStringUntil('\r');
	wifiConfigFile.read(); // line ending is CR&LF so we read the \n character
	password = wifiConfigFile.readStringUntil('\r');
	wifiConfigFile.read(); // line ending is CR&LF so we read the \n character
	wifiConfigFile.close();
	SPIFFS.end();
	Serial.println("SSID: [" + ssid + "]");
	Serial.println("Password: [" + password + "]");
}

// function which looks at each schedule in the scheduleString, decides which has the top priority, and returns the signal to send to the heater
bool evaluateSchedule()
{
	if (isnan(temperature))
	{
		// the temperature can't be read (sensor error probably)
		Serial.println("temperature is nan");
		return false;
	}
	bool signal = false;
	int highestPriority = -1;
	int beginIndex = 0;
	beginIndex = scheduleString.indexOf('{', 1); // we find the first occurence of the character '{', excluding the first character; this is the beggining of the first schedule object
	while (beginIndex != -1)
	{
		int endIndex = scheduleString.indexOf('}', beginIndex + 1); // we find the next occurence of '}', starting at beginIndex; this is the end of the first schedule object
		String str = scheduleString.substring(beginIndex, endIndex + 1); // we get the schedule object as a string
		Serial.print(str);
		DynamicJsonDocument doc(400); // we allocate the document enough memory to store even the biggest kind of schedule object
		deserializeJson(doc, str);
		JsonObject schedule = doc.as<JsonObject>();
		if (scheduleIsActive(schedule))
		{
			// if a one-time schedule is active, then it has the highest priority, so we stop the loop after it
			if (schedule["repeat"] == "None")
			{
				float setTemp = schedule["setTemp"];
				signal = compareTemperatureWithSetTemperature(temperature, setTemp);
				break;
			}
			int priority = schedule["priority"];
			if (priority > highestPriority)
			{
				float setTemp = schedule["setTemp"];
				signal = compareTemperatureWithSetTemperature(temperature, setTemp);
				highestPriority = priority;
			}
		}
		beginIndex = scheduleString.indexOf('{', endIndex + 1); // we find the next occurence of '{', starting at endIndex; this is the beggining of the next schedule object; we repeat until there are no more objects
	}
	return signal;
}

// function that checks if the supplied schedule object is active at the moment
bool scheduleIsActive(const JsonObject& schedule)
{
	const char* repeat = schedule["repeat"];
	// if it is a daily schedule, we check if its start time is before the current time and if its end time is after the current time
	if (strcmp(repeat, "Daily") == 0)
	{
		int startTime = schedule["sH"].as<int>() * 60 + schedule["sM"].as<int>();
		int endTime = schedule["eH"].as<int>() * 60 + schedule["eM"].as<int>();
		time_t t = now();
		int time = hour(t) * 60 + minute(t);
		Serial.println(startTime <= time && time < endTime ? "Active" : "Not active");
		return startTime <= time && time < endTime;
	}
	// weekly schedule, we check if we are on the same weekday and between the start and end time
	if (strcmp(repeat, "Weekly") == 0)
	{
		int wDay = schedule["weekDay"]; // Sunday is day 1
		int startTime = schedule["sH"].as<int>() * 60 + schedule["sM"].as<int>();
		int endTime = schedule["eH"].as<int>() * 60 + schedule["eM"].as<int>();
		time_t t = now();
		int currWeekDay = weekday(t); // Sunday is day 1
		int time = hour(t) * 60 + minute(t);
		Serial.println(wDay == currWeekDay && startTime <= time && time < endTime ? "Active" : "Not active");
		return wDay == currWeekDay && startTime <= time && time < endTime;
	}
	// the schedule doesn't repeat, we check if we are between the start and end time
	if (strcmp(repeat, "None") == 0)
	{
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
		Serial.println(startT <= t && t < endT ? "Active" : "Not active");
		return startT <= t && t < endT;
	}
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
	// the sensor setting will be used later, to decide which temperature sensor dictates the set temperature, until i implement that, it does nothing
	int sensor = 0;
	float temp = 20.0f;
	int duration = 30;
	int option = 0;
	int sel = 0;
	if (temporaryScheduleActive)
	{
		//sensor = programTempSensor;
		temp = temporaryScheduleTemp;
		duration = -1;
	}
	unsigned long previousTime = millis();
	bool autoselect = true;
	temporaryScheduleHelper(sensor, temp, duration, option, sel);
	while (sel < 4)
	{
		Button pressed = buttonPressed();
		if (pressed == Button::None)
		{
			if (autoselect && millis() - previousTime > waitingTimeInTemporaryScheduleMenu)
				break;
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
				break;
			case 1:
				if (temp < 35.0f)
					temp += temporaryScheduleTempResolution;
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
				break;
			case 1:
				if (temp > 5.0f)
					temp -= temporaryScheduleTempResolution;
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
		temporaryScheduleHelper(sensor, temp, duration, option, sel);
	}
	if (option == 1 || autoselect)
		return;
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
	}
	else if (option == 2)
	{
		temporaryScheduleActive = false;
	}
}

// helper function that displays the selected values on the screen
void temporaryScheduleHelper(int sensor, float temp, int duration, int option, int sel)
{
	display.clearDisplay();
	display.setCursor(4, 0);
	display.println(FPSTR(temporaryScheduleString));
	display.println();

	if (sel == 0)
	{
		display.setTextColor(WHITE, BLACK);
	}
	else
	{
		display.setTextColor(BLACK);
	}
	display.printf_P(temporaryScheduleSensorString, sensor);
	if (sel == 1)
	{
		display.setTextColor(WHITE, BLACK);
	}
	else
	{
		display.setTextColor(BLACK);
	}
	display.printf_P(temporaryScheduleTempString, temp);
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
			display.print(FPSTR(temporaryScheduleDurationInfiniteString));
		}
		else if (ptDuration < 60)
		{
			display.printf_P(temporaryScheduleDuration1String, ptDuration);
		}
		else 
		{
			display.printf_P(temporaryScheduleDuration2String, ((float)ptDuration) / 60);
		}
	}
	else if (duration < 60)
	{
		display.printf_P(temporaryScheduleDuration1String, duration);
	}
	else
	{
		if (duration == 24 * 60 + 30)
			display.print(FPSTR(temporaryScheduleDurationInfiniteString));
		else
			display.printf_P(temporaryScheduleDuration2String, ((float)duration) / 60);
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
		display.print(FPSTR(temporaryScheduleOkString));
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
		display.print(FPSTR(temporaryScheduleCancelString));
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
		display.println(FPSTR(temporaryScheduleDeleteString));
	}
	else
	{
		display.setTextColor(BLACK);
		display.print(FPSTR(temporaryScheduleOkString));
		display.write(' ');
		display.print(FPSTR(temporaryScheduleCancelString));
		display.write(' ');
		display.println(FPSTR(temporaryScheduleDeleteString));
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

// cursorX and cursorY are the location of the top left corner
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
  display.printf_P(displayDateLine1String, dayShortStr(wDay), day);
  display.setCursor(cursorX, display.getCursorY());
  display.printf_P(displayDateLine2String, mth, year);
}

// cursorX and cursorY are the location of the middle left point
// it uses the DSEG7 Classic Bold font
void displayClock(int hour, int min, int cursorX, int cursorY)
{
  display.setCursor(cursorX, cursorY);
  display.setFont(&DSEG7Classic_Bold6pt7b);
  display.setTextSize(1);
  display.printf_P(displayClockFormatString, hour, min);
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
  display.printf_P(PSTR("%c%c"), DEGREESYMB, displayTempLetter);
  display.setCursor(x, y+7);
  float decimals = temp - floorf(temp);
  decimals *= 10;
  display.printf_P(PSTR(".%d"), (int)(decimals + 0.5f));
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

// prompts the user to enter the current date and time
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
		case Button::Up:
			if (manualTime[sel] < maxValue[sel])
				manualTime[sel]++;
			else
				manualTime[sel] = minValue[sel];
			break;
		case Button::Down:
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
	display.println(FPSTR(manualTimeString));

	display.setCursor(27, 15);
	if (sel == 0)
	{
		display.setTextColor(WHITE, BLACK);
		display.printf_P(manualTimeFormatString, h);
		display.setTextColor(BLACK);
	}
	else
	{
		display.setTextColor(BLACK);
		display.printf_P(manualTimeFormatString, h);
	}
	display.print(':');
	if (sel == 1)
	{
		display.setTextColor(WHITE, BLACK);
		display.printf_P(manualTimeFormatString, m);
		display.setTextColor(BLACK);
	}
	else
	{
		display.setTextColor(BLACK);
		display.printf_P(manualTimeFormatString, m);
	}

	display.setCursor(14, 30);
	if (sel == 2)
	{
		display.setTextColor(WHITE, BLACK);
		display.printf_P(manualTimeFormatString, d);
		display.setTextColor(BLACK);
	}
	else
	{
		display.setTextColor(BLACK);
		display.printf_P(manualTimeFormatString, d);
	}
	display.print('.');
	if (sel == 3)
	{
		display.setTextColor(WHITE, BLACK);
		display.printf_P(manualTimeFormatString, mth);
		display.setTextColor(BLACK);
	}
	else
	{
		display.setTextColor(BLACK);
		display.printf_P(manualTimeFormatString, mth);
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
	Serial.println("trying to connect to wifi");
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
		Serial.println("failed to connect");
		return false;
	}
	Serial.println("Connected!");
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
            Serial.println("Enter was pressed");
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
            Serial.println("Up was pressed");
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
            Serial.println("Down was pressed");
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
		Serial.println("Enter was pressed");
		return Button::Enter;
	}
	if (str == "1" || str == "SUS" || str == "sus")
	{
		Serial.println("Sus was pressed");
		return Button::Up;
	}
	if (str == "2" || str == "JOS" || str == "jos")
	{
		Serial.println("Jos was pressed");
		return Button::Down;
	}
	return Button::None;
}

void sendSignalToHeater(bool signal)
{
	// to be implemented
	heaterState = signal;
    digitalWrite(heaterPin, signal);
	virtualSendSignalToHeater(signal);
}

// prints the desired heater state in the Serial
void virtualSendSignalToHeater(bool signal)
{
    Serial.printf("Heater: %s\n", signal ? "on" : "off");
}
