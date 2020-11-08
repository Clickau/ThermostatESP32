#ifndef LOGGER_H
#define LOGGER_H

#define LOGGER_LEVEL_DISABLED 0
#define LOGGER_LEVEL_ERROR 1
#define LOGGER_LEVEL_WARN 2
#define LOGGER_LEVEL_DEBUG 3
#define LOGGER_LEVEL_TRACE 4

inline SemaphoreHandle_t loggerMutex;

#define INTERNAL_LOG_TIMESTAMP()                  \
    Serial.print(millis());                       \
    Serial.print(' ');

#define INTERNAL_LOG_SEVERITY(severity)           \
    Serial.print(severity);                       \
    Serial.print('/');

#define INTERNAL_LOG_CORE()                       \
    Serial.print(xPortGetCoreID());               \
    Serial.print('/');

#define INTERNAL_LOG_FUNCTION()                   \
    Serial.print(__func__);                       \
    Serial.print(": ");

#define INTERNAL_LOG_MESSAGE(format, ...)         \
    Serial.printf(format, ##__VA_ARGS__);         \
    Serial.println();

#define INTERNAL_LOG_INIT()                       \
    loggerMutex = xSemaphoreCreateMutex();        \
    Serial.begin(115200);

#define INTERNAL_LOG_E(format, ...)               \
    xSemaphoreTake(loggerMutex, portMAX_DELAY);   \
    INTERNAL_LOG_TIMESTAMP()                      \
    INTERNAL_LOG_SEVERITY("ERR")                  \
    INTERNAL_LOG_CORE()                           \
    INTERNAL_LOG_FUNCTION()                       \
    INTERNAL_LOG_MESSAGE(format, ##__VA_ARGS__)   \
    xSemaphoreGive(loggerMutex)

#define INTERNAL_LOG_W(format, ...)               \
    xSemaphoreTake(loggerMutex, portMAX_DELAY);   \
    INTERNAL_LOG_TIMESTAMP()                      \
    INTERNAL_LOG_SEVERITY("WRN")                  \
    INTERNAL_LOG_CORE()                           \
    INTERNAL_LOG_FUNCTION()                       \
    INTERNAL_LOG_MESSAGE(format, ##__VA_ARGS__)   \
    xSemaphoreGive(loggerMutex)

#define INTERNAL_LOG_D(format, ...)               \
    xSemaphoreTake(loggerMutex, portMAX_DELAY);   \
    INTERNAL_LOG_TIMESTAMP()                      \
    INTERNAL_LOG_SEVERITY("DBG")                  \
    INTERNAL_LOG_CORE()                           \
    INTERNAL_LOG_FUNCTION()                       \
    INTERNAL_LOG_MESSAGE(format, ##__VA_ARGS__)   \
    xSemaphoreGive(loggerMutex)

#define INTERNAL_LOG_T(format, ...)               \
    xSemaphoreTake(loggerMutex, portMAX_DELAY);   \
    INTERNAL_LOG_TIMESTAMP()                      \
    INTERNAL_LOG_SEVERITY("TRC")                  \
    INTERNAL_LOG_CORE()                           \
    INTERNAL_LOG_FUNCTION()                       \
    INTERNAL_LOG_MESSAGE(format, ##__VA_ARGS__)   \
    xSemaphoreGive(loggerMutex)

#if LOGGER_SELECTED_LEVEL == LOGGER_LEVEL_DISABLED

#warning "Logger is disabled"

#define LOG_INIT()
#define LOG_E(format, ...)
#define LOG_W(format, ...)
#define LOG_D(format, ...)
#define LOG_T(format, ...)

#elif LOGGER_SELECTED_LEVEL == LOGGER_LEVEL_ERROR

#define LOG_INIT() INTERNAL_LOG_INIT()
#define LOG_E(format, ...) INTERNAL_LOG_E(format, ##__VA_ARGS__)
#define LOG_W(format, ...)
#define LOG_D(format, ...)
#define LOG_T(format, ...)

#elif LOGGER_SELECTED_LEVEL == LOGGER_LEVEL_WARN

#define LOG_INIT() INTERNAL_LOG_INIT()
#define LOG_E(format, ...) INTERNAL_LOG_E(format, ##__VA_ARGS__)
#define LOG_W(format, ...) INTERNAL_LOG_W(format, ##__VA_ARGS__)
#define LOG_D(format, ...)
#define LOG_T(format, ...)

#elif LOGGER_SELECTED_LEVEL == LOGGER_LEVEL_DEBUG

#define LOG_INIT() INTERNAL_LOG_INIT()
#define LOG_E(format, ...) INTERNAL_LOG_E(format, ##__VA_ARGS__)
#define LOG_W(format, ...) INTERNAL_LOG_W(format, ##__VA_ARGS__)
#define LOG_D(format, ...) INTERNAL_LOG_D(format, ##__VA_ARGS__)
#define LOG_T(format, ...)

#elif LOGGER_SELECTED_LEVEL == LOGGER_LEVEL_TRACE

#define LOG_INIT() INTERNAL_LOG_INIT()
#define LOG_E(format, ...) INTERNAL_LOG_E(format, ##__VA_ARGS__)
#define LOG_W(format, ...) INTERNAL_LOG_W(format, ##__VA_ARGS__)
#define LOG_D(format, ...) INTERNAL_LOG_D(format, ##__VA_ARGS__)
#define LOG_T(format, ...) INTERNAL_LOG_T(format, ##__VA_ARGS__)

#endif

#endif