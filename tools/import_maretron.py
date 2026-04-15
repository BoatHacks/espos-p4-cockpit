#!/usr/bin/env python3
"""
Import a Maretron N2KView config and generate a C++ header describing
the screens, switches, gauges, and indicators for the cockpit display.

Usage:
  python3 import_maretron.py <config.n2k> > ../src/maretron_layout.h

The input file is Base64-encoded XML produced by N2KView's export.
"""

import base64
import sys
import xml.etree.ElementTree as ET


def decode_file(path):
    with open(path, 'rb') as f:
        data = f.read()
    # File is Base64 without line breaks
    return base64.b64decode(data).decode('utf-8')


def c_escape(s):
    """Escape a string for C string literal."""
    if s is None:
        return ""
    return s.replace('\\', '\\\\').replace('"', '\\"')


def main():
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        sys.exit(1)

    xml_str = decode_file(sys.argv[1])
    root = ET.fromstring(xml_str)

    print("// Auto-generated from Maretron N2KView config — do not edit.")
    print(f"// Source: {sys.argv[1]}")
    print(f"// Version: {root.get('version')}")
    print(f"// Export date: {root.get('date')}")
    print()
    print("#pragma once")
    print()
    print("#include <cstdint>")
    print("#include <vector>")
    print()
    print("namespace cockpit_config {")
    print()
    print("struct Switch {")
    print("  const char* label;")
    print("  int16_t bank;")
    print("  int16_t channel;")
    print("  const char* screen;")
    print("};")
    print()
    print("struct Indicator {")
    print("  const char* label;")
    print("  int16_t bank;")
    print("  int16_t channel;")
    print("  const char* screen;")
    print("};")
    print()
    print("struct Gauge {")
    print("  const char* label;")
    print("  const char* unit;")
    print("  const char* code;      // Maretron p attribute")
    print("  int instance;          // -1 for singleton")
    print("  float min;")
    print("  float max;")
    print("  const char* screen;")
    print("};")
    print()

    switches = []
    indicators = []
    gauges = []

    for screen in root.findall('screen'):
        screen_name = screen.get('label', '?')
        for comp in screen.findall('component'):
            ctrl = comp.get('control', '')
            title = comp.get('title', '?')

            if ctrl in ('S', 'CB'):
                inst = comp.get('instance', '-1')
                ind = comp.get('indicator', '0')
                try:
                    switches.append((title, int(inst), int(ind), screen_name))
                except ValueError:
                    pass
            elif ctrl in ('I', 'Il', 'Is'):
                inst = comp.get('instance', '-1')
                ind = comp.get('indicator', '0')
                try:
                    indicators.append((title, int(inst), int(ind), screen_name))
                except ValueError:
                    pass
            elif ctrl in ('D', 'G', 'W'):
                try:
                    gauges.append((
                        title,
                        comp.get('unit', ''),
                        comp.get('p', ''),
                        int(comp.get('instance', '-1')),
                        float(comp.get('min', '0')) if comp.get('min') else 0.0,
                        float(comp.get('max', '0')) if comp.get('max') else 0.0,
                        screen_name,
                    ))
                except ValueError:
                    pass

    print(f"// {len(switches)} switches / circuit breakers")
    print("inline const std::vector<Switch>& get_switches() {")
    print("  static const std::vector<Switch> v = {")
    for label, bank, ch, screen in switches:
        print(f'      {{"{c_escape(label)}", {bank}, {ch}, "{c_escape(screen)}"}},')
    print("  };")
    print("  return v;")
    print("}")
    print()

    print(f"// {len(indicators)} indicator lamps")
    print("inline const std::vector<Indicator>& get_indicators() {")
    print("  static const std::vector<Indicator> v = {")
    for label, bank, ch, screen in indicators:
        print(f'      {{"{c_escape(label)}", {bank}, {ch}, "{c_escape(screen)}"}},')
    print("  };")
    print("  return v;")
    print("}")
    print()

    print(f"// {len(gauges)} gauges and readouts")
    print("inline const std::vector<Gauge>& get_gauges() {")
    print("  static const std::vector<Gauge> v = {")
    for label, unit, code, inst, mn, mx, screen in gauges:
        print(f'      {{"{c_escape(label)}", "{c_escape(unit)}", '
              f'"{c_escape(code)}", {inst}, {mn}f, {mx}f, "{c_escape(screen)}"}},')
    print("  };")
    print("  return v;")
    print("}")
    print()

    print("}  // namespace cockpit_config")


if __name__ == "__main__":
    main()
