/**
 * ESP32-P4 Cockpit — Phase 5: Instruments + Switches.
 *
 * Native LVGL UI with tabbed pages:
 *   - Switches: live N2K switch states with toggle control
 *   - Instruments: live gauges (COG, SOG, depth, wind, temp, position)
 *   - Status: placeholder
 */

#include <Arduino.h>

#include "sensesp_cockpit_display.h"
#include "sensesp_cockpit_display/hal/boards/waveshare_7b.h"
#include "sensesp_n2k_gateway.h"
#include "sensesp_app_builder.h"

using namespace sensesp;
using namespace sensesp_cockpit_display;

static std::vector<std::unique_ptr<SKSwitchBinding>> sw_bindings;
static std::vector<std::unique_ptr<SKGaugeBinding>> gauge_bindings;

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

  // Display + touch + LVGL
  auto* display = new Waveshare7BDisplay();
  auto* touch = new Waveshare7BTouch();
  auto* ui = new CockpitUI(display, touch);
  ui->init();

  // SensESP — must init before SK bindings
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
  add_gauge(inst, "SOG", "kn", "navigation.speedOverGround",
            ms_to_knots, 1);
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

  // Heartbeat
  event_loop()->onRepeat(5000, [receiver, n2k_server]() {
    ESP_LOGI("main", "n2k: rx=%u | sw=%u gauges=%u | heap=%lu",
             (unsigned)receiver->rx_count(),
             (unsigned)sw_bindings.size(),
             (unsigned)gauge_bindings.size(),
             (unsigned long)esp_get_free_heap_size());
  });
}

void loop() { event_loop()->tick(); }
