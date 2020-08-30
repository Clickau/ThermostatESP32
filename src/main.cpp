#include <Arduino.h>
#include <WiFi.h>
#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <ArduinoOTA.h>
#include <lwip/apps/sntp.h>

#include "string_consts.h"
#include "settings.h"
#include "firebase_certificate.h"
#include "webpage.h"
#include "FirebaseClient.h"
#include "Logger.h"

enum class Button
{
    None,
    Enter,
    Up,
    Down
};

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

Adafruit_PCD8544 display{pinDC, pinCS, pinRST};
FirebaseClient firebaseClient{firebaseRootCA, firebasePath, firebaseSecret};
WebServer server{80};
DHTesp dht;

TaskHandle_t setupTaskHandle;
TaskHandle_t serverTaskHandle;
TaskHandle_t updateTaskHandle;
TaskHandle_t firebaseTaskHandle;

// Tasks
void normalOperationTask(void *);
void setupWifiTask(void *);
void otaUpdateTask(void *);
void serverLoopTask(void *);
void updateLoopTask(void *);
void firebaseLoopTask(void *);

// General purpose
void sendSignalToHeater(bool signal);
void showStartupMenu();
void startupMenuHelper(int highlightedOption);
Button buttonPressed();
void simpleDisplay(const char *str);
bool connectSTAMode();
void getCredentials(String &ssid, String &password);
void manualTimeSetup();
void manualTimeHelper(int h, int m, int d, int mth, int y, int sel);

// Setup Wifi helpers
void setupWifiDisplayInfo();
void setupWifiHandleRoot();
void setupWifiHandlePost();
void storeCredentials(const String &ssid, const String &password);

extern "C" void app_main()
{
    initArduino();
    LOG_INIT();
    // stopping the heater right at startup
    pinMode(pinHeater, OUTPUT);
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
    uint32_t count = 0;
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
        // give CPU time to the IDLE task every few hundred milliseconds
        if (++count >= 100000)
        {
            vTaskDelay(5);
            count = 0;
        }
    }
    // we clear the display before entering the chosen setup
    display.clearDisplay();
    display.display();
    if (autoselect)
    {
        currentHighlightedValue = 0;
    }
    LOG_D("User selected mode: %d", currentHighlightedValue);
    switch (currentHighlightedValue)
    {
    case 0:
        xTaskCreate(
            normalOperationTask,
            "normalOperationTask",
            8192,
            nullptr,
            1,
            &setupTaskHandle
        );
        break;
    case 1:
        xTaskCreate(
            setupWifiTask,
            "setupWifiTask",
            3072,
            nullptr,
            1,
            &setupTaskHandle
        );
        break;
    case 2:
        xTaskCreate(
            otaUpdateTask,
            "otaUpdateTask",
            5120,
            nullptr,
            1,
            &setupTaskHandle
        );
        break;
    }
}

void startupMenuHelper(int highlightedOption)
{
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);

    display.println(menuTitleString);

    if (highlightedOption == 0)
        display.setTextColor(WHITE, BLACK);
    else
        display.setTextColor(BLACK);

    display.println(menuNormalModeString);

    if (highlightedOption == 1)
        display.setTextColor(WHITE, BLACK);
    else
        display.setTextColor(BLACK);

    display.println(menuSetupWifiString);
    
    if (highlightedOption == 2)
        display.setTextColor(WHITE, BLACK);
    else
        display.setTextColor(BLACK);

    display.println(menuOTAUpdateString);

    display.setTextColor(BLACK);

    display.display();
}

