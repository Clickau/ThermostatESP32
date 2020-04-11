#include "FirebaseClient.h"

FirebaseClient::FirebaseClient(const char *_rootCA, const char *_firebaseURL, const char *_secret):
    error(false),
    rootCA(_rootCA),
    firebaseURL(_firebaseURL),
    secret(_secret),
    streamConnected(false)
{
    streamingClientSecure.setCACert(rootCA);
    requestClientSecure.setCACert(rootCA);
}

bool FirebaseClient::consumeStreamIfAvailable()
{
    if (!streamingHTTPClient.connected())
    {
        Serial.println("Connection lost");
        closeStream();
        setError(true);
        return false;
    }
    WiFiClient *client = streamingHTTPClient.getStreamPtr();
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
    char eventType[20];
    size_t size = client->readBytesUntil('\n', eventType, 20);
    // we clear the stream
    int c = client->read();
    while (c >= 0 && c != '\n')
        c = client->read();
    c = client->read();
    while (c >= 0 && c != '\n')
        c = client->read();
    // the event might be just a keep-alive, so we check if the type is 'put'
    if (size > 7)
    {
        if (strncmp(eventType + 7, "put", 3) == 0)
        {
            return true;
        }
    }
    return false;
}

void FirebaseClient::initializeStream(const char *streamingPath)
{
    if (streamConnected)
        closeStream();

    // we keep the connection alive after the first request
    streamingHTTPClient.setReuse(true);
    // the esp32 version of httpclient doesn't manage redirects automatically
    //setFollowRedirects(true);
    // we open a secure connection
    
    streamingHTTPClient.begin(streamingClientSecure, makeURL(streamingPath));
    streamingHTTPClient.addHeader("Accept", "text/event-stream");

    //manage redirects manually
    const char* headers[] = {"Location"};
    streamingHTTPClient.collectHeaders(headers, 1);

    int status = streamingHTTPClient.GET();

    //manage redirects manually
    while (status == HTTP_CODE_TEMPORARY_REDIRECT)
    {
        String location = streamingHTTPClient.header("Location");
        streamingHTTPClient.setReuse(false);
        streamingHTTPClient.end();
        streamingHTTPClient.setReuse(true);
        streamingHTTPClient.begin(location);
        status = streamingHTTPClient.GET();
    }

    if (status != HTTP_CODE_OK)
    {
        streamConnected = false;
        setError(true);
        return;
    }

    streamConnected = true;
    setError(false);
}

void FirebaseClient::closeStream()
{
    streamConnected = false;
    streamingHTTPClient.setReuse(false);
    streamingHTTPClient.end();
}

String FirebaseClient::makeURL(const char *path)
{
    String url;
    if (strncmp(firebaseURL, "https://", 8) != 0)
        url.concat("https://");
    url.concat(firebaseURL);
    if (path[0] != '/')
        url.concat('/');
    url.concat(path);
    url.concat(".json?auth=");
    url.concat(secret);
    return url;
}

bool FirebaseClient::getError()
{
    return error;
}

void FirebaseClient::setError(bool value)
{
    error = value;
}

void FirebaseClient::getJson(const char *path, String &result)
{
    int resultCode;
    requestHTTPClient.begin(requestClientSecure, makeURL(path));
    resultCode = requestHTTPClient.GET();
    if (resultCode != HTTP_CODE_OK)
    {
        Serial.println("error getJson");
        Serial.println(resultCode);
        requestHTTPClient.end();
        setError(true);
        return;
    }
    // we store the received string, which contains the json representation of the data
    result = requestHTTPClient.getString();
    Serial.println("success getJson");
    requestHTTPClient.end();
    setError(false);
}