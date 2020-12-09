/*
    Copyright 2019-2020 Cosmin Popan

    This file is part of ThermostatESP32

    ThermostatESP32 is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    ThermostatESP32 is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with ThermostatESP32. If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>

#define LOGGER_LEVEL_DISABLED 0
#define LOGGER_LEVEL_ERROR 1
#define LOGGER_LEVEL_WARN 2
#define LOGGER_LEVEL_DEBUG 3
#define LOGGER_LEVEL_TRACE 4

inline SemaphoreHandle_t loggerMutex;

#define INTERNAL_LOG_INIT()                                                                            \
    loggerMutex = xSemaphoreCreateMutex();                                                             \
    Serial.begin(115200);

#define INTERNAL_LOG_E(format, ...)                                                                    \
    xSemaphoreTake(loggerMutex, portMAX_DELAY);                                                        \
    printf("%llu %s/%u/%s: ", esp_timer_get_time() / 1000ULL, "ERR", xPortGetCoreID(), __func__);      \
    printf(format, ##__VA_ARGS__);                                                                     \
    puts("");                                                                                          \
    xSemaphoreGive(loggerMutex)

#define INTERNAL_LOG_W(format, ...)                                                                    \
    xSemaphoreTake(loggerMutex, portMAX_DELAY);                                                        \
    printf("%llu %s/%u/%s: ", esp_timer_get_time() / 1000ULL, "WRN", xPortGetCoreID(), __func__);      \
    printf(format, ##__VA_ARGS__);                                                                     \
    puts("");                                                                                          \
    xSemaphoreGive(loggerMutex)

#define INTERNAL_LOG_D(format, ...)                                                                    \
    xSemaphoreTake(loggerMutex, portMAX_DELAY);                                                        \
    printf("%llu %s/%u/%s: ", esp_timer_get_time() / 1000ULL, "DBG", xPortGetCoreID(), __func__);      \
    printf(format, ##__VA_ARGS__);                                                                     \
    puts("");                                                                                          \
    xSemaphoreGive(loggerMutex)

#define INTERNAL_LOG_T(format, ...)                                                                    \
    xSemaphoreTake(loggerMutex, portMAX_DELAY);                                                        \
    printf("%llu %s/%u/%s: ", esp_timer_get_time() / 1000ULL, "TRC", xPortGetCoreID(), __func__);      \
    printf(format, ##__VA_ARGS__);                                                                     \
    puts("");                                                                                          \
    xSemaphoreGive(loggerMutex)

#if LOGGER_SELECTED_LEVEL == LOGGER_LEVEL_DISABLED

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