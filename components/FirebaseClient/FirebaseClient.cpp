#include <Stream.h>
#include <StreamString.h>
#include <esp_http_client.h>
#include "FirebaseClient.h"
#include "Logger.h"

static esp_err_t http_event_handler(esp_http_client_event_handle_t event)
{
    if (event->event_id == HTTP_EVENT_ON_DATA)
    {
        if (event->user_data)
        {
            auto sstring = static_cast<StreamString *>(event->user_data);
            sstring->write(static_cast<const uint8_t *>(event->data), event->data_len);
        }
    }
    return ESP_OK;
}

FirebaseClient::FirebaseClient(const char *_rootCA, const char *_firebaseURL, const char *_secret, const char *_streamingPath):
    error(false),
    rootCA(_rootCA),
    firebaseURL(_firebaseURL),
    streamConnected(false),
    streamingHost(nullptr),
    streamingPathWithQuery(nullptr)
{
    errorMutex = xSemaphoreCreateMutex();
    snprintf(query, sizeof(query), "auth=%s", _secret);
    int ret = asprintf(&streamingPathWithQuery, "%s?%s", _streamingPath, query);
    if (ret == -1)
    {
        LOG_D("Could not allocate streamingPathWithQuery");
        abort();
    }
}

FirebaseClient::~FirebaseClient()
{
    free(streamingPathWithQuery);
    free(streamingHost);
}

bool FirebaseClient::internal_initializeStream(const char *pathWithQuery, const char *location, bool locationIsURL)
{
    esp_tls_cfg_t cfg = {};
    cfg.cacert_pem_buf = (const unsigned char *) rootCA;
    cfg.cacert_pem_bytes = strlen(rootCA) + 1;
    cfg.non_block = true;
    streaming_tls = esp_tls_init();
    if (!streaming_tls)
    {
        LOG_E("esp_tls_init error");
        return false;
    }
    LOG_T("Connecting");
    if (!location)
    {
        // we use the last used host
        if (!streamingHost)
        {
            LOG_D("First initialization must have host");
            return false;
        }
        location = streamingHost;
        locationIsURL = false;
    }
    else
    {
        free(streamingHost);
        streamingHost = strdup(location);
    }
    int ret;
    while ((ret = locationIsURL ?
        esp_tls_conn_http_new_async(location, &cfg, streaming_tls) 
        : esp_tls_conn_new_async(location, strlen(location), 443, &cfg, streaming_tls)) == 0)
    {
        delay(50);
    }
    if (ret != 1)
    {
        LOG_D("Connection failed");
        return false;
    }
    LOG_T("Connection established");
    char *request;
    size_t hostLength;
    const char *host = location;
    if (locationIsURL)
    {
        // skip https://
        host += 8;
        const char *endHost = strchr(host, '/');
        if (!endHost)
        {   
            LOG_D("Could not find end of host in URL");
            return false;
        }
        hostLength = endHost - host;
    }
    else
    {
        hostLength = strlen(location);
    }
    
    ret = asprintf(&request, "GET %s HTTP/1.1\r\nHost: %.*s\r\nUser-Agent: ThermostatESP32\r\nAccept: text/event-stream\r\n\r\n", pathWithQuery, hostLength, host);
    if (ret == -1)
    {
        LOG_D("Could not allocate request");
        return false;
    }
    LOG_T("request: %s", request);
    LOG_T("Sending request");
    size_t written_bytes = 0;
    do
    {
        ret = esp_tls_conn_write(streaming_tls, request + written_bytes, strlen(request) - written_bytes);
        if (ret >= 0)
        {
            written_bytes += ret;
        }
        else if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
        {
            LOG_D("esp_tls_conn_write error: -%X", -ret);
            free(request);
            return false;
        }
    } while (written_bytes < strlen(request));
    free(request);
    return true;
}

void FirebaseClient::initializeStream()
{
    LOG_T("Initializing stream");
    if (streamConnected)
        closeStream();
    bool success = internal_initializeStream(streamingPathWithQuery, firebaseURL, false);
    if (!success)
    {
        if (streaming_tls)
            esp_tls_conn_delete(streaming_tls);
        setError(true);
        return;
    }
    LOG_D("Streaming started");
    streamConnected = true;
    setError(false);
    lastEvent = 0;
    afterFirstEvent = false;
    insideEvent = false;
}

