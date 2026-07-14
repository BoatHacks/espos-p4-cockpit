#pragma once

#include <set>
#include <string>

namespace jlp {

// Thin wrapper over SensESP's SKPutRequest<T>. Caches one request
// object per (path, kind) since constructing a new one each tap would
// leak (SKPutRequest registers itself in a static list with no
// removal API).
//
// Threading: call from the event_loop task only (widget click
// callbacks already are).
void put_bool(const std::string& path, bool value);
void put_int(const std::string& path, int value);
void put_float(const std::string& path, float value);
void put_string(const std::string& path, const std::string& value);

/** PUT a notification ACK to `notifications.<path_after_prefix>`.
 *  Sends the SK convention `{value: {state: "normal", method: [],
 *  message: ""}}`. Called by the alert overlay when the operator
 *  taps ACK on a pending notification. */
void put_notification_ack(const std::string& path_after_prefix);

/** Send an incremental SK subscribe frame for `paths` over the open WS.
 *
 *  SensESP subscribes to all its listeners exactly once, at WS connect.
 *  A path first bound by a layout pushed *after* connect therefore never
 *  joins the subscription, so its widget never receives live deltas
 *  (it shows only the one-shot REST value seed and then sits frozen).
 *  This sends `{context:"vessels.self", subscribe:[{path, period}...]}`
 *  for just the newly-introduced paths so they start streaming without
 *  a full ws->restart() (whose reconnect burst wedges event_loop).
 *
 *  Threading: event_loop task only (sendTXT is serialized there). */
void subscribe_new_paths(const std::set<std::string>& paths);

}  // namespace jlp
