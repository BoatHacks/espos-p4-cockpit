#include "mdns_announce.h"

#include "esp_log.h"
#include "mdns.h"
#include "sensesp.h"

static const char* TAG = "jlp.mdns";

namespace jlp {

void mdns_announce_start(uint16_t api_port) {
  // SensESP defers MDNS.begin via onDelay(0); we defer further so we
  // run AFTER that. Two zero-delays preserves enqueue order.
  sensesp::event_loop()->onDelay(0, [api_port]() {
    sensesp::event_loop()->onDelay(0, [api_port]() {
      mdns_txt_item_t txt[] = {
          // Keep widgets + firmware in lockstep with /hello in
          // http_api.cpp. mDNS browsers use these for capability
          // discovery before falling back to a real /hello fetch.
          {"schema",   "1"},
          {"widgets",  "label,value,toggle,arc,bar,bargroup,button,notifications,"
                       "anchor,anchor_track,voice,mute_speaker,mute_mic,volume"},
          {"firmware", "p4-cockpit-jlp-1.0.1"},
          {"api",      "/layout,/hello,/healthz,/screenshot"},
      };
      esp_err_t err = mdns_service_add(
          NULL, "_signalk-player", "_tcp", api_port,
          txt, sizeof(txt) / sizeof(txt[0]));
      if (err != ESP_OK) {
        ESP_LOGW(TAG, "mdns_service_add failed: %s", esp_err_to_name(err));
      } else {
        ESP_LOGI(TAG, "announced _signalk-player._tcp on port %u",
                 api_port);
      }
    });
  });
}

}  // namespace jlp
