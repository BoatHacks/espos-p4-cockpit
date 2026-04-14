/**
 * ESP32-P4 Cockpit — Phase 3: Switch page with grid layout.
 *
 * Native LVGL UI with a tabbed interface. First tab shows a grid of
 * toggle switches. Touch is handled locally — instant response.
 */

#include <Arduino.h>

#include "sensesp_cockpit_display.h"
#include "sensesp_cockpit_display/hal/boards/waveshare_7b.h"
#include "sensesp_n2k_gateway.h"
#include "sensesp_app_builder.h"

using namespace sensesp;
using namespace sensesp_cockpit_display;

void setup() {
  SetupLogging(ESP_LOG_INFO);

  ESP_LOGI("main", "=== Cockpit Phase 3: Switch Page ===");

  // Initialize display + touch + LVGL + UI
  auto* display = new Waveshare7BDisplay();
  auto* touch = new Waveshare7BTouch();
  auto* ui = new CockpitUI(display, touch);
  ui->init();

  // Add switches — using placeholder names for now.
  // Phase 4 will bind these to real SignalK paths.
  auto* sw_page = ui->get_switch_page();
  sw_page->add_switch("Nav Lights");
  sw_page->add_switch("Anchor Light");
  sw_page->add_switch("Deck Light");
  sw_page->add_switch("Cabin Light");
  sw_page->add_switch("Bilge Pump");
  sw_page->add_switch("Water Pump");
  sw_page->add_switch("Fridge");
  sw_page->add_switch("Charger");
  sw_page->add_switch("Instruments");
  sw_page->add_switch("Horn");
  sw_page->add_switch("Windlass");
  sw_page->add_switch("Autopilot");

  // SensESP with WiFi
  SensESPAppBuilder builder;
  auto app = builder.set_hostname("p4-cockpit")
                 ->set_wifi_client("MOIN", "Moin2018!")
                 ->set_sk_server("192.168.0.148", 3000)
                 ->enable_ota("cockpit-ota")
                 ->get_app();

  // N2K gateway
  auto* receiver =
      new TwaiReceiver(TwaiReceiverConfig::waveshare_touch_lcd_7b());
  auto* transmitter = new TwaiTransmitter();
  auto* n2k_server = new CandumpTcpServer(receiver, transmitter);
  receiver->start();
  transmitter->start();
  n2k_server->start();

  // LVGL runs at ~30fps from the main event loop
  event_loop()->onRepeat(33, [ui]() { ui->tick(); });

  // Heartbeat
  event_loop()->onRepeat(5000, [receiver, n2k_server]() {
    ESP_LOGI("main", "n2k: rx=%u clients=%u | heap=%lu",
             (unsigned)receiver->rx_count(),
             (unsigned)n2k_server->connected_clients(),
             (unsigned long)esp_get_free_heap_size());
  });
}

void loop() { event_loop()->tick(); }
