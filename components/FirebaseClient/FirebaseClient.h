#ifndef FIREBASECLIENT_H
#define FIREBASECLIENT_H

#include <Arduino.h>
#include <esp_tls.h>

// custom HTTPClient that can handle Firebase Streaming
class FirebaseClient
{
public:

    /* _rootCA - the SSL root certificate of Firebase
     * _url - url to the database, e.g. example.firebaseio.com
     * _secret - one of the secret keys of the database
     * _retryTimes - number of times we try a request if it fails, before we return error
     */
    FirebaseClient(const char *_rootCA, const char *_url, const char *_secret, const char *_streamingPath);

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
    void initializeStream();

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