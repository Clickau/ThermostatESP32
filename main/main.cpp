#include <Arduino.h>
#include <Adafruit_PCD8544.h>
#include <lwip/apps/sntp.h>
#include <DHTesp.h>
#include <ArduinoJson.h>
#include <tcpip_adapter.h>
#include <esp_event.h>
#include <esp_wifi.h>
#include <cstring>
#include <nvs_flash.h>
#include <esp_http_server.h>
#include <mdns.h>

#include "string_consts.h"
#include "settings.h"
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

struct settings_t 
{
    uint8_t ssid[32];
    uint8_t password[64];
    char firebaseURL[64];
    char firebaseSecret[41];
    char timezone[64];
} settings;

extern const char firebaseRootCA[] asm("_binary_firebaseio_root_ca_pem_start");

volatile bool wifiWorking = false;
SemaphoreHandle_t wifiWorkingMutex;

bool heaterState = false;
SemaphoreHandle_t heaterStateMutex;

String scheduleString;
SemaphoreHandle_t scheduleStringMutex;

float   temperature     = NAN;
int     humidity        = -1;
uint8_t dhtReachability = 0;
SemaphoreHandle_t sensorValuesMutex;

unsigned long          lastRetryErrors       = 0;
unsigned long          lastUploadState       = 0;
TickType_t             lastTemperatureUpdate = 0;
volatile unsigned long lastButtonPress       = 0;
portMUX_TYPE           lastButtonPressMux    = portMUX_INITIALIZER_UNLOCKED;

bool    temporaryScheduleActive = false;
float   temporaryScheduleTemp   = NAN;
int64_t temporaryScheduleEnd    = 0;
SemaphoreHandle_t temporaryScheduleMutex;


Adafruit_PCD8544 display{pinDC, pinCS, pinRST};
FirebaseClient firebaseClient;
DHTesp dht;

TaskHandle_t setupTaskHandle;
TaskHandle_t firebaseTaskHandle;
TaskHandle_t uiTaskHandle;
TaskHandle_t buttonSubscribedTaskHandle;
TaskHandle_t sensorTaskHandle;
TaskHandle_t evaluateSchedulesTaskHandle;

// Tasks
void normalOperationTask(void *);
void setupTask(void *);
void firebaseLoopTask(void *);
void uiLoopTask(void *);
void sensorLoopTask(void *);
void evaluateSchedulesLoopTask(void *);

// General purpose
void sendSignalToHeater(bool signal);
void simpleDisplay(const char *str);
bool connectSTAMode();
void subscribeToButtonEvents(TaskHandle_t taskHandle);
void unsubscribeFromButtonEvents();
bool loadSettings();

// Startup Menu
void showStartupMenu();
void startupMenuHelper(int highlightedOption);

// Manual Time
void manualTimeSetup();
void manualTimeHelper(int h, int m, int d, int mth, int y, int sel);

// Setup helpers
void setupDisplayInfo();
esp_err_t setupGetInfoHandler(httpd_req_t *req);
esp_err_t setupGetSettingsHandler(httpd_req_t *req);
esp_err_t setupPostSettingsHandler(httpd_req_t *req);
esp_err_t setupRestartHandler(httpd_req_t *req);

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

// Schedule evaluation helpers
bool cmpTempSetTemp(float temp, float setTemp);
bool scheduleIsActive(JsonObjectConst schedule);

// ISRs
void buttonISR(void *button);

// Event handlers
void wifi_event_handler(void *, esp_event_base_t base, int32_t id, void *);


const httpd_uri_t setupGetInfoURI      = { "/", HTTP_GET, setupGetInfoHandler, nullptr };
const httpd_uri_t setupGetSettingsURI  = { "/settings", HTTP_GET, setupGetSettingsHandler, nullptr };
const httpd_uri_t setupPostSettingsURI = { "/settings", HTTP_POST, setupPostSettingsHandler, nullptr };
const httpd_uri_t setupRestartURI      = { "/restart", HTTP_GET, setupRestartHandler, nullptr };


