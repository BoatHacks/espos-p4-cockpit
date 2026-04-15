/**
 * ESP32-P4 Cockpit — cockpit display with switches loaded from
 * Maretron N2KView config import.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <cstdio>
#include <map>
#include <string>

#include "sensesp_cockpit_display.h"
#include "sensesp_cockpit_display/hal/boards/waveshare_7b.h"
#include "sensesp_n2k_gateway.h"
#include "sensesp_ble_gateway/ble_signalk_gateway.h"
#include "sensesp_ble_gateway/esp_hosted_bluedroid_ble.h"
#include "sensesp_app_builder.h"
#include "sensesp/signalk/signalk_ws_client.h"
#include "sensesp/system/lambda_consumer.h"

#include "maretron_layout.h"

using namespace sensesp;
using namespace sensesp_cockpit_display;

static std::vector<std::unique_ptr<SKSwitchBinding>> sw_bindings;
static std::vector<std::unique_ptr<SKButtonBinding>> btn_bindings;
static std::vector<std::unique_ptr<SKGaugeBinding>> gauge_bindings;
static std::shared_ptr<sensesp::EspHostedBluedroidBLE> g_ble;
static std::shared_ptr<sensesp::BLESignalKGateway> g_ble_gw;

static void add_switch_from_config(SwitchPage* page,
                                   const cockpit_config::Switch& def) {
  char path[80];
  snprintf(path, sizeof(path),
           "electrical.switches.bank.%u.%u.state",
           (unsigned)def.bank, (unsigned)def.channel);
  auto* w = page->add_switch(def.label);
  sw_bindings.push_back(std::make_unique<SKSwitchBinding>(String(path), w));
}

static void add_button_from_config(ButtonSwitchPage* page,
                                   const cockpit_config::Switch& def) {
  char path[80];
  snprintf(path, sizeof(path),
           "electrical.switches.bank.%u.%u.state",
           (unsigned)def.bank, (unsigned)def.channel);
  auto* b = page->add_switch(def.label);
  btn_bindings.push_back(std::make_unique<SKButtonBinding>(String(path), b));
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

  // Stream ESP_LOGx over TCP — connect with: nc p4-cockpit.local 2323
  remote_log_start(2323);

  // --- Switches: all 53 using lightweight SwitchButton widgets ---
  // Grouped by Maretron screen into short-named tabs.
  std::map<std::string, std::string> tab_names = {
      {"Switches",    "SW1"},
      {"Switches 2",  "SW2"},
      {"Watermaker",  "WM"},
      {"Bilge Pumps", "Bilge"},
  };
  std::map<std::string, ButtonSwitchPage*> pages;
  for (const auto& s : cockpit_config::get_switches()) {
    std::string screen = s.screen ? s.screen : "Switches";
    auto it = pages.find(screen);
    ButtonSwitchPage* page;
    if (it == pages.end()) {
      auto name_it = tab_names.find(screen);
      const char* tab = name_it != tab_names.end()
                           ? name_it->second.c_str()
                           : screen.c_str();
      page = ui->add_button_switch_page(tab);
      pages[screen] = page;
    } else {
      page = it->second;
    }
    add_button_from_config(page, s);
  }
  ESP_LOGI("main", "Loaded %u switches across %u pages",
           (unsigned)btn_bindings.size(),
           (unsigned)pages.size());
  ui->finalize();

  // --- Status page: only basic WS state binding ---
  auto* status = ui->get_status_page();
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

  // --- BLE gateway ---
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

  // Status page update every 1s
  event_loop()->onRepeat(1000, [ui, receiver, n2k_server]() {
    auto* status = ui->get_status_page();

    if (WiFi.status() == WL_CONNECTED) {
      char buf[32];
      snprintf(buf, sizeof(buf), "%s (%d dBm)",
               WiFi.SSID().c_str(), WiFi.RSSI());
      status->wifi()->set_status(StatusLevel::kOk, buf);
    } else {
      status->wifi()->set_status(StatusLevel::kError, "disconnected");
    }

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

    status->update_info(millis() / 1000, esp_get_free_heap_size(),
                        rx, n2k_server->connected_clients());
  });

  // Heartbeat log
  event_loop()->onRepeat(5000, [receiver]() {
    ESP_LOGI("main", "n2k: rx=%u | sw=%u btn=%u gauges=%u | heap=%lu",
             (unsigned)receiver->rx_count(),
             (unsigned)sw_bindings.size(),
             (unsigned)btn_bindings.size(),
             (unsigned)gauge_bindings.size(),
             (unsigned long)esp_get_free_heap_size());
  });
}

void loop() { event_loop()->tick(); }
