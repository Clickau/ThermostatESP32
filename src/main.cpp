#include <Arduino.h>
#include <WiFi.h>
#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <ArduinoOTA.h>

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

// Tasks
void normalOperationTask(void *);
void setupWifiTask(void *);
void otaUpdateTask(void *);
void serverTask(void *);
void updateTask(void *);

// General purpose
void sendSignalToHeater(bool signal);
void showStartupMenu();
void startupMenuHelper(int highlightedOption);
Button buttonPressed();
void simpleDisplay(const char *str);
bool connectSTAMode();
void getCredentials(String &ssid, String &password);

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
        xTaskCreatePinnedToCore(
            normalOperationTask,
            "normalOperationTask",
            8192,
            nullptr,
            1,
            &setupTaskHandle,
            0
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
    vTaskDelete(nullptr);
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
        serverTask,
        "serverTask",
        3072,
        nullptr,
        1,
        &serverTaskHandle
    );

    vTaskDelete(nullptr);
}

void serverTask(void *)
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
        updateTask,
        "updateTask",
        3072,
        nullptr,
        1,
        &updateTaskHandle
    );

    vTaskDelete(nullptr);
}

void updateTask(void *)
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