extern "C" void app_main()
{
    initArduino();
    LOG_INIT();
    temporaryScheduleMutex = xSemaphoreCreateMutex();
    sensorValuesMutex = xSemaphoreCreateMutex();
    scheduleStringMutex = xSemaphoreCreateMutex();
    heaterStateMutex = xSemaphoreCreateMutex();
    wifiWorkingMutex = xSemaphoreCreateMutex();
    // stopping the heater right at startup
    pinMode(pinHeater, OUTPUT);
    pinMode(pinUp, INPUT_PULLDOWN);
    pinMode(pinDown, INPUT_PULLDOWN);
    pinMode(pinEnter, INPUT_PULLDOWN);
    sendSignalToHeater(false);
    display.clearDisplay();
    display.begin();
    display.setContrast(displayContrast);

    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(nullptr, nullptr));
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    bool success = loadSettings();
    if (!success)
    {
        LOG_D("Settings not found, entering Setup");
        simpleDisplay(errorSettingsNotFound);
        delay(3000);
        xTaskCreate(
            setupTask,
            "setupTask",
            3072,
            nullptr,
            1,
            &setupTaskHandle);
        return;
    }

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
    firebaseClient.begin(firebaseRootCA, settings.firebaseURL, settings.firebaseSecret, "/Schedules.json");
    simpleDisplay(waitingForWifiString);
    bool wifiWorking = true;
    if (!connectSTAMode())
    {
        LOG_D("Error connecting to Wifi");
        simpleDisplay(errorWifiConnectString);
        wifiWorking = false;
    }
    LOG_T("Starting NTP");
    configTzTime(settings.timezone, ntpServer0, ntpServer1, ntpServer2);
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
        firebaseClient.initializeStream();
    }
    else
    {
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

    xTaskCreatePinnedToCore(
        evaluateSchedulesLoopTask,
        "evaluateSchedulesLoopTask",
        3096, 
        nullptr,
        2,
        &evaluateSchedulesTaskHandle,
        0);

    // deactivate the temporary schedule in Firebase
    xTaskNotifyGive(firebaseTaskHandle);

    vTaskDelete(nullptr);
}

void setupTask(void *)
{
    LOG_T("begin");
    setupDisplayInfo();

    LOG_T("Starting Wifi AP");
    wifi_config_t wifi_config = {};
    strcpy((char *) wifi_config.ap.ssid, setupAPSSID);
    strcpy((char *) wifi_config.ap.password, setupAPPassword);
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.ap.max_connection = 1;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    LOG_T("Started Wifi AP");

    LOG_T("Starting mDNS");
    esp_err_t err = mdns_init();
    if (err != ESP_OK)
    {
        LOG_E("Error starting mDNS");
    }
    else
    {
        LOG_D("Started mDNS");
        mdns_hostname_set(mDNSHostname);
    }
    

    LOG_T("Starting server");
    httpd_handle_t server = nullptr;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    err = httpd_start(&server, &config);
    if (err != ESP_OK)
    {
        LOG_E("Error starting server: %d", err);
        simpleDisplay(errorSetupServer);
        vTaskDelete(nullptr);
        return;
    }

    httpd_register_uri_handler(server, &setupGetInfoURI);
    httpd_register_uri_handler(server, &setupGetSettingsURI);
    httpd_register_uri_handler(server, &setupPostSettingsURI);
    httpd_register_uri_handler(server, &setupRestartURI);
    LOG_D("Started server");

    vTaskDelete(nullptr);
}

