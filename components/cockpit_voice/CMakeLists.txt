# SPDX-License-Identifier: LicenseRef-Source-Available-No-Redistribution
idf_component_register(
    SRCS src/wyoming_satellite.cpp src/wake_engine.cpp src/protocol/events.cpp src/protocol/framing.cpp
    INCLUDE_DIRS include
    REQUIRES cockpit_hal
    PRIV_REQUIRES lwip bblanchon__arduinojson espressif__esp-sr)
