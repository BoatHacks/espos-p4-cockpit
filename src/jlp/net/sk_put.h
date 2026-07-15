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

// PUT an explicit JSON null to `path`. SKPutRequest<T> is typed and
// can't express null, so this sends the SK put envelope directly. Used
// by button widgets whose value is null — e.g. raising an anchor by
// clearing navigation.anchor.position.
void put_null(const std::string& path);

/** PUT a notification ACK to `notifications.<path_after_prefix>`.
 *  Sends the SK convention `{value: {state: "normal", method: [],
 *  message: ""}}`. Called by the alert overlay when the operator
 *  taps ACK on a pending notification. */
void put_notification_ack(const std::string& path_after_prefix);

// SensESP only subscribes at WS connect, so a path first bound by a
// layout pushed afterwards never streams — its widget freezes at the
// one-shot REST seed. This backfills that gap without a full
// ws->restart() (whose reconnect burst wedges event_loop); the send is
// marshalled onto event_loop internally, so it is callable from any task.
/** Send an incremental SK subscribe (`{context, subscribe:[{path,
 *  period}...]}`) for the newly-introduced `paths` over the open WS. */
void subscribe_new_paths(const std::set<std::string>& paths);

}  // namespace jlp
