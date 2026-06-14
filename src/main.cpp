/**
 * ESP32-P4 Cockpit — JSON Layout Player (jlp)
 *
 * Step 2: status overlay added. The overlay is the only always-on UI
 * element; future layout swaps reparent under overlay().content_root().
 */

#include <Arduino.h>
#include <WiFi.h>
#include <cstdio>
#include <set>
#include <string>
#include <vector>
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
#include "jlp/idle_dimmer.h"
#include "jlp/layout/layout_manager.h"
#include "jlp/layout/store.h"
#include "jlp/net/http_api.h"
#include "jlp/net/layout_fetch.h"
#include "jlp/net/zone_fetch.h"
#include "jlp/zone_registry.h"
#include "jlp/net/mdns_announce.h"
#include "jlp/status_overlay.h"
#include "jlp/subject_registry.h"
#include "jlp/alert_overlay.h"
#include "jlp/notifications_registry.h"
#include "jlp/sun_state.h"
#include "jlp/wake_overlay.h"
#include "jlp/zone_registry.h"

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
  // Value-init so r.ok is false on the !store_read() path; Cppcheck
  // flags the default-init form as a use-of-uninitialized-member.
  jlp::ApplyResult r{};
  if (jlp::store_read(&stored)) {
    r = jlp::layout_manager().apply(stored, jlp::ApplySource::BootStore);
    if (!r.ok) {
      ESP_LOGW("main", "stored layout rejected (%s); clearing + falling back",
               r.err.c_str());
      // Drop the bad blob so the next boot doesn't keep re-reading and
      // re-rejecting the same content. The user can push a fresh layout
      // from the designer once the device is responsive; until then the
      // compiled default applies (next branch below).
      jlp::store_clear();
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
                 ->set_sk_server("192.168.0.148", 4100)
                 ->get_app();

  // Start SNTP so `time(nullptr)` returns valid Unix seconds once
  // WiFi is up. UTC only — the day/night classifier in sun_state
  // works purely in UTC against SK's sunsetTime / sunriseTime, no
  // local TZ needed. SNTP client retries internally; firing it
  // before WiFi is fine.
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  // After every layout swap, restart the SK WS only when the new
  // layout introduced bound paths SensESP isn't already subscribed
  // to. (SensESP subscribes once at on_connected; listeners added
  // later are silently ignored until reconnect — so we only pay the
  // reconnect cost when there's a real subscription gap.)
  //
  // Deferred to a fresh event_loop tick so the apply() that invoked
  // us can return to the HTTP task and signal its done-semaphore
  // *before* we start tearing down the WS. ws->restart() is
  // synchronous and can take several seconds on cold reconnect; if
  // we ran it inline, POST /layout would 504 on every first-push-
  // after-boot (the only case that introduces a brand-new path set
  // relative to the empty known_paths_ map).
  jlp::layout_manager().set_post_swap_hook(
      [app](bool new_paths_introduced,
            const std::set<std::string>& new_paths) {
        if (!new_paths_introduced) return;
        // Pull SK meta (zones + description) for just the newly-
        // introduced paths via HTTP REST. The per-path endpoint is
        // ~25 ms per path on a healthy LAN; the fetch task drives
        // them serially so the burst stays small.
        std::vector<std::string> v(new_paths.begin(), new_paths.end());
        jlp::zone_fetch_for_paths("192.168.0.148", 4100, v);
        // ws->restart() is intentionally NOT called here anymore.
        // The reconnect floods event_loop with SK's initial-state
        // burst for 30+ seconds, wedging the next push or
        // /screenshot. New SKValueListeners created lazily by
        // widget builds don't get an immediate subscription, but
        // they DO get filled by the zone_fetch_for_paths REST
        // value seed above — same effect on the widget render
        // without the WS storm. Once the panel power-cycles or
        // the WS happens to reconnect on its own, the lazy
        // listeners join the next subscribe frame normally.
      });

  remote_log_start(2323);
  http_ota_start(8080);
  jlp::http_api_start(8081);
  jlp::mdns_announce_start(8081);
  jlp::zones().hook_sk_ws();
  jlp::notifications().hook_sk_ws();
  jlp::sun_state().hook_sk_ws();
  jlp::alert_overlay().init();
  // wake overlay must be initialised AFTER alert_overlay so that
  // alert_overlay's move_foreground (when it pops a notification)
  // still wins z-order over the wake-overlay.
  jlp::wake_overlay().init();
  jlp::idle_dimmer().init();
  // An ESCALATING notification wakes the panel so the alarm overlay is
  // actually visible. Clears and reductions don't — otherwise a brief
  // transient alarm that auto-resolves (e.g. AC compressor inrush
  // tripping InverterImbalance for a second) drags the helm out of
  // idle every cycle. Token discarded — dimmer is process-lifetime.
  (void)jlp::notifications().on_change([]() {
    if (jlp::notifications().last_change_was_escalation()) {
      jlp::idle_dimmer().wake();
    }
  });
  jlp::layout_fetch_async_apply("192.168.0.148", 4100);

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

  // event_loop liveness watchdog.
  //
  // The health-check watchdog above runs ON event_loop, so if
  // event_loop ever deadlocks (a lock held and never released, an
  // infinite loop in a callback) it can't fire — the panel hangs and
  // needs a manual power-cycle. This second watchdog runs on its own
  // FreeRTOS task: event_loop bumps a monotonic heartbeat every tick;
  // the task samples it once a second and force-reboots if it hasn't
  // advanced for 15 s. 15 s is comfortably longer than the worst
  // legitimate event_loop stall we expect (a ~5 s screenshot burst,
  // a layout apply draining the WS reconnect storm) but short enough
  // that a real hang self-recovers instead of stranding the helm.
  static volatile uint32_t s_event_loop_heartbeat = 0;
  event_loop()->onRepeat(250, []() { s_event_loop_heartbeat++; });
  xTaskCreate(
      [](void*) {
        uint32_t last = 0;
        int stalled_s = 0;
        for (;;) {
          vTaskDelay(pdMS_TO_TICKS(1000));
          uint32_t now = s_event_loop_heartbeat;
          if (now == last) {
            if (++stalled_s >= 15) {
              ESP_LOGE("el_wdt",
                       "event_loop stalled %ds (hb=%lu); rebooting",
                       stalled_s, (unsigned long)now);
              esp_restart();
            }
          } else {
            stalled_s = 0;
            last = now;
          }
        }
      },
      "el_wdt", 2560, nullptr, configMAX_PRIORITIES - 1, nullptr);

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
