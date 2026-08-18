#pragma once

#include <stdint.h>
#include <string>

namespace jlp {

// Schedules a one-shot GET to the SignalK applicationData layout
// endpoint and, on success, hands the body to LayoutManager::apply
// with ApplySource::BootFetched. 404 / 4xx are treated as "no server
// layout" and silently leave the current layout alone.
//
// Runs on the event_loop task via onDelay; safe to call from setup().
void layout_fetch_async_apply(const std::string& sk_host, uint16_t sk_port);

}  // namespace jlp
