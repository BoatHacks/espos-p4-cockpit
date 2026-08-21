// SPDX-License-Identifier: LicenseRef-Source-Available-No-Redistribution
#pragma once

namespace espos_voice {
class WyomingSatellite;
}

namespace jlp {

// Point the satellite at the server's wake service, so the wake word is
// configured once on the server instead of per panel.
//
// Call after the SignalK stream connects: at boot there is no route yet.
void wake_discover_start(espos_voice::WyomingSatellite* sat);

}  // namespace jlp