void firebaseLoopTask(void *)
{
    LOG_T("begin");
    while (true)
    {
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
                    firebaseClient.getJson("/Schedules.json", scheduleString);
                    xSemaphoreGive(scheduleStringMutex);
                    if (!firebaseClient.getError())
                        break;
                }
                if (!firebaseClient.getError())
                {
                    LOG_D("Got new schedules");

                    xTaskNotifyGive(evaluateSchedulesTaskHandle);
                }
                else
                {
                    LOG_D("Failed to get new schedules");
                }
            }

        if (!firebaseClient.getError() && millis() - lastUploadState > intervalUploadState)
        {
            lastUploadState = millis();
            char state[100];
            xSemaphoreTake(sensorValuesMutex, portMAX_DELAY);
            xSemaphoreTake(heaterStateMutex, portMAX_DELAY);
            if (!isnan(temperature))
            {
                snprintf(state, sizeof(state), 
                    R"==({"temperature": %.1f, "humidity": %d, "state": %s, "time": {".sv": "timestamp"}})==",
                    isnan(temperature) ? -1.0f : temperature, humidity, heaterState ? "true" : "false");
            }
            else
            {
                strcpy(state, R"==({"temperature": "nan", "humidity": -1, "state": false, "time": {".sv": "timestamp"}})==");
            }
            xSemaphoreGive(sensorValuesMutex);
            xSemaphoreGive(heaterStateMutex);
            firebaseClient.pushJson("/State.json", state);
            if (firebaseClient.getError())
            {
                LOG_D("Error uploading state");
            }
        }

        uint32_t notification = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));
        if (!firebaseClient.getError() && notification)
        {
            // upload temporary schedule
            LOG_T("Uploading temporary schedule");
            char string[100];
            xSemaphoreTake(temporaryScheduleMutex, portMAX_DELAY);
            if (temporaryScheduleActive)
            {
                snprintf(string, sizeof(string),
                    R"==({"active": true, "temperature": %.1f, "remaining": %lld, "time": {".sv": "timestamp"}})==",
                    temporaryScheduleTemp, (temporaryScheduleEnd == -1) ? -1 : temporaryScheduleEnd - millis());
            }
            else
            {
                strcpy(string, R"==({"active": false})==");
            }
            xSemaphoreGive(temporaryScheduleMutex);
            firebaseClient.setJson("/TemporarySchedule.json", string);
            if (firebaseClient.getError())
            {
                LOG_D("Error uploading temporary schedule");
            }
        }

        if (millis() - lastRetryErrors > intervalRetryErrors)
        {
            lastRetryErrors = millis();
            LOG_D("Trying to fix errors");
            xSemaphoreTake(wifiWorkingMutex, portMAX_DELAY);
            bool wifiWorkingCopy = wifiWorking;
            xSemaphoreGive(wifiWorkingMutex);
            if (firebaseClient.getError() && wifiWorkingCopy)
            {
                LOG_T("Initializing Firebase stream");
                firebaseClient.initializeStream();
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
        xSemaphoreTake(sensorValuesMutex, portMAX_DELAY);
        dhtReachability <<= 1;
        if (dht.getStatus() == DHTesp::ERROR_NONE)
        {
            temperature = temp;
            humidity = hum;
            dhtReachability |= 1;
            LOG_D("Temperature: %.1f, humidity: %d, reachability: %hho", temperature, humidity, dhtReachability);
        }
        else
        {
            LOG_D("Error reading sensor, reachability: %hho", dhtReachability);
            if (dhtReachability == 0)
            {
                temperature = NAN;
                humidity = -1;
            }
        }
        xSemaphoreGive(sensorValuesMutex);
        xTaskNotifyGive(evaluateSchedulesTaskHandle);

        vTaskDelayUntil(&lastTemperatureUpdate, pdMS_TO_TICKS(intervalUpdateTemperature));
    }
    vTaskDelete(nullptr);
}

