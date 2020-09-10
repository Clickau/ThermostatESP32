#include <Arduino.h>
#include <WiFi.h>
#include <Adafruit_PCD8544.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <ArduinoOTA.h>
#include <lwip/apps/sntp.h>
#include <DHTesp.h>

#include "string_consts.h"
#include "settings.h"
#include "firebase_certificate.h"
#include "webpage.h"
#include "FirebaseClient.h"
#include "Logger.h"
#include "DSEG7Classic-Bold6pt.h"
#include "flame.h"

enum class Button
{
    None = 0,
    Enter,
    Up,
    Down
};


bool heaterState = false;
SemaphoreHandle_t heaterStateMutex;

String scheduleString;
SemaphoreHandle_t scheduleStringMutex;

bool wifiWorking = true;
bool ntpWorking  = true;
bool sensorWorking = true;
SemaphoreHandle_t errorsMutex;

float temperature = NAN;
int   humidity    = -1;
SemaphoreHandle_t temperatureHumidityMutex;

unsigned long          lastRetryErrors       = 0;
TickType_t             lastTemperatureUpdate = 0;
volatile unsigned long lastButtonPress       = 0;
portMUX_TYPE           lastButtonPressMux    = portMUX_INITIALIZER_UNLOCKED;

bool   temporaryScheduleActive = false;
float  temporaryScheduleTemp   = NAN;
time_t temporaryScheduleEnd    = 0;
SemaphoreHandle_t temporaryScheduleMutex;


Adafruit_PCD8544 display{pinDC, pinCS, pinRST};
FirebaseClient firebaseClient{firebaseRootCA, firebasePath, firebaseSecret};
WebServer server{80};
DHTesp dht;

TaskHandle_t setupTaskHandle;
TaskHandle_t serverTaskHandle;
TaskHandle_t updateTaskHandle;
TaskHandle_t firebaseTaskHandle;
TaskHandle_t uiTaskHandle;
TaskHandle_t buttonSubscribedTaskHandle;
TaskHandle_t sensorTaskHandle;

// Tasks
void normalOperationTask(void *);
void setupWifiTask(void *);
void otaUpdateTask(void *);
void serverLoopTask(void *);
void updateLoopTask(void *);
void firebaseLoopTask(void *);
void uiLoopTask(void *);
void sensorLoopTask(void *);

// General purpose
void sendSignalToHeater(bool signal);
void simpleDisplay(const char *str);
bool connectSTAMode();
void getCredentials(String &ssid, String &password);
void subscribeToButtonEvents(TaskHandle_t taskHandle);
void unsubscribeFromButtonEvents();

// Startup Menu
void showStartupMenu();
void startupMenuHelper(int highlightedOption);

// Manual Time
void manualTimeSetup();
void manualTimeHelper(int h, int m, int d, int mth, int y, int sel);

// Setup Wifi helpers
void setupWifiDisplayInfo();
void setupWifiHandleRoot();
void setupWifiHandlePost();
void storeCredentials(const String &ssid, const String &password);

// Display helpers
void updateDisplay();
void displayDate(int day, int mth, int year, int wDay, int cursorX, int cursorY);
void displayClock(int hour, int min, int cursorX, int cursorY);
void displayTemp(float temp, int cursorX, int cursorY);
void displayHumidity(int hum, int cursorX, int cursorY);
void displayFlame(const uint8_t *flameBitmap, int cursorX, int cursorY, int width, int height);
void displayErrors(int cursorX, int cursorY);

// Temporary Schedule
void temporaryScheduleSetup();
void temporaryScheduleHelper(float temp, int duration, int option, int sel);

// ISRs
void buttonISR(void *button);


