"""PlatformIO pre-script: say out loud whether this build is provisioned.

A build with no WiFi credentials is a legitimate, intended artifact — it is
what every release ships. But it is indistinguishable from a build where
platformio_local.ini was silently not read (an `extra_configs` line that only
exists on another branch, a typo'd filename, a file that was never created),
because PlatformIO does not warn about overrides it did not apply.

That failure is expensive: the firmware builds and flashes cleanly, then the
panel reboot-loops on `wifi disconnected` with nothing in the build log to
explain why. Printing the resolved values turns a silent misconfiguration into
one line you cannot miss.
"""
Import("env")  # noqa: F821 — provided by SCons

_UNSET = "<unset>"


def _macro(name):
    """Value of a -D NAME='"VALUE"' build flag, or _UNSET.

    Read from the raw BUILD_FLAGS string rather than CPPDEFINES: at
    pre-script time PlatformIO has not yet parsed the -D flags into
    CPPDEFINES, so that list is empty and every value looks unset.
    """
    for flag in env.get("BUILD_FLAGS", []):  # noqa: F821
        text = str(flag)
        marker = "-D " + name + "="
        if text.startswith(name + "="):
            return text.split("=", 1)[1].strip("'\"") or _UNSET
        if marker in text:
            return text.split(marker, 1)[1].strip("'\"") or _UNSET
    return _UNSET


ssid = _macro("COCKPIT_WIFI_SSID")
sk_host = _macro("COCKPIT_SK_HOST")

print("=" * 62)
if ssid is _UNSET:
    print("provisioning: NONE — ships unprovisioned (SensESP config portal)")
    print("  This is correct for a release. For a personal build, put your")
    print("  credentials in platformio_local.ini (see the .example) and check")
    print("  that platformio.ini still has `extra_configs`.")
else:
    print(f"provisioning: wifi={ssid}  sk={sk_host}")
    print("  LOCAL BUILD — do not publish this image; `strings` prints both.")
print("=" * 62)
