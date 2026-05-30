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

}  // namespace jlp
