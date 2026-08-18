# SPDX-License-Identifier: LicenseRef-Source-Available-No-Redistribution
idf_component_register(
    SRCS src/waveshare_7b.cpp src/ui.cpp
    INCLUDE_DIRS include
    REQUIRES lvgl__lvgl esp_lcd esp_lcd_ek79007
    PRIV_REQUIRES esp_driver_i2c esp_driver_ledc esp_driver_gpio esp_timer)