void evaluateSchedulesLoopTask(void *)
{
    LOG_T("begin");
    while (true)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        xSemaphoreTake(sensorValuesMutex, portMAX_DELAY);
        if (isnan(temperature))
        {
            xSemaphoreGive(sensorValuesMutex);
            sendSignalToHeater(false);
            continue;
        }
        xSemaphoreTake(temporaryScheduleMutex, portMAX_DELAY);
        if (temporaryScheduleActive)
        {
            LOG_D("Temporary schedule is active");
            if (temporaryScheduleEnd == -1 || millis() < temporaryScheduleEnd)
            {
                bool signal = cmpTempSetTemp(temperature, temporaryScheduleTemp);
                sendSignalToHeater(signal);
            }
            else
            {
                LOG_D("Temporary schedule expired");
                temporaryScheduleActive = false;
                xTaskNotifyGive(firebaseTaskHandle);
            }
            xSemaphoreGive(sensorValuesMutex);
            xSemaphoreGive(temporaryScheduleMutex);
        }
        else
        {
            xSemaphoreGive(temporaryScheduleMutex);
            float temperatureCopy = temperature;
            xSemaphoreGive(sensorValuesMutex);
            LOG_D("Evaluating schedules");
            xSemaphoreTake(scheduleStringMutex, portMAX_DELAY);
            // we find the active schedule from each category (daily, weekly, nonrepeating) if it exists
            // in the end we give priority to the nonrepeating one, then to the weekly, then daily
            bool onceScheduleActive = false, weeklyScheduleActive = false, dailyScheduleActive = false;
            bool onceScheduleSignal = false, weeklyScheduleSignal = false, dailyScheduleSignal = false;
            // we find the first occurrence of the character '{', excluding the first character; this is the beggining of the first schedule object
            int beginIndex = scheduleString.indexOf('{', 1);
            while (beginIndex != -1)
            {
                // we find the next occurrence of '}', starting at beginIndex; this is the end of the first schedule object
                int endIndex = scheduleString.indexOf('}', beginIndex + 1);
                StaticJsonDocument<400> doc;
                auto error = deserializeJson(doc, scheduleString.c_str() + beginIndex, endIndex - beginIndex + 1);
                if (error)
                {
                    LOG_D("Invalid schedule");
                }
                else
                {
                    auto schedule = doc.as<JsonObjectConst>();
                    if (scheduleIsActive(schedule))
                    {
                        float setTemp = schedule["setTemp"];
                        const char *repeat = schedule["repeat"];
                        bool signal = cmpTempSetTemp(temperatureCopy, setTemp);

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
                }
                beginIndex = scheduleString.indexOf('{', endIndex + 1);
            }
            xSemaphoreGive(scheduleStringMutex);

            // if there was a nonrepeating schedule active, we pass its signal on
            if (onceScheduleActive)
            {
                LOG_D("Following a one time schedule");
                sendSignalToHeater(onceScheduleSignal);
                continue;
            }

            // otherwise, if there was a weekly schedule active, we pass its signal on
            if (weeklyScheduleActive)
            {
                LOG_D("Following a weekly schedule");
                sendSignalToHeater(weeklyScheduleSignal);
                continue;
            }

            // otherwise, if there was a daily schedule active, we pass its signal on
            if (dailyScheduleActive)
            {
                LOG_D("Following a daily schedule");
                sendSignalToHeater(dailyScheduleSignal);
                continue;
            }

            // if there was no schedule active, we don't turn on the heater
            LOG_D("No schedule is active");
            sendSignalToHeater(false);
        }
    }
    vTaskDelete(nullptr);
}


/* Startup Menu */

void showStartupMenu()
{
    // operation mode:  0 - Normal Operation
    //                  1 - Setup
    // first the selected option is Normal Operation
    LOG_T("begin");
    size_t selectedOption = 0;
    startupMenuHelper(selectedOption);
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
            if (pressed == Button::Up || pressed == Button::Down)
            {
                selectedOption = (selectedOption + 1) % 2;
                startupMenuHelper(selectedOption);
            }
            else if (pressed == Button::Enter)
            {
                break;
            }
            
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
            setupTask,
            "setupTask",
            3072,
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

    display.println(menuSetupString);

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
                temporaryScheduleEnd = millis() + duration * 60 * 1000;
            }
        }
        LOG_D("Saved temporary schedule");
        xTaskNotifyGive(evaluateSchedulesTaskHandle);
        xTaskNotifyGive(firebaseTaskHandle);
        break;
    case 1:
        LOG_D("Return without changing anything");
        break;
    case 2:
        temporaryScheduleActive = false;
        LOG_D("Deleted temporary schedule");
        xTaskNotifyGive(evaluateSchedulesTaskHandle);
        xTaskNotifyGive(firebaseTaskHandle);
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
            ptDuration = (temporaryScheduleEnd - millis()) / 1000 / 60;
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
    xSemaphoreTake(sensorValuesMutex, portMAX_DELAY);
    displayTemp(temperature, 48, 32);
    displayHumidity(humidity, 3, 18);
    xSemaphoreGive(sensorValuesMutex);
    displayFlame(flame, 75, 0, 8, 12); // the last two arguments are the width and the height of the flame icon
    displayErrors(0, 0);
    display.display();
}

