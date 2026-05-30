/**
 * ESP32-P4 Cockpit — JSON Layout Player (jlp)
 *
 * Step 1 skeleton: HAL up, single hardcoded LVGL label, all the
 * existing infra (WiFi, OTA, remote log, N2K gateway, watchdog).
 * Compile-time switch/gauge wiring is gone. Subsequent steps add the
 * subject registry, JSON parser, POST /layout endpoint, etc.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <cstdio>
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "sensesp_cockpit_display/hal/boards/waveshare_7b.h"
#include "sensesp_cockpit_display/lvgl/lv_drivers.h"
#include "sensesp_cockpit_display/net/http_ota.h"
#include "sensesp_cockpit_display/net/remote_log.h"
#include "sensesp_n2k_gateway.h"
#include "sensesp_app_builder.h"

using namespace sensesp;
using namespace sensesp_cockpit_display;

static lv_obj_t* g_boot_label = nullptr;

static void build_boot_screen() {
  lv_obj_t* scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0d1117), LV_PART_MAIN);

  g_boot_label = lv_label_create(scr);
  lv_obj_set_style_text_color(g_boot_label, lv_color_hex(0xe6edf3),
                              LV_PART_MAIN);
  lv_obj_set_style_text_font(g_boot_label, &lv_font_montserrat_28,
                             LV_PART_MAIN);
  lv_label_set_text(g_boot_label, "jlp boot");
  lv_obj_center(g_boot_label);
}

void setup() {
  SetupLogging(ESP_LOG_INFO);

  auto* display = new Waveshare7BDisplay();
  auto* touch = new Waveshare7BTouch();
  lvgl_init(display, touch);
  build_boot_screen();

  SensESPAppBuilder builder;
  auto app = builder.set_hostname("p4-cockpit")
                 ->set_wifi_client("MOIN", "Moin2018!")
                 ->set_wifi_access_point("", "")
                 ->set_sk_server("192.168.0.148", 3000)
                 ->get_app();

  remote_log_start(2323);
  http_ota_start(8080);

  // --- N2K gateway (unchanged from cockpit firmware) ---
  auto* receiver =
      new TwaiReceiver(TwaiReceiverConfig::waveshare_touch_lcd_7b());
  auto* transmitter = new TwaiTransmitter();
  auto* n2k_server = new CandumpTcpServer(receiver, transmitter);
  receiver->start();
  transmitter->start();
  n2k_server->start();

  sensesp_cockpit_display::set_ota_quiesce_callback(
      [receiver, transmitter, n2k_server]() {
        n2k_server->stop();
        receiver->stop();
        transmitter->stop();
      });

  // Watchdog: reboot on WiFi loss, heap exhaustion, or N2K rx stall
  // (only after we have ever received a frame).
  event_loop()->onRepeat(30000, [receiver]() {
    static int consecutive_fail = 0;
    bool ok = true;
    const char* reason = "";

    if (WiFi.status() != WL_CONNECTED) {
      ok = false;
      reason = "wifi disconnected";
    } else if (esp_get_free_heap_size() < 64 * 1024) {
      ok = false;
      reason = "heap exhausted";
    } else if (receiver->ever_received() &&
               receiver->seconds_since_last_rx() > 30) {
      ok = false;
      reason = "n2k rx stalled";
    }

    if (!ok) {
      consecutive_fail++;
      ESP_LOGW("watchdog", "health check FAIL %d/3: %s", consecutive_fail,
               reason);
      if (consecutive_fail >= 3) {
        ESP_LOGE("watchdog", "rebooting due to: %s", reason);
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_restart();
      }
    } else {
      consecutive_fail = 0;
    }
  });

  event_loop()->onRepeat(33, []() { lvgl_tick(); });

  event_loop()->onRepeat(5000, [receiver, n2k_server]() {
    int64_t rx_idle = receiver->seconds_since_last_rx();
    ESP_LOGI("main", "n2k: rx_idle=%llds cl=%u | heap=%lu",
             (long long)(receiver->ever_received() ? rx_idle : -1),
             (unsigned)n2k_server->connected_clients(),
             (unsigned long)esp_get_free_heap_size());
  });
}

void loop() { event_loop()->tick(); }