bool FirebaseClient::consumeStreamIfAvailable()
{
    // declared here to avoid "jump to label 'error' crosses initialization" error
    int ret;
    const char *event;
    const char *end_event;

    // Firebase sends a keep-alive event every 30 seconds
    // if we do not receive any event for 45 seconds, the connection is broken
    if (afterFirstEvent && xTaskGetTickCount() - lastEvent > pdMS_TO_TICKS(45000))
    {
        LOG_D("Connection lost");
        goto error;
    }

    ret = esp_tls_conn_read(streaming_tls, streaming_buf, sizeof(streaming_buf) - 1);
    if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
        return false;
    if (ret < 0)
    {
        LOG_D("esp_tls_conn_read error: -%X", -ret);
        goto error;
    }
    if (ret == 0)
    {
        LOG_D("Connection closed");
        goto error;
    }

    lastEvent = xTaskGetTickCount();
    afterFirstEvent = true;

    // make sure the string is null-terminated
    streaming_buf[ret] = 0;

    // when there are a lot of schedules, the event doesn't fit into streaming_buf and gets split up
    // we only want to process it once, so if we are inside an event we ignore it
    if (insideEvent)
    {
        end_event = strstr(streaming_buf, "\n\n");
        // if we find two newlines or the message consists of only one endline,
        // we are at the last part of the current event
        if (end_event || (ret == 1 && streaming_buf[0] == '\n'))
        {
            insideEvent = false;
        }
        return false;
    }

    event = streaming_buf;
    if (strncmp(streaming_buf, "HTTP/1.", 7) == 0)
    {
        // the buffer contains the response headers
        int code = atoi(streaming_buf + 9);
        if (code / 100 == 3)
        {
            LOG_D("Redirecting");
            const char *location = strstr(streaming_buf, "Location: ");
            if (!location)
            {
                LOG_D("No location header");
                goto error;
            }
            const char *location_end = strchr(location, '\r');
            if (!location_end)
            {
                LOG_D("No newline after location");
                goto error;
            }
            streaming_buf[location_end - streaming_buf] = 0;
            location += 10;
            closeStream();
            bool success;
            if (location[0] == '/')
            {
                // relative url
                success = internal_initializeStream(location, nullptr, false);
            }
            else
            {
                // absolute url
                // we search for first / after https://
                const char *path = strchr(location + 8, '/');
                if (!path)
                {
                    LOG_D("Could not find path");
                    goto error;
                }
                success = internal_initializeStream(path, location, true);
            }
            if (!success)
            {
                LOG_D("Error initializing stream to new location");
                if (streaming_tls)
                {
                    esp_tls_conn_delete(streaming_tls);
                }
                setError(true);
                return false;
            }
            LOG_D("Initialized stream to new location");
            streamConnected = true;
            setError(false);
            lastEvent = 0;
            afterFirstEvent = false;
            insideEvent = false;
            return false;
        }
        switch (code)
        {
        case 200:
            break;
        case 400:
            LOG_D("Bad request");
            goto error;
        case 401:
            LOG_D("Unauthorized");
            goto error;
        case 404:
            LOG_D("Not found");
            goto error;
        case 500:
            LOG_D("Internal server error");
            goto error;
        case 503:
            LOG_D("Service Unavailable");
            goto error;
        default:
            LOG_E("Unknown response code");
            goto error;
        }
        event = strstr(streaming_buf, "\r\n\r\nevent: ");
        if (!event)
        {
            LOG_D("Headers did not fit in first message, make buffer larger");
            goto error;
        }
        event += 4;
    }
    // an event contains 3 newlines: one after "event: ...", one after "data: ..." and one at the end
    // if we can't find all of them, the event is split up
    end_event = event;
    for (size_t i = 0; i < 3; i++)
    {
        if (!end_event)
            break;
        end_event = strchr(end_event + 1, '\n');
    }
    if (!end_event)
        insideEvent = true;
    event += 7;
    if (strncmp(event, "keep-alive", 10) == 0)
    {
        return false;
    }
    if (strncmp(event, "put", 3) == 0 || strncmp(event, "patch", 5) == 0)
    {
        LOG_T("Received put event");
        return true;
    }
    if (strncmp(event, "cancel", 6) == 0 || strncmp(event, "auth_revoked", 12) == 0)
    {
        LOG_D("Cancel or auth_revoked");
        goto error;
    }
    LOG_D("Unknown event");
    goto error;

error:
    closeStream();
    setError(true);
    return false;
}

void FirebaseClient::closeStream()
{
    LOG_T("Closing stream");
    streamConnected = false;
    esp_tls_conn_delete(streaming_tls);
}

bool FirebaseClient::getError()
{
    xSemaphoreTake(errorMutex, portMAX_DELAY);
    bool errorCopy = error;
    xSemaphoreGive(errorMutex);
    return errorCopy;
}

void FirebaseClient::setError(bool value)
{
    xSemaphoreTake(errorMutex, portMAX_DELAY);
    error = value;
    xSemaphoreGive(errorMutex);
}

void FirebaseClient::getJson(const char *path, String &result)
{
    StreamString sstring;
    esp_http_client_config_t config = {};
    config.cert_pem = rootCA;
    config.host = firebaseURL;
    config.path = path;
    config.query = query;
    config.transport_type = HTTP_TRANSPORT_OVER_SSL;
    config.user_data = &sstring;
    config.event_handler = http_event_handler;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    LOG_T("Starting connection to retrieve an object");
    // loop because we need to manually handle some redirects
    do {
        esp_err_t err = esp_http_client_perform(client);
        if (err == ESP_OK)
        {
            int code = esp_http_client_get_status_code(client);
            if (code / 100 == 3)
            {
                // we must manually handle redirections with codes 303, 307, 308
                // 301 and 302 are handled automatically
                esp_http_client_set_redirection(client);
                continue;
            }
            if (code == 200)
            {
                LOG_D("The object was retrieved successfully");
                result = sstring.readString();
                setError(false);
            }
            else
            {
                LOG_D("Server returned status code: %d", code);
                setError(true);
            }
        }
        else
        {
            LOG_D("Connection failed with error: %d, %s", err, esp_err_to_name(err));
            setError(true);
        }
        break;
    } while(true);
    esp_http_client_cleanup(client);
}