// cursorX and cursorY are the location of the top left corner
void displayErrors(int cursorX, int cursorY)
{
    display.setCursor(cursorX, cursorY);
    xSemaphoreTake(wifiWorkingMutex, portMAX_DELAY);
    bool wifiWorkingCopy = wifiWorking;
    xSemaphoreGive(wifiWorkingMutex);
    if (!wifiWorkingCopy)
    {
        // error with wifi, no need to check if firebase and ntp work because they don't
        // also no need to display ntp and firebase errors, just wifi error
        display.print(displayErrorWifiString);
        display.write(' ');
    }
    else
    {
        if ((sntp_getreachability(0) | sntp_getreachability(1) | sntp_getreachability(2)) == 0)
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

    if (dhtReachability == 0)
    {
        display.print(displayErrorSensorString);
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


/* Setup Helpers */

void setupDisplayInfo()
{
    LOG_T("begin");
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.setTextColor(BLACK);
    display.println(setupSSIDPassString);
    display.println(setupAPSSID);
    display.println(setupAPPassword);
    display.println();
    display.println(setupIPString);
    display.println("192.168.4.1");
    display.display();
}

esp_err_t setupGetInfoHandler(httpd_req_t *req)
{
    const char *infoString = R"==({"version": 1.0, "settings": ["wifi", "firebase", "timezone"]})==";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, infoString, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t setupGetSettingsHandler(httpd_req_t *req)
{
    char response[512];
    StaticJsonDocument<512> doc;
    doc["ssid"] = settings.ssid;
    doc["firebaseURL"] = settings.firebaseURL;
    doc["timezone"] = settings.timezone;
    serializeJson(doc, response);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t setupPostSettingsHandler(httpd_req_t *req)
{
    char buffer[512];
    if (req->content_len > sizeof(buffer) - 1)
    {
        LOG_E("content_len too large: %d", req->content_len);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Content-Length too large");
        return ESP_FAIL;
    }

    int ret = httpd_req_recv(req, buffer, sizeof(buffer) - 1);
    if (ret < 0)
    {
        LOG_E("Error httpd_req_recv: %d", ret);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Error reading request");
        return ESP_FAIL;
    }
    buffer[ret] = 0;

    StaticJsonDocument<512> doc;
    deserializeJson(doc, buffer);
    const char *ssid = doc["ssid"];
    const char *password = doc["password"];
    const char *firebaseURL = doc["firebaseURL"];
    const char *firebaseSecret = doc["firebaseSecret"];
    const char *timezone = doc["timezone"];
    if (!ssid || !password || !firebaseURL || !firebaseSecret || !timezone)
    {
        LOG_W("Not all settings are present");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Not all settings are present");
        return ESP_FAIL;
    }

    settings_t new_settings = {};
    strncpy((char *) new_settings.ssid, ssid, 31);
    strncpy((char *) new_settings.password, password, 63);
    strncpy(new_settings.firebaseURL, firebaseURL, 63);
    strncpy(new_settings.firebaseSecret, firebaseSecret, 40);
    strncpy(new_settings.timezone, timezone, 63);

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("settings", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
    {
        LOG_E("Error nvs_open: %d", err);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Error saving settings");
        return ESP_FAIL;
    }
    
    err = nvs_set_blob(nvs_handle, "settings", &new_settings, sizeof(new_settings));
    if (err != ESP_OK)
    {
        nvs_close(nvs_handle);
        LOG_E("Error nvs_set_blob: %d", err);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Error saving settings");
        return ESP_FAIL;
    }
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    if (err != ESP_OK)
    {
        LOG_E("Error nvs_commit: %d", err);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Error saving settings");
        return ESP_FAIL;
    }

    settings = new_settings;

    simpleDisplay(setupReceivedSettingsString);
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t setupRestartHandler(httpd_req_t *req)
{
    simpleDisplay(setupRestartingString);
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    delay(3000);
    esp_restart();
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

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, nullptr));
    wifi_config_t wifi_config = {};
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
    strncpy((char *) wifi_config.sta.ssid, (const char *) settings.ssid, 31);
    strncpy((char *) wifi_config.sta.password, (const char *) settings.password, 63);
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // wait for got ip event or timeout
    bool success = false;
    unsigned long lastMillis = millis();
    while (true)
    {
        xSemaphoreTake(wifiWorkingMutex, portMAX_DELAY);
        success = wifiWorking;
        xSemaphoreGive(wifiWorkingMutex);
        if (success || millis() - lastMillis > waitingTimeConnectWifi)
            break;
        delay(100);
    }
    
    if (success)
    {
        LOG_D("Connected");
        return true;
    }
    else
    {
        LOG_D("Failed to connect");
        return false;
    }
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

bool loadSettings()
{
    settings = {};
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("settings", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
    {
        LOG_E("Error nvs_open: %d", err);
        return false;
    }

    size_t size = sizeof(settings);
    err = nvs_get_blob(nvs_handle, "settings", &settings, &size);
    nvs_close(nvs_handle);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        LOG_D("Settings not found");
        return false;
    }
    else if (err != ESP_OK)
    {
        LOG_E("Error nvs_get_blob: %d", err);
        return false;
    }
    LOG_D("Loaded settings");
    return true;
}


/* Schedule evaluation helpers */

bool cmpTempSetTemp(float temp, float setTemp)
{
    xSemaphoreTake(heaterStateMutex, portMAX_DELAY);
    bool heaterStateCopy = heaterState;
    xSemaphoreGive(heaterStateMutex);
    if (heaterStateCopy)
    {
        return temp < setTemp + tempThreshold;
    }
    return temp <= setTemp - tempThreshold;
}

bool scheduleIsActive(JsonObjectConst schedule)
{
    const char *repeat = schedule["repeat"];
    
    if (strcmp(repeat, "Once") == 0)
    {
        LOG_D("Once");
        tm starttm, endtm;
        starttm.tm_sec = 0;
        starttm.tm_min = schedule["sM"];
        starttm.tm_hour = schedule["sH"];
        starttm.tm_mday = schedule["sD"];
        starttm.tm_mon = schedule["sMth"];
        starttm.tm_year = schedule["sY"].as<int>() - 1900;
        starttm.tm_isdst = -1;
        endtm.tm_sec = 0;
        endtm.tm_min = schedule["eM"];
        endtm.tm_hour = schedule["eH"];
        endtm.tm_mday = schedule["eD"];
        endtm.tm_mon = schedule["eMth"];
        endtm.tm_year = schedule["eY"].as<int>() - 1900;
        endtm.tm_isdst = -1;
        time_t startTime = mktime(&starttm);
        time_t endTime = mktime(&endtm);
        time_t now;
        time(&now);
        bool active = startTime <= now && now < endTime;
        LOG_D("%s", active ? "Active" : "Not active");
        return active;
    }

    int startTime = schedule["sH"].as<int>() * 60 + schedule["sM"].as<int>();
    int endTime = schedule["eH"].as<int>() * 60 + schedule["eM"].as<int>();
    time_t now;
    tm tmnow;
    time(&now);
    localtime_r(&now, &tmnow);
    int currentTime = tmnow.tm_hour * 60 + tmnow.tm_min;

    if (strcmp(repeat, "Daily") == 0)
    {
        LOG_D("Daily");
        bool active = startTime <= currentTime && currentTime < endTime;
        LOG_D("%s", active ? "Active" : "Not active");
        return active;
    }

    if (strcmp(repeat, "Weekly") == 0)
    {
        LOG_D("Weekly");
        JsonArrayConst weekdays = schedule["weekDays"]; // Sunday is day 1
        for (int wday : weekdays)
            if (wday == tmnow.tm_wday + 1)
            {
                LOG_T("Active on this weekday");
                bool active = startTime <= currentTime && currentTime < endTime;
                LOG_D("%s", active ? "Active" : "Not active");
                return active;
            }
        LOG_T("Not active on this weekday");
        LOG_D("Not active");
        return false;
    }
    LOG_D("Schedule repeat is invalid");
    return false;
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


/* Event handlers */

void wifi_event_handler(void *, esp_event_base_t base, int32_t id, void *)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START)
    {
        LOG_D("Wifi started");
        esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK)
        {
            LOG_D("esp_wifi_connect error: %s", esp_err_to_name(err));
            xSemaphoreTake(wifiWorkingMutex, portMAX_DELAY);
            wifiWorking = false;
            xSemaphoreGive(wifiWorkingMutex);
        }
    }
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
    {
        xSemaphoreTake(wifiWorkingMutex, portMAX_DELAY);
        wifiWorking = false;
        xSemaphoreGive(wifiWorkingMutex);
        esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK)
        {
            LOG_D("esp_wifi_connect error: %s", esp_err_to_name(err));
        }
    }
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
    {
        LOG_D("Got IP");
        xSemaphoreTake(wifiWorkingMutex, portMAX_DELAY);
        wifiWorking = true;
        xSemaphoreGive(wifiWorkingMutex);
    }
}