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

#ifndef FIREBASECLIENT_H
#define FIREBASECLIENT_H

#include <Arduino.h>
#include <esp_tls.h>

class FirebaseClient
{
public:

    FirebaseClient();

    /* rootCert - TLS certificate of Firebase's authority (Google Trust Services)    
     * url - URL of database (example.firebaseio.com)
     * secret - secret key of database
     * streamingPath - path in database where client should listen for changes
     */
    void begin(const char *rootCert, const char *url, const char *secret, const char *streamingPath);

    /* checks if anything changed in the database
     * if it receives updates on the stream, it checks if the event is of type 'put', and if so, it means something in the database changed, so it clears the stream and returns true
     * otherwise, it returns false
     */
    bool consumeStreamIfAvailable();

    // initialize a stream that will receive a notification when something changes in the database
    void initializeStream();

    // closes the current streaming connection
    void closeStream();

    // it is public because we might want to set the error if wifi isn't working
    void setError(bool value);

    bool getError();

    /* gets the string that contains the json representation of the object
     * path is the part after the url including .json
     * for example if we want to get example.firebaseio.com/Schedules.json, streamingPath should be "Schedules.json"
     * the string obtained from the request is stored in result; if the request fails, result is not modified
     */
    void getJson(const char *path, String &result);

    void setJson(const char *path, const char *data);

    void pushJson(const char *path, const char *data);

    ~FirebaseClient();

private:
    bool internal_initializeStream(const char *pathWithQuery, const char *location, bool locationIsURL);
    void sendRequest(int method, const char *path, const char *data, void *responseReceiver);

    bool error;
    const char *rootCA;
    const char *firebaseURL;
    char query[50];

    bool streamConnected;
    char *streamingHost;
    char *streamingPathWithQuery;
    esp_tls_t *streaming_tls;
    char streaming_buf[512];
    TickType_t lastEvent;
    bool afterFirstEvent;
    bool insideEvent;

    SemaphoreHandle_t errorMutex;
};

#endif