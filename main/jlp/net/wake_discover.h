// SPDX-License-Identifier: LicenseRef-Source-Available-No-Redistribution
#pragma once

namespace espos_voice {
class WyomingSatellite;
}

namespace jlp {

// Ask the SignalK server whether it runs a wake service, and if so point the
// satellite at it.
//
// The panel boots on the on-device word (esp-sr WakeNet, "Hi ESP") because at
// construction time there is no network to ask. Once the SK link is up this
// queries the signalk-openwakeword plugin and, if it is ready, switches the
// satellite to network wake -- so a custom-trained word configured on the
// server just works, with no per-panel setting to keep in sync.
//
// Call once after the SignalK stream connects; it runs a short task and
// returns. Raw-boot discovery fails (no route yet), which is why this is tied
// to the stream rather than to app_main.
void wake_discover_start(espos_voice::WyomingSatellite* sat);

}  // namespace jlp
