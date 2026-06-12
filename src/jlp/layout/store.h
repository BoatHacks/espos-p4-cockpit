#pragma once

#include <string>

namespace jlp {

// On-device persistence for the active layout. v1: a single file on a
// littlefs partition (labelled "spiffs" — the pio-default name for the
// data partition; we use it via the joltwallet littlefs driver).

// Mount the partition. Idempotent. Returns false on failure.
bool store_init();

// Returns true and fills `out` if a layout file exists. False if
// missing, or on read error.
bool store_read(std::string* out);

// Atomic write: write to a tmp path then rename. Returns false on any
// IO error. Caller must only call this after a successful layout swap.
bool store_write_atomic(const std::string& json);

// Remove the persisted layout. Called by main.cpp on boot when the
// stored layout fails to apply, so subsequent boots skip straight to
// the compiled default instead of re-trying the same bad blob.
// Returns true if the file was removed or wasn't there to begin with;
// false only on a real IO error.
bool store_clear();

}  // namespace jlp
