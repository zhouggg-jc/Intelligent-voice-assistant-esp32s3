idf_component_register(SRCS "http.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_http_client mbedtls esp-tls json driver)
