idf_component_register(
    SRCS "FirebaseClient.cpp"
    INCLUDE_DIRS "."
    REQUIRES "arduino" "esp-tls"
    PRIV_REQUIRES "Logger" "esp_http_client"
)