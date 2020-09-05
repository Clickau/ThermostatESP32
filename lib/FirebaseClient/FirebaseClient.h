#ifndef FIREBASECLIENT_H
#define FIREBASECLIENT_H

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>

// custom HTTPClient that can handle Firebase Streaming
class FirebaseClient
{
public:

    /* _rootCA - the SSL root certificate of Firebase
     * _url - url to the database, e.g. example.firebaseio.com
     * _secret - one of the secret keys of the database
     * _retryTimes - number of times we try a request if it fails, before we return error
     */
    FirebaseClient(const char *_rootCA, const char *_url, const char *_secret);

    /* checks if anything changed in the database
     * if it receives updates on the stream, it checks if the event is of type 'put', and if so, it means something in the database changed, so it clears the stream and returns true
     * otherwise, it returns false
     */
    bool consumeStreamIfAvailable();

    /*
     * initialize a stream that will receive a notification when something changes in the database
     * streamingPath is the part after the url (and without .json) of the path to the json blob that contains the data we want to monitor
     * for example if we want to monitor example.firebaseio.com/Data/Schedules.json, streamingPath should be "Data/Schedules" or "/Data/Schedules"
     */
    void initializeStream(const char *streamingPath);

    // closes the current streaming connection
    void closeStream();

    // it is public because we might want to set the error if wifi isn't working
    void setError(bool value);

    bool getError();

    /*
     * gets the string that contains the json representation of the object
     * path is the part after the url (and without .json) of the path to the json blob that contains the data we want
     * for example if we want to get example.firebaseio.com/Data/Schedules.json, streamingPath should be "Data/Schedules" or "/Data/Schedules"
     * the string obtained from the request is stored in result; if the request fails, result is not modified
     */
    void getJson(const char *path, String &result);

    void setJson(const char *path, const String &data);

    void pushJson(const char *path, const String &data);

private:
    bool error;
    const char *rootCA;
    const char *firebaseURL;
    const char *secret;

    bool streamConnected;
    HTTPClient streamingHTTPClient;
    WiFiClientSecure streamingClientSecure;

    HTTPClient requestHTTPClient;
    WiFiClientSecure requestClientSecure;

    String makeURL(const char *path);

    SemaphoreHandle_t errorMutex;
};

#endif