/**
 * ESP32-P4 Cockpit — Phase 4: SignalK-bound switch panel.
 *
 * Switch widgets show live N2K switch states from SignalK and toggling
 * them sends PUT requests back to control the actual switches.
 */

#include <Arduino.h>

#include "sensesp_cockpit_display.h"
#include "sensesp_cockpit_display/hal/boards/waveshare_7b.h"
#include "sensesp_n2k_gateway.h"
#include "sensesp_app_builder.h"

using namespace sensesp;
using namespace sensesp_cockpit_display;

// Keep bindings alive for the lifetime of the app
static std::vector<std::unique_ptr<SKSwitchBinding>> bindings;

/// Helper: add a switch widget and bind it to a SignalK path.
static void add_bound_switch(SwitchPage* page, const char* label,
                             const char* sk_path) {
  auto* widget = page->add_switch(label);
  bindings.push_back(
      std::make_unique<SKSwitchBinding>(String(sk_path), widget));
}

void setup() {
  SetupLogging(ESP_LOG_INFO);

  ESP_LOGI("main", "=== Cockpit Phase 4: SignalK Switches ===");

  // Display + touch + LVGL
  auto* display = new Waveshare7BDisplay();
  auto* touch = new Waveshare7BTouch();
  auto* ui = new CockpitUI(display, touch);
  ui->init();

  // SensESP — must be initialized before creating SK bindings
  SensESPAppBuilder builder;
  auto app = builder.set_hostname("p4-cockpit")
                 ->set_wifi_client("MOIN", "Moin2018!")
                 ->set_sk_server("192.168.0.148", 3000)
                 ->enable_ota("cockpit-ota")
                 ->get_app();

  // Add switches bound to actual N2K switch bank paths.
  // Format: "electrical.switches.bank.{bank_id}.{channel}.state"
  auto* sw_page = ui->get_switch_page();

  // Bank 6 (YDEN02.213)
  add_bound_switch(sw_page, "Bank 6 Ch1", "electrical.switches.bank.6.1.state");
  add_bound_switch(sw_page, "Bank 6 Ch2", "electrical.switches.bank.6.2.state");
  add_bound_switch(sw_page, "Bank 6 Ch3", "electrical.switches.bank.6.3.state");
  add_bound_switch(sw_page, "Bank 6 Ch4", "electrical.switches.bank.6.4.state");

  // Bank 7 (YDEN02.171)
  add_bound_switch(sw_page, "Bank 7 Ch1", "electrical.switches.bank.7.1.state");
  add_bound_switch(sw_page, "Bank 7 Ch2", "electrical.switches.bank.7.2.state");
  add_bound_switch(sw_page, "Bank 7 Ch3", "electrical.switches.bank.7.3.state");
  add_bound_switch(sw_page, "Bank 7 Ch4", "electrical.switches.bank.7.4.state");

  // Bank 12 (YDEN02.19)
  add_bound_switch(sw_page, "Bank 12 Ch1", "electrical.switches.bank.12.1.state");
  add_bound_switch(sw_page, "Bank 12 Ch2", "electrical.switches.bank.12.2.state");
  add_bound_switch(sw_page, "Bank 12 Ch3", "electrical.switches.bank.12.3.state");
  add_bound_switch(sw_page, "Bank 12 Ch4", "electrical.switches.bank.12.4.state");

  // N2K gateway
  auto* receiver =
      new TwaiReceiver(TwaiReceiverConfig::waveshare_touch_lcd_7b());
  auto* transmitter = new TwaiTransmitter();
  auto* n2k_server = new CandumpTcpServer(receiver, transmitter);
  receiver->start();
  transmitter->start();
  n2k_server->start();

  // LVGL at ~30fps
  event_loop()->onRepeat(33, [ui]() { ui->tick(); });

  // Heartbeat
  event_loop()->onRepeat(5000, [receiver, n2k_server]() {
    ESP_LOGI("main", "n2k: rx=%u clients=%u | bindings=%u | heap=%lu",
             (unsigned)receiver->rx_count(),
             (unsigned)n2k_server->connected_clients(),
             (unsigned)bindings.size(),
             (unsigned long)esp_get_free_heap_size());
  });
}

void loop() { event_loop()->tick(); }
