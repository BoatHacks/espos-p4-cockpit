/**
 * ESP32-P4 Cockpit — Phase 1: LVGL on screen with touch.
 *
 * Renders a simple LVGL UI on the Waveshare 7B display with touch input.
 * This validates the LVGL + MIPI-DSI + GT911 integration.
 */

#include <Arduino.h>

#include "sensesp_cockpit_display.h"
#include "sensesp_cockpit_display/hal/boards/waveshare_7b.h"
#include "sensesp_n2k_gateway.h"
#include "sensesp_app_builder.h"

using namespace sensesp;
using namespace sensesp_cockpit_display;

static uint32_t touch_count = 0;

static void create_test_ui() {
  // Dark background
  lv_obj_t* scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_make(20, 25, 35), 0);

  // Title
  lv_obj_t* title = lv_label_create(scr);
  lv_label_set_text(title, "Signal K Cockpit");
  lv_obj_set_style_text_color(title, lv_color_make(220, 240, 255), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_36, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

  // Subtitle
  lv_obj_t* sub = lv_label_create(scr);
  lv_label_set_text(sub, "LVGL 9 + MIPI-DSI + GT911 Touch");
  lv_obj_set_style_text_color(sub, lv_color_make(120, 140, 160), 0);
  lv_obj_set_style_text_font(sub, &lv_font_montserrat_20, 0);
  lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 70);

  // Touch test button
  lv_obj_t* btn = lv_btn_create(scr);
  lv_obj_set_size(btn, 300, 80);
  lv_obj_align(btn, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(btn, lv_color_make(40, 200, 80), 0);
  lv_obj_set_style_bg_color(btn, lv_color_make(30, 150, 60),
                            LV_STATE_PRESSED);

  lv_obj_t* btn_label = lv_label_create(btn);
  lv_label_set_text(btn_label, "Tap Me!");
  lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_28, 0);
  lv_obj_center(btn_label);

  // Touch counter label
  lv_obj_t* counter = lv_label_create(scr);
  lv_label_set_text(counter, "Touches: 0");
  lv_obj_set_style_text_color(counter, lv_color_make(220, 200, 40), 0);
  lv_obj_set_style_text_font(counter, &lv_font_montserrat_28, 0);
  lv_obj_align(counter, LV_ALIGN_CENTER, 0, 80);

  // Button event handler
  lv_obj_add_event_cb(
      btn,
      [](lv_event_t* e) {
        touch_count++;
        lv_obj_t* lbl = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
        char buf[32];
        snprintf(buf, sizeof(buf), "Touches: %lu",
                 (unsigned long)touch_count);
        lv_label_set_text(lbl, buf);
      },
      LV_EVENT_CLICKED, counter);

  // Status bar at bottom
  lv_obj_t* status = lv_label_create(scr);
  lv_label_set_text(status, "Phase 1: Hardware validation");
  lv_obj_set_style_text_color(status, lv_color_make(80, 100, 120), 0);
  lv_obj_set_style_text_font(status, &lv_font_montserrat_16, 0);
  lv_obj_align(status, LV_ALIGN_BOTTOM_MID, 0, -10);
}

void setup() {
  SetupLogging(ESP_LOG_INFO);

  ESP_LOGI("main", "=== Cockpit Phase 1: LVGL ===");

  // Initialize display + touch + LVGL
  auto* display = new Waveshare7BDisplay();
  auto* touch = new Waveshare7BTouch();
  lvgl_init(display, touch);

  // Create test UI
  create_test_ui();

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

  // LVGL runs from the main event loop at ~30fps
  event_loop()->onRepeat(33, []() { lvgl_tick(); });

  // Heartbeat
  event_loop()->onRepeat(5000, [receiver, n2k_server]() {
    ESP_LOGI("main", "n2k: rx=%u clients=%u | touch=%lu | heap=%lu",
             (unsigned)receiver->rx_count(),
             (unsigned)n2k_server->connected_clients(),
             (unsigned long)touch_count,
             (unsigned long)esp_get_free_heap_size());
  });
}

void loop() { event_loop()->tick(); }
