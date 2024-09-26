set(CMAKE_CXX_STANDARD 20)
idf_component_register(
    SRCS
       main.cpp
       WebServer.cpp
    INCLUDE_DIRS
       "."
    REQUIRES
        driver
        esp_timer
        wifimanager
        nvsstoragemanager
        jsonwrapper
        esp_http_server
        button
)
