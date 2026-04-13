/**
 * @file main.cpp
 * @brief ESP32-P4 Cockpit: Display streaming + N2K + BLE gateway.
 *
 * Runs all three functions on a single Waveshare ESP32-P4-WIFI6-Touch-LCD-7B:
 *   - MJPEG display streaming from Pi5 (KIP dashboard via Chromium)
 *   - NMEA 2000 candump-over-TCP on port 2599 (canboatjs)
 *   - BLE advertisements forwarded to signalk-server's BLE provider API
 *
 * The 7B board has no Ethernet PHY — all networking goes through the C6
 * companion via esp_hosted WiFi.
 */

#include "sensesp_display_stream.h"
#include "sensesp_n2k_gateway.h"
#include "sensesp_ble_gateway/ble_signalk_gateway.h"
#include "sensesp_ble_gateway/esp_hosted_bluedroid_ble.h"

#include "sensesp_app_builder.h"

using namespace sensesp;
using namespace sensesp_display_stream;

static std::shared_ptr<EspHostedBluedroidBLE> g_ble;
static std::shared_ptr<BLESignalKGateway> g_ble_gateway;

void setup() {
  SetupLogging(ESP_LOG_INFO);

  SensESPAppBuilder builder;
  auto app = builder.set_hostname("p4-cockpit")
                 ->enable_ota("cockpit-ota")
                 ->get_app();

  // --- Display streaming ---
  auto* display = new DisplayOutput();
  display->init();

  auto* decoder = new JpegDecoder(display);
  decoder->init();

  auto* stream =
      new MjpegStreamClient("signalk.local", 5004, decoder);
  stream->start();

  auto* touch = new TouchForwarder("signalk.local", 5005);
  touch->start();

  // --- NMEA 2000 gateway ---
  // The 7B board has a built-in TJA1051T CAN transceiver on GPIO22/GPIO21.
  auto* receiver =
      new TwaiReceiver(TwaiReceiverConfig::waveshare_touch_lcd_7b());
  auto* transmitter = new TwaiTransmitter();
  auto* n2k_server = new CandumpTcpServer(receiver, transmitter);

  receiver->start();
  transmitter->start();
  n2k_server->start();

  // --- BLE gateway ---
  // Uses Bluedroid over esp_hosted SDIO to the C6 companion.
  // WiFi + BLE coexistence on the C6 requires CONFIG_SW_COEXIST_ENABLE=y.
  // If this proves unstable, comment out the BLE section and use a separate
  // ESP32-C5 for BLE scanning.
  g_ble = std::make_shared<EspHostedBluedroidBLE>();
  g_ble_gateway =
      std::make_shared<BLESignalKGateway>(g_ble, app->get_ws_client());
  g_ble_gateway->start();

  // --- Heartbeat ---
  event_loop()->onRepeat(5000, [receiver, n2k_server, stream, decoder]() {
    ESP_LOGI("COCKPIT",
             "display: rx=%u dec=%u err=%u conn=%d | "
             "n2k: rx=%u clients=%u | "
             "ble: hits=%u posted=%u",
             (unsigned)stream->frames_received(),
             (unsigned)decoder->frames_decoded(),
             (unsigned)decoder->decode_errors(),
             (int)stream->is_connected(),
             (unsigned)receiver->rx_count(),
             (unsigned)n2k_server->connected_clients(),
             (unsigned)(g_ble ? g_ble->scan_hit_count() : 0),
             (unsigned)(g_ble_gateway ? g_ble_gateway->advertisements_posted()
                                     : 0));
  });
}

void loop() { event_loop()->tick(); }
