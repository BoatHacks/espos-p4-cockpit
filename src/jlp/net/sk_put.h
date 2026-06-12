#pragma once

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

}  // namespace jlp