void normalOperationTask(void *)
{
    LOG_T("begin");
    LOG_T("Starting DHT sensor");
    dht.setup(pinDHT, dhtType);
    LOG_D("Started DHT sensor");
    simpleDisplay(waitingForWifiString);
    if (!connectSTAMode())
	{
        LOG_D("Error connecting to Wifi");
        simpleDisplay(errorWifiConnectString);
		wifiWorking = false;
	}
    LOG_T("Starting NTP");
    configTzTime(timezoneString, ntpServer0, ntpServer1, ntpServer2);
    LOG_D("Started NTP");
    if (wifiWorking)
    {
        LOG_D("Trying to get NTP time");
        simpleDisplay(waitingForNTPString);
        uint32_t startMillis = millis();
        while ((sntp_getreachability(0) | sntp_getreachability(1) | sntp_getreachability(2)) == 0 && millis() - startMillis < waitingTimeNTP)
        {
            delay(100);
        }
        if ((sntp_getreachability(0) | sntp_getreachability(1) | sntp_getreachability(2)) == 0)
        {
            LOG_D("Couldn't get NTP time");
			NTPWorking = false;
            LOG_D("Entering Manual Time Setup");
            simpleDisplay(errorNTPString);
            delay(3000);
            manualTimeSetup();
        }
        else
        {
            time_t now;
            time(&now);
            LOG_D("Got NTP time: %ld", now);
        }

        simpleDisplay(waitingForFirebaseString);
        LOG_D("Initializing Firebase stream");
		firebaseClient.initializeStream("/Schedules");
    }
    else
	{
        LOG_D("Bypassed getting NTP time");
		NTPWorking = false;
        LOG_D("Bypassed initializing Firebase stream");
		firebaseClient.setError(true);
        LOG_D("Entering Manual Time Setup");
        delay(3000);
        manualTimeSetup();
	}

    // we are sure we have the current time (either via ntp or manual time)
    time_t now;
    tm tmnow;
    time(&now);
    localtime_r(&now, &tmnow);
    LOG_D("Got Time: %s", ctime(&now));

	display.clearDisplay();
	display.setCursor(0, 0);
	display.println(gotTimeString);
	display.printf(gotTimeHourFormatString, tmnow.tm_hour, tmnow.tm_min);
	display.printf(gotTimeDateFormatString, tmnow.tm_mday, tmnow.tm_mon + 1, tmnow.tm_year + 1900);
	display.display();

    xTaskCreatePinnedToCore(
        firebaseLoopTask,
        "firebaseLoopTask",
        5120,
        nullptr,
        1,
        &firebaseTaskHandle,
        0
    );

    vTaskDelete(nullptr);
}

void firebaseLoopTask(void *)
{
    LOG_T("begin");
    while (true)
    {
        vTaskDelay(50);
        if (firebaseClient.getError())
            continue;
        if (firebaseClient.consumeStreamIfAvailable())
        {
            // something in the database changed, we download the whole database
            // we try it for timesTryFirebase times, before we give up
            LOG_D("New change in Firebase stream");
            LOG_D("Trying to get new data");
            for (int i = 1; i <= timesTryFirebase; i++)
            {
                LOG_D("Attempt %d/%d", i, timesTryFirebase);
                firebaseClient.getJson("/Schedules", scheduleString);
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

    vTaskDelete(nullptr);
}

// prompts the user to enter the current date and time
void manualTimeSetup()
{
    LOG_T("begin");
	int manualTime[] = {0, 0, 1, 1, 2020}; // int h=0, m=0, d=1, mth=1, y=2020;
	const int maxValue[] = {23, 59, 31, 12, 2100};
	const int minValue[] = {0, 0, 1, 1, 2020};
	int sel = 0;
	manualTimeHelper(manualTime[0], manualTime[1], manualTime[2], manualTime[3], manualTime[4], sel);
    LOG_T( "hour=%d\n"\
           "minute=%d\n"\
           "day=%d\n"\
           "month=%d\n"\
           "year=%d\n"\
           "Selected=%d",
           manualTime[0], manualTime[1], manualTime[2], manualTime[3], manualTime[4], sel );
    uint32_t count = 0;
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
        if (++count > 100)
        {
            vTaskDelay(5);
            count = 0;
        }
	}

    LOG_T( "hour=%d\n"\
        "minute=%d\n"\
        "day=%d\n"\
        "month=%d\n"\
        "year=%d",
        manualTime[0], manualTime[1], manualTime[2], manualTime[3], manualTime[4] );
    tm newTm;
    newTm.tm_sec = 0;
    newTm.tm_hour = manualTime[0];
    newTm.tm_min = manualTime[1];
    newTm.tm_mday = manualTime[2];
    newTm.tm_mon = manualTime[3] - 1;
    newTm.tm_year = manualTime[4] - 1900;
    newTm.tm_isdst = -1; // let mktime decide if the DST is active at the respective time
    time_t newTime = mktime(&newTm);
    timeval newTv = { newTime, 0 };
    settimeofday(&newTv, nullptr);
}

// helper function that displays the current selected date and time in manual time mode
void manualTimeHelper(int h, int m, int d, int mth, int y, int sel)
{
	display.clearDisplay();
	display.setTextSize(1);
	display.setTextColor(BLACK);
	display.setCursor(10, 0);
	display.println(manualTimeTitleString);

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

void setupWifiTask(void *)
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

    xTaskCreate(
        serverLoopTask,
        "serverLoopTask",
        3072,
        nullptr,
        1,
        &serverTaskHandle
    );

    vTaskDelete(nullptr);
}

void serverLoopTask(void *)
{
    LOG_T("begin");
    while (true)
    {
        server.handleClient();
        vTaskDelay(5);
    }
    vTaskDelete(nullptr);
}

void setupWifiDisplayInfo()
{
    LOG_T("begin");
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.setTextColor(BLACK);
    display.println(setupWifiSSIDAndPassString);
    display.println(setupWifiAPSSID);
    display.println(setupWifiAPPassword);
    display.println();
    display.println(setupWifiIPString);
    display.println(setupWifiServerIP);
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
        server.send(HTTP_CODE_BAD_REQUEST, "text/plain", serverNotAllArgsPresentString);
        LOG_D("Not all arguments were present in POST request");
        return;
    }
    server.send(HTTP_CODE_OK, "text/plain", serverReceivedArgsString);
    String ssid = server.arg("ssid");
    String password = server.arg("password");
    LOG_D("Received SSID and password");
    
    storeCredentials(ssid, password);
    // we display a success message and restart the ESP
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextColor(BLACK);
    display.println(gotCredentialsString);
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
        simpleDisplay(errorOpenSPIFFSString);
        LOG_E("Waiting for user intervention");
        while (true) { delay(1000); }
    }
    LOG_T("Opening config.txt for writing");
    File wifiConfigFile = SPIFFS.open("/config.txt", "w+");
    if (!wifiConfigFile)
    {
        LOG_E("Error opening config file for writing");
        simpleDisplay(errorOpenConfigWriteString);
        LOG_E("Waiting for user intervention");
        while (true) { delay(1000); }
    }
    wifiConfigFile.println(ssid);
    wifiConfigFile.println(password);
    wifiConfigFile.close();
    SPIFFS.end();
    LOG_D("Stored credentials successfully");
}