extern "C" void app_main()
{
    initArduino();
    LOG_INIT();
    temporaryScheduleMutex = xSemaphoreCreateMutex();
    errorsMutex = xSemaphoreCreateMutex();
    temperatureHumidityMutex = xSemaphoreCreateMutex();
    scheduleStringMutex = xSemaphoreCreateMutex();
    heaterStateMutex = xSemaphoreCreateMutex();
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


/* Tasks */

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
            ntpWorking = false;
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
        ntpWorking = false;
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
        0);

    xTaskCreatePinnedToCore(
        uiLoopTask,
        "uiLoopTask",
        2048,
        nullptr,
        1,
        &uiTaskHandle,
        1);

    xTaskCreatePinnedToCore(
        sensorLoopTask,
        "sensorLoopTask",
        2048,
        nullptr,
        1,
        &sensorTaskHandle,
        0);

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
        serverLoopTask,
        "serverLoopTask",
        3072,
        nullptr,
        1,
        &serverTaskHandle);

    vTaskDelete(nullptr);
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
        while (true)
        {
            delay(100);
        }
    }
    LOG_D("Waiting for update");
    simpleDisplay(updateWaitingString);
    ArduinoOTA.setHostname(otaHostname);
    ArduinoOTA
        .onStart([]() {
            LOG_D("Update started");
            simpleDisplay(updateStartedString);
        })
        .onEnd([]() {
            LOG_D("Update ended");
            simpleDisplay(updateEndedString);
        })
        .onProgress([](unsigned int progress, unsigned int total) {
            //display.clearDisplay();
            LOG_D("Update progress: %d%%", progress * 100 / total);
            //display.printf(updateProgressFormatString, progress * 100 / total);
            //display.display();
        })
        .onError([](ota_error_t error) {
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
        &updateTaskHandle);

    vTaskDelete(nullptr);
}

void serverLoopTask(void *)
{
    LOG_T("begin");
    while (true)
    {
        server.handleClient();
        delay(5);
    }
    vTaskDelete(nullptr);
}

void updateLoopTask(void *)
{
    LOG_T("begin");
    while (true)
    {
        ArduinoOTA.handle();
        delay(5);
    }
    vTaskDelete(nullptr);
}

