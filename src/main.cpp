/**
 * ESP32-P4 Cockpit — JSON Layout Player (jlp)
 *
 * Step 2: status overlay added. The overlay is the only always-on UI
 * element; future layout swaps reparent under overlay().content_root().
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
#include "sensesp/signalk/signalk_ws_client.h"
#include "sensesp/system/lambda_consumer.h"

#include "jlp/status_overlay.h"
#include "jlp/subject_registry.h"

using namespace sensesp;
using namespace sensesp_cockpit_display;

namespace {

// Step-3 demo: one toggle + one label bound via the SubjectRegistry. In
// step 4 this is replaced by the JSON builder.
void build_demo_layout(lv_obj_t* parent) {
  // --- Toggle: switches.bank.0.0 ---
  {
    lv_subject_t* sub = jlp::registry().get_or_create(
        "electrical.switches.bank.0.0.state", jlp::SubjectKind::Int);

    lv_obj_t* lbl = lv_label_create(parent);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xe6edf3), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_label_set_text(lbl, "bank 0.0");
    lv_obj_set_pos(lbl, 20, 20);

    lv_obj_t* sw = lv_switch_create(parent);
    lv_obj_set_pos(sw, 20, 50);
    lv_obj_set_size(sw, 80, 40);

    lv_subject_add_observer_obj(
        sub,
        [](lv_observer_t* obs, lv_subject_t* s) {
          auto* w = lv_observer_get_target_obj(obs);
          if (lv_subject_get_int(s)) lv_obj_add_state(w, LV_STATE_CHECKED);
          else                       lv_obj_remove_state(w, LV_STATE_CHECKED);
        },
        sw, nullptr);
  }

  // --- Label: environment.depth.belowTransducer (m) ---
  {
    lv_subject_t* sub = jlp::registry().get_or_create(
        "environment.depth.belowTransducer", jlp::SubjectKind::Float);

    lv_obj_t* cap = lv_label_create(parent);
    lv_obj_set_style_text_color(cap, lv_color_hex(0x8b949e), LV_PART_MAIN);
    lv_obj_set_style_text_font(cap, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text(cap, "depth");
    lv_obj_set_pos(cap, 200, 20);

    lv_obj_t* val = lv_label_create(parent);
    lv_obj_set_style_text_color(val, lv_color_hex(0xe6edf3), LV_PART_MAIN);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_label_set_text(val, "-.- m");
    lv_obj_set_pos(val, 200, 40);

    lv_subject_add_observer_obj(
        sub,
        [](lv_observer_t* obs, lv_subject_t* s) {
          auto* w = lv_observer_get_target_obj(obs);
          float v = lv_subject_get_float(s);
          lv_label_set_text_fmt(w, "%.1f m", v);
        },
        val, nullptr);
  }
}

}  // namespace

void setup() {
  SetupLogging(ESP_LOG_INFO);

  auto* display = new Waveshare7BDisplay();
  auto* touch = new Waveshare7BTouch();
  lvgl_init(display, touch);
  jlp::overlay().init();
  jlp::overlay().set_hostname("p4-cockpit");
  build_demo_layout(jlp::overlay().content_root());

  SensESPAppBuilder builder;
  auto app = builder.set_hostname("p4-cockpit")
                 ->set_wifi_client("MOIN", "Moin2018!")
                 ->set_wifi_access_point("", "")
                 ->set_sk_server("192.168.0.148", 3000)
                 ->get_app();

  remote_log_start(2323);
  http_ota_start(8080);

  // --- SK WS state into the overlay ---
  auto ws_client = app->get_ws_client();
  ws_client->connect_to(new LambdaConsumer<SKWSConnectionState>(
      [](SKWSConnectionState state) {
        switch (state) {
          case SKWSConnectionState::kSKWSConnected:
            jlp::overlay().set_sk("ok");
            break;
          case SKWSConnectionState::kSKWSConnecting:
            jlp::overlay().set_sk("connecting");
            break;
          case SKWSConnectionState::kSKWSAuthorizing:
            jlp::overlay().set_sk("auth");
            break;
          case SKWSConnectionState::kSKWSDisconnected:
          default:
            jlp::overlay().set_sk("down");
            break;
        }
      }));

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

  // 1s status tick: WiFi / N2K / uptime / heap into the overlay.
  event_loop()->onRepeat(1000, [receiver, n2k_server]() {
    if (WiFi.status() == WL_CONNECTED) {
      char buf[40];
      snprintf(buf, sizeof(buf), "%s %ddBm", WiFi.SSID().c_str(),
               WiFi.RSSI());
      jlp::overlay().set_wifi(buf);
    } else {
      jlp::overlay().set_wifi("down");
    }

    int64_t rx_idle =
        receiver->ever_received() ? receiver->seconds_since_last_rx() : -1;
    jlp::overlay().set_n2k(rx_idle, n2k_server->connected_clients());

    jlp::overlay().set_uptime_heap(millis() / 1000,
                                   esp_get_free_heap_size());
  });

  event_loop()->onRepeat(5000, [receiver, n2k_server]() {
    int64_t rx_idle = receiver->seconds_since_last_rx();
    ESP_LOGI("main", "n2k: rx_idle=%llds cl=%u | heap=%lu",
             (long long)(receiver->ever_received() ? rx_idle : -1),
             (unsigned)n2k_server->connected_clients(),
             (unsigned long)esp_get_free_heap_size());
  });
}

void loop() { event_loop()->tick(); }
