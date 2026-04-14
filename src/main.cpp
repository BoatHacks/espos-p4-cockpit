/**
 * ESP32-P4 Cockpit — Phase 6: Switches + Instruments + Status page.
 *
 * Native LVGL UI with tabbed pages:
 *   - Switches: live N2K switch states with toggle control (PUT)
 *   - Instruments: live gauges bound to SignalK paths
 *   - Status: WiFi / SK WebSocket / N2K / BLE health indicators
 */

#include <Arduino.h>
#include <WiFi.h>

#include "sensesp_cockpit_display.h"
#include "sensesp_cockpit_display/hal/boards/waveshare_7b.h"
#include "sensesp_n2k_gateway.h"
#include "sensesp_ble_gateway/ble_signalk_gateway.h"
#include "sensesp_ble_gateway/esp_hosted_bluedroid_ble.h"
#include "sensesp_app_builder.h"
#include "sensesp/signalk/signalk_ws_client.h"
#include "sensesp/system/lambda_consumer.h"

using namespace sensesp;
using namespace sensesp_cockpit_display;

static std::vector<std::unique_ptr<SKSwitchBinding>> sw_bindings;
static std::vector<std::unique_ptr<SKGaugeBinding>> gauge_bindings;
static std::shared_ptr<sensesp::EspHostedBluedroidBLE> g_ble;
static std::shared_ptr<sensesp::BLESignalKGateway> g_ble_gw;

static void add_switch(SwitchPage* page, const char* label,
                       const char* sk_path) {
  auto* w = page->add_switch(label);
  sw_bindings.push_back(
      std::make_unique<SKSwitchBinding>(String(sk_path), w));
}

static void add_gauge(InstrumentPage* page, const char* label,
                      const char* unit, const char* sk_path,
                      std::function<float(float)> conv = nullptr,
                      int decimals = 1) {
  auto* w = page->add_gauge(label, unit);
  gauge_bindings.push_back(
      std::make_unique<SKGaugeBinding>(String(sk_path), w, conv, decimals));
}