void otaUpdateTask(void *)
{
    LOG_T("begin");
    simpleDisplay(waitingForWifiString);
    if (!connectSTAMode())
    {
        LOG_E("Error connecting to Wifi");
        simpleDisplay(errorWifiConnectString);
        // if wifi doesn't work, we do nothing
        LOG_E("Awaiting reset by user");
        while (true) { delay(100); }
    }
    LOG_D("Waiting for update");
    simpleDisplay(updateWaitingString);
    ArduinoOTA.setHostname(otaHostname);
    ArduinoOTA
        .onStart([]()
        {
            LOG_D("Update started");
            simpleDisplay(updateStartedString);
        })
        .onEnd([]()
        {
            LOG_D("Update ended");
            simpleDisplay(updateEndedString);
        })
        .onProgress([](unsigned int progress, unsigned int total)
        {
            //display.clearDisplay();
            LOG_D("Update progress: %d%%", progress * 100 / total);
            //display.printf(updateProgressFormatString, progress * 100 / total);
            //display.display();
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
                    display.println(updateErrorAuthString);
                    break;
                case OTA_BEGIN_ERROR:
                    LOG_E("Update Begin Error");
                    display.println(updateErrorBeginString);
                    break;
                case OTA_CONNECT_ERROR:
                    LOG_E("Update Connect Error");
                    display.println(updateErrorConnectString);
                    break;
                case OTA_RECEIVE_ERROR:
                    LOG_E("Update Receive Error");
                    display.println(updateErrorReceiveString);
                    break;
                case OTA_END_ERROR:
                    LOG_E("Update End Error");
                    display.println(updateErrorEndString);
                    break;
            }
            display.display();
        });
    LOG_T("Starting OTA Update client");
    ArduinoOTA.begin();
    LOG_D("Started OTA Update client");

    xTaskCreate(
        updateLoopTask,
        "updateLoopTask",
        3072,
        nullptr,
        1,
        &updateTaskHandle
    );

    vTaskDelete(nullptr);
}

void updateLoopTask(void *)
{
    LOG_T("begin");
    while (true)
    {
        ArduinoOTA.handle();
        vTaskDelay(5);
    }
    vTaskDelete(nullptr);
}

// helper function that clears the display and prints the String parameter in the top left corner
void simpleDisplay(const char *str)
{
    display.clearDisplay();
    display.setTextColor(BLACK);
    display.setCursor(0, 0);
    display.println(str);
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

// gets the Wifi login credentials from the SPIFFS
void getCredentials(String &ssid, String &password)
{
    LOG_T("begin");
    // we begin SPIFFS with formatOnFail=true, so that, if the initial begin fails, it will format the filesystem and try again
    LOG_T("Starting SPIFFS");
    if (!SPIFFS.begin(true))
    {
        LOG_E("Error starting SPIFFS");
        simpleDisplay(errorOpenSPIFFSString);
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
        simpleDisplay(errorOpenConfigReadString);
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

void sendSignalToHeater(bool signal)
{
    LOG_D("Sending signal to heater: %s", signal ? "on" : "off");
    heaterState = signal;
    digitalWrite(pinHeater, signal);
}