void firebaseLoopTask(void *)
{
    LOG_T("begin");
    while (true)
    {
        delay(500);
        if (!firebaseClient.getError())
            if (firebaseClient.consumeStreamIfAvailable())
            {
                // something in the database changed, we download the whole database
                // we try it for timesTryFirebase times, before we give up
                LOG_D("New change in Firebase stream");
                LOG_D("Trying to get new data");
                for (int i = 1; i <= timesTryFirebase; i++)
                {
                    LOG_D("Attempt %d/%d", i, timesTryFirebase);
                    xSemaphoreTake(scheduleStringMutex, portMAX_DELAY);
                    firebaseClient.getJson("/Schedules", scheduleString);
                    xSemaphoreGive(scheduleStringMutex);
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

        if (millis() - lastRetryErrors > intervalRetryErrors)
        {
            lastRetryErrors = millis();
            LOG_D("Trying to fix errors");
            bool newWifiWorking = WiFi.isConnected();
            if (!newWifiWorking)
            {
                LOG_T("Disconnecting Wifi and trying to reconnect");
                WiFi.disconnect(true);
                newWifiWorking = connectSTAMode();
            }
            xSemaphoreTake(errorsMutex, portMAX_DELAY);
            wifiWorking = newWifiWorking;
            ntpWorking = (sntp_getreachability(0) | sntp_getreachability(1) | sntp_getreachability(2)) != 0;
            xSemaphoreGive(errorsMutex);
            if (firebaseClient.getError() && newWifiWorking)
            {
                LOG_T("Initializing Firebase stream");
                firebaseClient.initializeStream("/Schedules");
            }
        }
    }

    vTaskDelete(nullptr);
}

void uiLoopTask(void *)
{
    LOG_T("begin");
    subscribeToButtonEvents(uiTaskHandle);
    while (true)
    {
        updateDisplay();
        uint32_t notificationValue = 0;
        BaseType_t result = xTaskNotifyWait(
            pdFALSE,
            ULONG_MAX,
            &notificationValue,
            100); // wait maximum 100 ticks (100 ms)
        if (result == pdTRUE)
        {
            Button pressed = static_cast<Button>(notificationValue);
            if (pressed == Button::Enter)
            {
                temporaryScheduleSetup();
            }
        }
    }
    unsubscribeFromButtonEvents();
    vTaskDelete(nullptr);
}

void sensorLoopTask(void *)
{
    LOG_T("begin");
    lastTemperatureUpdate = xTaskGetTickCount();
    while (true)
    {
        LOG_T("Updating temperature and humidity");
        auto[temp, hum] = dht.getTempAndHumidity();
        bool newSensorWorking = (dht.getStatus() == DHTesp::ERROR_NONE);
        xSemaphoreTake(errorsMutex, portMAX_DELAY);
        xSemaphoreTake(temperatureHumidityMutex, portMAX_DELAY);
        sensorWorking = newSensorWorking;
        if (newSensorWorking)
        {
            temperature = temp;
            humidity = hum;
            LOG_D("Temperature: %.1f, humidity: %d", temperature, humidity);
        }
        else
        {
            temperature = NAN;
            humidity = -1;
            LOG_D("Error reading sensor");
        }
        xSemaphoreGive(temperatureHumidityMutex);
        xSemaphoreGive(errorsMutex);

        vTaskDelayUntil(&lastTemperatureUpdate, pdMS_TO_TICKS(intervalUpdateTemperature));
    }
    vTaskDelete(nullptr);
}


/* Startup Menu */

void showStartupMenu()
{
    // operation mode:  0 - Normal Operation
    //                  1 - Setup Wifi
    //                  2 - OTA Update
    // first the selected option is Normal Operation
    LOG_T("begin");
    startupMenuHelper(0);
    size_t selectedOption = 0;
    LOG_T("selectedOption=%d", selectedOption);
    subscribeToButtonEvents(xTaskGetCurrentTaskHandle());

    uint32_t notificationValue;
    BaseType_t result = xTaskNotifyWait(
        pdFALSE,
        ULONG_MAX,
        &notificationValue,
        pdMS_TO_TICKS(waitingTimeInStartupMenu));
    if (result == pdTRUE)
    {
        // a button was pressed, do not autoselect
        while (true)
        {
            Button pressed = static_cast<Button>(notificationValue);
            if (pressed == Button::Up)
            {
                if (selectedOption != 0)
                    selectedOption--;
                else
                    selectedOption = 2;
                LOG_T("selectedOption=%d", selectedOption);
                startupMenuHelper(selectedOption);
            }
            else if (pressed == Button::Down)
            {
                if (selectedOption != 2)
                    selectedOption++;
                else
                    selectedOption = 0;
                LOG_T("selectedOption=%d", selectedOption);
                startupMenuHelper(selectedOption);
            }
            else if (pressed == Button::Enter)
                break;
            
            xTaskNotifyWait(
                pdFALSE,
                ULONG_MAX,
                &notificationValue,
                portMAX_DELAY);
        }
    }
    unsubscribeFromButtonEvents();
    // we clear the display before entering the chosen setup
    display.clearDisplay();
    display.display();
    LOG_D("User selected mode: %d", selectedOption);
    switch (selectedOption)
    {
    case 0:
        xTaskCreate(
            normalOperationTask,
            "normalOperationTask",
            8192,
            nullptr,
            1,
            &setupTaskHandle);
        break;
    case 1:
        xTaskCreate(
            setupWifiTask,
            "setupWifiTask",
            3072,
            nullptr,
            1,
            &setupTaskHandle);
        break;
    case 2:
        xTaskCreate(
            otaUpdateTask,
            "otaUpdateTask",
            5120,
            nullptr,
            1,
            &setupTaskHandle);
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


/* Temporary Schedule */

void temporaryScheduleSetup()
{
    LOG_T("begin");
    LOG_D("Entering Temporary Schedule Setup");
    float temp = 20.0f;
    // the duration is in minutes, -1 means it will use the same end time as the previous schedule, 24 * 60 + 30 means infinite duration
    int duration = 30;
    int option = 0;
    int sel = 0;
    xSemaphoreTake(temporaryScheduleMutex, portMAX_DELAY);
    if (temporaryScheduleActive)
    {
        LOG_T("Modifying current temporary schedule");
        temp = temporaryScheduleTemp;
        // the new temporary schedule will end at the same time as the old one
        duration = -1;
    }
    xSemaphoreGive(temporaryScheduleMutex);
    LOG_T("temp=%f\n"
        "duration=%d\n"
        "option=%d\n"
        "sel=%d",
        temp, duration, option, sel);
    temporaryScheduleHelper(temp, duration, option, sel);

    uint32_t notificationValue;
    BaseType_t result = xTaskNotifyWait(
        pdFALSE,
        ULONG_MAX,
        &notificationValue,
        pdMS_TO_TICKS(waitingTimeInTemporaryScheduleMenu));
    if (result == pdFALSE)
    {
        LOG_D("Exiting menu because nothing was pressed");
        return;
    }
    while (true)
    {
        Button pressed = static_cast<Button>(notificationValue);
        if (pressed == Button::Enter)
        {
            sel++;
            if (sel >= 3)
                break;
        }
        else if (pressed == Button::Up)
        {
            switch (sel)
            {
            case 0:
                if (temp < 35.0f)
                    temp += temporaryScheduleTempResolution;
                else
                    temp = 5.0f;
                LOG_T("temp=%f", temp);
                break;
            case 1:
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
            case 2:
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
            switch (sel)
            {
            case 0:
                if (temp > 5.0f)
                    temp -= temporaryScheduleTempResolution;
                else
                    temp = 35.0f;
                LOG_T("temp=%f", temp);
                break;
            case 1:
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
            case 2:
                if (option > 0)
                    option--;
                else
                    option = 2;
                LOG_T("option=%d", option);
            default:
                break;
            }
        }
        temporaryScheduleHelper(temp, duration, option, sel);

        xTaskNotifyWait(
            pdFALSE,
            ULONG_MAX,
            &notificationValue,
            portMAX_DELAY);
    }

    xSemaphoreTake(temporaryScheduleMutex, portMAX_DELAY);
    switch (option)
    {
    case 0:
        temporaryScheduleActive = true;
        temporaryScheduleTemp = temp;
        if (duration != -1)
        {
            if (duration == 24 * 60 + 30)
                temporaryScheduleEnd = -1;
            else
            {
                time_t now;
                time(&now);
                temporaryScheduleEnd = now + duration * 60;
            }
        }
        LOG_D("Saved temporary schedule");
        break;
    case 1:
        LOG_D("Return without changing anything");
        break;
    case 2:
        temporaryScheduleActive = false;
        LOG_D("Deleted temporary schedule");
        break;
    }
    xSemaphoreGive(temporaryScheduleMutex);
}

// helper function that displays the selected values on the screen
void temporaryScheduleHelper(float temp, int duration, int option, int sel)
{
    display.clearDisplay();
    display.println(temporaryScheduleTitleString);
    display.println();

    if (sel == 0)
    {
        display.setTextColor(WHITE, BLACK);
    }
    else
    {
        display.setTextColor(BLACK);
    }
    display.printf(temporaryScheduleTempFormatString, temp);
    if (sel == 1)
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
        xSemaphoreTake(temporaryScheduleMutex, portMAX_DELAY);
        if (temporaryScheduleEnd == -1)
            ptDuration = -1;
        else
        {
            time_t now;
            time(&now);
            ptDuration = (temporaryScheduleEnd - now) / 60;
        }
        xSemaphoreGive(temporaryScheduleMutex);
        if (ptDuration == -1)
        {
            display.print(temporaryScheduleDurationInfiniteString);
        }
        else if (ptDuration < 60)
        {
            display.printf(temporaryScheduleDuration1FormatString, ptDuration);
        }
        else
        {
            display.printf(temporaryScheduleDuration2FormatString, ((float)ptDuration) / 60);
        }
    }
    else if (duration < 60)
    {
        display.printf(temporaryScheduleDuration1FormatString, duration);
    }
    else
    {
        if (duration == 24 * 60 + 30)
            display.print(temporaryScheduleDurationInfiniteString);
        else
            display.printf(temporaryScheduleDuration2FormatString, ((float)duration) / 60);
    }
    if (sel == 2)
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


/* Display helpers */

void updateDisplay()
{
    display.clearDisplay();
    time_t now;
    tm tmnow;
    time(&now);
    localtime_r(&now, &tmnow);
    displayDate(tmnow.tm_mday, tmnow.tm_mon + 1, tmnow.tm_year + 1900, tmnow.tm_wday, 3, 32);
    displayClock(tmnow.tm_hour, tmnow.tm_min, 42, 20);
    xSemaphoreTake(temperatureHumidityMutex, portMAX_DELAY);
    displayTemp(temperature, 48, 32);
    displayHumidity(humidity, 3, 18);
    xSemaphoreGive(temperatureHumidityMutex);
    displayFlame(flame, 75, 0, 8, 12); // the last two arguments are the width and the height of the flame icon
    displayErrors(0, 0);
    display.display();
}

// cursorX and cursorY are the location of the top left corner
void displayErrors(int cursorX, int cursorY)
{
    xSemaphoreTake(errorsMutex, portMAX_DELAY);
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
        if (!ntpWorking)
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

    if (!sensorWorking)
    {
        display.print(displayErrorSensorString);
        display.write(' ');
    }
    xSemaphoreGive(errorsMutex);
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
    display.printf(displayHumidityFormatString, hum);
}

// cursorX and cursorY are the location of the top left corner
// only displays the flame if the heater is on
void displayFlame(const uint8_t *flameBitmap, int cursorX, int cursorY, int width, int height)
{
    xSemaphoreTake(heaterStateMutex, portMAX_DELAY);
    if (heaterState)
        display.drawBitmap(cursorX, cursorY, flameBitmap, width, height, BLACK);
    xSemaphoreGive(heaterStateMutex);
}

// cursorX and cursorY are the location of the top left corner
// sunday is wDay 0
void displayDate(int day, int mth, int year, int wDay, int cursorX, int cursorY)
{
    display.setCursor(cursorX, cursorY);
    display.setFont();
    display.setTextSize(1);
    display.printf(displayDateLine1FormatString, displayShortWeekdayStrings[wDay], day);
    display.setCursor(cursorX, display.getCursorY());
    display.printf(displayDateLine2FormatString, mth, year);
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
    int x = display.getCursorX() - 1;
    int y = display.getCursorY();
    display.setCursor(x, y - 2);
    display.printf("%c%c", char(247), displayTempLetter); // char(247) is the degree symbol °
    display.setCursor(x, y + 7);
    float decimals = temp - floorf(temp);
    decimals *= 10;
    display.printf(".%d", (int)(decimals + 0.5f));
}


/* Manual Time */

// prompts the user to enter the current date and time
void manualTimeSetup()
{
    LOG_T("begin");
    int manualTime[] = {0, 0, 1, 1, 2020}; // int h=0, m=0, d=1, mth=1, y=2020;
    const int maxValue[] = {23, 59, 31, 12, 2100};
    const int minValue[] = {0, 0, 1, 1, 2020};
    int sel = 0;
    manualTimeHelper(manualTime[0], manualTime[1], manualTime[2], manualTime[3], manualTime[4], sel);
    LOG_T("hour=%d\n"
          "minute=%d\n"
          "day=%d\n"
          "month=%d\n"
          "year=%d\n"
          "Selected=%d",
          manualTime[0], manualTime[1], manualTime[2], manualTime[3], manualTime[4], sel);
    
    subscribeToButtonEvents(setupTaskHandle);
    while (sel < 5)
    {
        uint32_t notificationValue;
        xTaskNotifyWait(
            pdFALSE,
            ULONG_MAX,
            &notificationValue,
            portMAX_DELAY);
        Button pressed = static_cast<Button>(notificationValue);
        switch (pressed)
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
    unsubscribeFromButtonEvents();

    LOG_T("hour=%d\n"
          "minute=%d\n"
          "day=%d\n"
          "month=%d\n"
          "year=%d",
          manualTime[0], manualTime[1], manualTime[2], manualTime[3], manualTime[4]);
    tm newTm;
    newTm.tm_sec = 0;
    newTm.tm_hour = manualTime[0];
    newTm.tm_min = manualTime[1];
    newTm.tm_mday = manualTime[2];
    newTm.tm_mon = manualTime[3] - 1;
    newTm.tm_year = manualTime[4] - 1900;
    newTm.tm_isdst = -1; // let mktime decide if the DST is active at the respective time
    time_t newTime = mktime(&newTm);
    timeval newTv = {newTime, 0};
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


/* Setup Wifi Helpers */

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
        while (true)
        {
            delay(1000);
        }
    }
    LOG_T("Opening config.txt for writing");
    File wifiConfigFile = SPIFFS.open("/config.txt", "w+");
    if (!wifiConfigFile)
    {
        LOG_E("Error opening config file for writing");
        simpleDisplay(errorOpenConfigWriteString);
        LOG_E("Waiting for user intervention");
        while (true)
        {
            delay(1000);
        }
    }
    wifiConfigFile.println(ssid);
    wifiConfigFile.println(password);
    wifiConfigFile.close();
    SPIFFS.end();
    LOG_D("Stored credentials successfully");
}


/* General purpose */

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

void subscribeToButtonEvents(TaskHandle_t taskHandle)
{
    buttonSubscribedTaskHandle = taskHandle;
    static_assert(sizeof(Button) <= sizeof(void *));
    static_assert(sizeof(Button) <= sizeof(uint32_t));
    attachInterruptArg(pinUp, buttonISR, (void *) Button::Up, RISING);
    attachInterruptArg(pinDown, buttonISR, (void *) Button::Down, RISING);
    attachInterruptArg(pinEnter, buttonISR, (void *) Button::Enter, RISING);
}

void unsubscribeFromButtonEvents()
{
    detachInterrupt(pinUp);
    detachInterrupt(pinDown);
    detachInterrupt(pinEnter);
    buttonSubscribedTaskHandle = nullptr;
}

void sendSignalToHeater(bool signal)
{
    LOG_D("Sending signal to heater: %s", signal ? "on" : "off");
    xSemaphoreTake(heaterStateMutex, portMAX_DELAY);
    heaterState = signal;
    xSemaphoreGive(heaterStateMutex);
    digitalWrite(pinHeater, signal);
}


/* ISRs */

void IRAM_ATTR buttonISR(void *button)
{
    portENTER_CRITICAL_ISR(&lastButtonPressMux);
    if (millis() - lastButtonPress >= buttonDebounceTime)
    {
        lastButtonPress = millis();
        BaseType_t higherPriorityTaskWoken = pdFALSE;
        uint32_t buttonValue = (uint32_t) button;
        xTaskNotifyFromISR(
            buttonSubscribedTaskHandle,
            buttonValue,
            eSetValueWithoutOverwrite,
            &higherPriorityTaskWoken);
        if (higherPriorityTaskWoken == pdTRUE)
        {
            portYIELD_FROM_ISR();
        }
    }
    portEXIT_CRITICAL_ISR(&lastButtonPressMux);
}