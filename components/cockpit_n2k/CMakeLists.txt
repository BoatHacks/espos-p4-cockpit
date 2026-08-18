# SPDX-License-Identifier: LicenseRef-Source-Available-No-Redistribution
idf_component_register(
    SRCS src/twai_receiver.cpp src/twai_transmitter.cpp src/candump_tcp_server.cpp src/candump_format.cpp
    INCLUDE_DIRS include
    REQUIRES driver esp_timer
    PRIV_REQUIRES lwip espressif__mdns esp_driver_gpio)
