#pragma once

#include <string>

namespace jlp {

// On-device persistence for the active layout. Stored as a single NVS
// blob in a dedicated "layout" NVS partition (see p4_16mb.csv). NVS
// replaced LittleFS here because LittleFS on this flash did not survive
// a power-cycle — the partition came back unmountable and was silently
// reformatted, dropping the persisted layout.

// Mount the partition. Idempotent. Returns false on failure.
bool store_init();

// One-line summary of what store_init() observed at boot (mount
// used-bytes, whether a layout was preserved/restored, reformats).
// Surfaced via /hello for diagnostics when serial isn't available.
const char* store_boot_report();

// Returns true and fills `out` if a layout blob is stored. False if
// absent, or on read error.
bool store_read(std::string* out);

// Persist the layout. Atomic and power-fail-safe via NVS: nvs_set_blob
// stages the value and nvs_commit makes it visible in one step, so a
// power loss mid-write leaves the prior value intact (no tmp/rename
// needed). Returns false on any NVS error. Caller must only call this
// after a successful layout swap. (Name kept for the unchanged caller.)
bool store_write_atomic(const std::string& json);

// Remove the persisted layout. Called by main.cpp on boot when the
// stored layout fails to apply, so subsequent boots skip straight to
// the compiled default instead of re-trying the same bad blob.
// Returns true if the blob was erased or wasn't there to begin with;
// false only on a real NVS error.
bool store_clear();

}  // namespace jlp