void setup() {
  SetupLogging(ESP_LOG_INFO);

  auto* display = new Waveshare7BDisplay();
  auto* touch = new Waveshare7BTouch();
  auto* ui = new CockpitUI(display, touch);
  ui->init();

  SensESPAppBuilder builder;
  auto app = builder.set_hostname("p4-cockpit")
                 ->set_wifi_client("MOIN", "Moin2018!")
                 ->set_sk_server("192.168.0.148", 3000)
                 ->enable_ota("cockpit-ota")
                 ->get_app();

  // --- Switches ---
  auto* sw = ui->get_switch_page();
  add_switch(sw, "Bank 6 Ch1", "electrical.switches.bank.6.1.state");
  add_switch(sw, "Bank 6 Ch2", "electrical.switches.bank.6.2.state");
  add_switch(sw, "Bank 6 Ch3", "electrical.switches.bank.6.3.state");
  add_switch(sw, "Bank 6 Ch4", "electrical.switches.bank.6.4.state");
  add_switch(sw, "Bank 7 Ch1", "electrical.switches.bank.7.1.state");
  add_switch(sw, "Bank 7 Ch2", "electrical.switches.bank.7.2.state");
  add_switch(sw, "Bank 7 Ch3", "electrical.switches.bank.7.3.state");
  add_switch(sw, "Bank 7 Ch4", "electrical.switches.bank.7.4.state");
  add_switch(sw, "Bank 12 Ch1", "electrical.switches.bank.12.1.state");
  add_switch(sw, "Bank 12 Ch2", "electrical.switches.bank.12.2.state");
  add_switch(sw, "Bank 12 Ch3", "electrical.switches.bank.12.3.state");
  add_switch(sw, "Bank 12 Ch4", "electrical.switches.bank.12.4.state");

  // --- Instruments ---
  auto* inst = ui->get_instrument_page();
  add_gauge(inst, "COG", "\xC2\xB0", "navigation.courseOverGroundTrue",
            rad_to_deg, 0);
  add_gauge(inst, "SOG", "kn", "navigation.speedOverGround", ms_to_knots, 1);
  add_gauge(inst, "HDG", "\xC2\xB0", "navigation.headingTrue",
            rad_to_deg, 0);
  add_gauge(inst, "STW", "kn", "navigation.speedThroughWater",
            ms_to_knots, 1);
  add_gauge(inst, "DEPTH", "m", "environment.depth.surfaceToTransducer",
            nullptr, 1);
  add_gauge(inst, "WIND SPD", "kn", "environment.wind.speedApparent",
            ms_to_knots, 1);
  add_gauge(inst, "WIND ANG", "\xC2\xB0", "environment.wind.angleApparent",
            rad_to_deg, 0);
  add_gauge(inst, "PRESSURE", "hPa", "environment.outside.pressure",
            pa_to_hpa, 0);
  add_gauge(inst, "AIR TEMP", "\xC2\xB0""C", "environment.outside.temperature",
            kelvin_to_c, 1);
  add_gauge(inst, "WATER TEMP", "\xC2\xB0""C",
            "environment.inside.temperature", kelvin_to_c, 1);
  add_gauge(inst, "HUMIDITY", "%", "environment.inside.relativeHumidity",
            ratio_to_pct, 0);
  add_gauge(inst, "ROT", "\xC2\xB0/m", "navigation.rateOfTurn",
            rad_to_deg, 2);

  // --- Status page wiring ---
  auto* status = ui->get_status_page();

  // SK WebSocket connection state → status indicator
  auto ws_client = app->get_ws_client();
  ws_client->connect_to(new LambdaConsumer<SKWSConnectionState>(
      [status](SKWSConnectionState state) {
        switch (state) {
          case SKWSConnectionState::kSKWSConnected:
            status->sk_ws()->set_status(StatusLevel::kOk, "connected");
            break;
          case SKWSConnectionState::kSKWSConnecting:
            status->sk_ws()->set_status(StatusLevel::kWarning, "connecting");
            break;
          case SKWSConnectionState::kSKWSAuthorizing:
            status->sk_ws()->set_status(StatusLevel::kWarning, "authorizing");
            break;
          case SKWSConnectionState::kSKWSDisconnected:
          default:
            status->sk_ws()->set_status(StatusLevel::kError, "disconnected");
            break;
        }
      }));

  // --- BLE gateway (Bluedroid via C6 over esp_hosted) ---
  g_ble = std::make_shared<sensesp::EspHostedBluedroidBLE>();
  g_ble_gw = std::make_shared<sensesp::BLESignalKGateway>(
      g_ble, app->get_ws_client());
  g_ble_gw->start();

  // --- N2K gateway ---
  auto* receiver =
      new TwaiReceiver(TwaiReceiverConfig::waveshare_touch_lcd_7b());
  auto* transmitter = new TwaiTransmitter();
  auto* n2k_server = new CandumpTcpServer(receiver, transmitter);
  receiver->start();
  transmitter->start();
  n2k_server->start();

  // LVGL at ~30fps
  event_loop()->onRepeat(33, [ui]() { ui->tick(); });

  // Update status page every 1s
  event_loop()->onRepeat(1000, [ui, receiver, n2k_server]() {
    auto* status = ui->get_status_page();

    // WiFi status
    if (WiFi.status() == WL_CONNECTED) {
      char buf[32];
      snprintf(buf, sizeof(buf), "%s (%d dBm)",
               WiFi.SSID().c_str(), WiFi.RSSI());
      status->wifi()->set_status(StatusLevel::kOk, buf);
    } else {
      status->wifi()->set_status(StatusLevel::kError, "disconnected");
    }

    // N2K status
    uint32_t rx = receiver->rx_count();
    static uint32_t last_rx = 0;
    if (rx > last_rx) {
      char buf[32];
      snprintf(buf, sizeof(buf), "%lu frames, %u clients",
               (unsigned long)rx, (unsigned)n2k_server->connected_clients());
      status->n2k()->set_status(StatusLevel::kOk, buf);
    } else if (rx == 0) {
      status->n2k()->set_status(StatusLevel::kWarning, "no frames yet");
    }
    last_rx = rx;

    // BLE status
    if (g_ble && g_ble->is_scanning()) {
      char buf[48];
      snprintf(buf, sizeof(buf), "scanning, %u hits / %u posted",
               (unsigned)g_ble->scan_hit_count(),
               (unsigned)(g_ble_gw ? g_ble_gw->advertisements_posted() : 0));
      status->ble()->set_status(StatusLevel::kOk, buf);
    } else if (g_ble) {
      status->ble()->set_status(StatusLevel::kWarning, "not scanning");
    } else {
      status->ble()->set_status(StatusLevel::kError, "no controller");
    }

    // Info line
    status->update_info(millis() / 1000, esp_get_free_heap_size(),
                        rx, n2k_server->connected_clients());
  });

  // Heartbeat log
  event_loop()->onRepeat(5000, [receiver]() {
    ESP_LOGI("main", "n2k: rx=%u | sw=%u gauges=%u | heap=%lu",
             (unsigned)receiver->rx_count(),
             (unsigned)sw_bindings.size(),
             (unsigned)gauge_bindings.size(),
             (unsigned long)esp_get_free_heap_size());
  });
}

void loop() { event_loop()->tick(); }
