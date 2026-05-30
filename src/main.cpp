/**
 * ESP32-P4 Cockpit — JSON Layout Player (jlp)
 *
 * Step 2: status overlay added. The overlay is the only always-on UI
 * element; future layout swaps reparent under overlay().content_root().
 */

#include <Arduino.h>
#include <WiFi.h>
#include <cstdio>
#include <string>
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

#include "jlp/default_layout.h"
#include "jlp/layout/layout_manager.h"
#include "jlp/layout/store.h"
#include "jlp/net/http_api.h"
#include "jlp/net/layout_fetch.h"
#include "jlp/net/mdns_announce.h"
#include "jlp/status_overlay.h"
#include "jlp/subject_registry.h"

using namespace sensesp;
using namespace sensesp_cockpit_display;

void setup() {
  SetupLogging(ESP_LOG_INFO);

  auto* display = new Waveshare7BDisplay();
  auto* touch = new Waveshare7BTouch();
  lvgl_init(display, touch);
  jlp::overlay().init();
  jlp::overlay().set_hostname("p4-cockpit");

  jlp::layout_manager().init(jlp::overlay().content_root());

  jlp::store_init();
  std::string stored;
  jlp::ApplyResult r;
  if (jlp::store_read(&stored)) {
    r = jlp::layout_manager().apply(stored, jlp::ApplySource::BootStore);
    if (!r.ok) {
      ESP_LOGW("main", "stored layout rejected (%s); falling back to default",
               r.err.c_str());
    }
  }
  if (!r.ok) {
    r = jlp::layout_manager().apply(jlp::kDefaultLayoutJson,
                                    jlp::ApplySource::BootDefault);
    if (!r.ok) {
      ESP_LOGE("main", "default layout rejected: %s", r.err.c_str());
    }
  }

  SensESPAppBuilder builder;
  auto app = builder.set_hostname("p4-cockpit")
                 ->set_wifi_client("MOIN", "Moin2018!")
                 ->set_wifi_access_point("", "")
                 ->set_sk_server("192.168.0.148", 3000)
                 ->get_app();

  remote_log_start(2323);
  http_ota_start(8080);
  jlp::http_api_start(8081);
  jlp::mdns_announce_start(8081);
  jlp::layout_fetch_async_apply("192.168.0.148", 3000);

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
