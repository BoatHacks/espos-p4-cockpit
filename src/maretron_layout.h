// Auto-generated from Maretron N2KView config — do not edit.
// Source: /home/dirk/dev/MOIN_Config_Maretron_13_n2k.n2k
// Version: 6.4.1.20221117
// Export date: 17-Jan-2025 19:25:40 UTC

#pragma once

#include <cstdint>
#include <vector>

namespace cockpit_config {

struct Switch {
  const char* label;
  int16_t bank;
  int16_t channel;
  const char* screen;
};

struct Indicator {
  const char* label;
  int16_t bank;
  int16_t channel;
  const char* screen;
};

struct Gauge {
  const char* label;
  const char* unit;
  const char* code;      // Maretron p attribute
  int instance;          // -1 for singleton
  float min;
  float max;
  const char* screen;
};

// 53 switches / circuit breakers
inline const std::vector<Switch>& get_switches() {
  static const std::vector<Switch> v = {
      {"Tank Port -> STB", 9, 2, "Watermaker"},
      {"Radio Galley", 73, 2, "Switches"},
      {"Radio Galley", 73, 3, "Switches"},
      {"Radio Galley", 10, 1, "Switches"},
      {"Radio Galley", 10, 2, "Switches"},
      {"Radio Galley", 10, 3, "Switches"},
      {"Radio Galley", 10, 5, "Switches"},
      {"Radio Galley", 10, 6, "Switches"},
      {"Radio Galley", 12, 1, "Switches"},
      {"Wifi Mast EXTERN", 10, 4, "Switches"},
      {"Radio Salon", 12, 5, "Switches"},
      {"AIS Power", 12, 3, "Switches"},
      {"VHF Power", 12, 4, "Switches"},
      {"AIS Silent", 12, 6, "Switches"},
      {"Blackwater Close", 11, 1, "Switches"},
      {"Cockpit Alarm SW", 11, 2, "Switches"},
      {"Ambilight TV", 11, 3, "Switches"},
      {"Fueltransfer", 11, 4, "Switches"},
      {"RC Tender/Pasarelle", 11, 5, "Switches"},
      {"Deckwash Pump", 11, 6, "Switches"},
      {"Nav Tricolor", 72, 1, "Switches"},
      {"Nav Steam", 72, 2, "Switches"},
      {"Nav Stern", 72, 3, "Switches"},
      {"Nav Red/Green", 72, 4, "Switches"},
      {"Backbeam", 72, 5, "Switches"},
      {"Cockpit Override", 72, 6, "Switches"},
      {"Underwater Lights", 73, 1, "Switches"},
      {"MT Strobe", 73, 4, "Switches"},
      {"Courtesy Inside", 73, 5, "Switches"},
      {"Courtesy Outside", 73, 6, "Switches"},
      {"Monitors Fly", 13, 1, "Switches"},
      {"Navigation Instruments", 12, 2, "Switches"},
      {"Autopilot Primary", 13, 2, "Switches"},
      {"Compass Light", 13, 3, "Switches"},
      {"Autoanchor", 13, 4, "Switches"},
      {"Alarm Sound Inverter", 13, 5, "Switches"},
      {"Monitor Heater Fly", 13, 6, "Switches"},
      {"Airco Generator", 6, 1, "Switches 2"},
      {"Dehumidifier STB", 6, 2, "Switches 2"},
      {"Dehumidifier Port", 6, 3, "Switches 2"},
      {"Airhandler MAIN", 7, 1, "Switches 2"},
      {"Airhandler STB", 7, 3, "Switches 2"},
      {"Airhandler Port", 7, 4, "Switches 2"},
      {"Chiller ECO", 8, 2, "Switches 2"},
      {"AUTOFILL Virtual", 9, 1, "Switches 2"},
      {"Watertank LOW", 9, 4, "Switches 2"},
      {"Watertank HIGH", 9, 3, "Switches 2"},
      {"Watermaker Port->STB", 9, 2, "Switches 2"},
      {"Airhandler Salon", 7, 2, "Switches 2"},
      {"Chiller Remote Control", 8, 1, "Switches 2"},
      {"Chiller Heating (Season)", 8, 3, "Switches 2"},
      {"Chiller enabled", 8, 4, "Switches 2"},
      {"Anchor Light", 6, 4, "Switches 2"},
  };
  return v;
}

// 65 indicator lamps
inline const std::vector<Indicator>& get_indicators() {
  static const std::vector<Indicator> v = {
      {"Low Coolant Level", 0, 0, "Engines"},
      {"Over Temperature", 0, 0, "Engines"},
      {"Water In Fuel", 1, 0, "Engines"},
      {"Low Oil Pressure", 1, 0, "Engines"},
      {"Over Temperature", 1, 0, "Engines"},
      {"Low Coolant Level", 1, 0, "Engines"},
      {"Low Oil Pressure", 0, 0, "Engines"},
      {"Water In Fuel", 0, 0, "Engines"},
      {"Low Fuel Pressure", 0, 0, "Engines"},
      {"High Boost Pressure", 0, 0, "Engines"},
      {"High Boost Pressure", 1, 0, "Engines"},
      {"Low Fuel Pressure", 1, 0, "Engines"},
      {"PF C", 213, 6, "Tanks"},
      {"PF C", 213, 6, "Tanks"},
      {"PF O", 213, 5, "Tanks"},
      {"PA O", 213, 3, "Tanks"},
      {"SA C", 214, 2, "Tanks"},
      {"SA O", 214, 1, "Tanks"},
      {"Do not Charge", 212, 1, "Tanks"},
      {"Do not Charge", 212, 2, "Tanks"},
      {"Do not Charge", 212, 5, "Tanks"},
      {"SF C", 214, 4, "Tanks"},
      {"SF O", 214, 3, "Tanks"},
      {"Do not Charge", 213, 1, "Tanks"},
      {"Do not Discharge", 213, 2, "Tanks"},
      {"Do not Charge", 212, 4, "Tanks"},
      {"Do not Charge", 215, 1, "Tanks"},
      {"Do not Charge", 215, 3, "Tanks"},
      {"Fire", 210, 2, "Fire Alarms"},
      {"Fire", 210, 6, "Fire Alarms"},
      {"Fire", 211, 1, "Fire Alarms"},
      {"Smoke", 214, 6, "Fire Alarms"},
      {"Smoke", 214, 5, "Fire Alarms"},
      {"HW", 212, 4, "Fire Alarms"},
      {"HW", 212, 5, "Fire Alarms"},
      {"HW", 212, 2, "Fire Alarms"},
      {"HW", 215, 1, "Fire Alarms"},
      {"Smoke", 212, 3, "Fire Alarms"},
      {"CO", 210, 5, "Fire Alarms"},
      {"CO", 210, 3, "Fire Alarms"},
      {"Fire", 210, 4, "Fire Alarms"},
      {"CO", 210, 1, "Fire Alarms"},
      {"CO", 211, 2, "Fire Alarms"},
      {"CO", 211, 3, "Fire Alarms"},
      {"Fire", 211, 4, "Fire Alarms"},
      {"HW", 212, 1, "Fire Alarms"},
      {"HW", 215, 3, "Fire Alarms"},
      {"HW", 215, 3, "Fire Alarms"},
      {"Port", 215, 5, "Watermaker"},
      {"STB", 215, 6, "Watermaker"},
      {"Oil Change Indicator", 0, 0, "Watermaker"},
      {"Feed Pressure Status", 0, 0, "Watermaker"},
      {"Filter Status", 0, 0, "Watermaker"},
      {"Salinity Status", 0, 0, "Watermaker"},
      {"System Status", 0, 0, "Watermaker"},
      {"Product Solenoid Valve", 0, 0, "Watermaker"},
      {"Production Start/Stop", 0, 0, "Watermaker"},
      {"Rinse Start/Stop", 0, 0, "Watermaker"},
      {"Low Pressure Pump", 0, 0, "Watermaker"},
      {"High Pressure Pump", 0, 0, "Watermaker"},
      {"Running Main", 88, 1, "Bilge Pumps"},
      {"Running Main", 88, 2, "Bilge Pumps"},
      {"Running Engine", 88, 3, "Bilge Pumps"},
      {"Running Engine", 88, 4, "Bilge Pumps"},
      {"Roll", -1, 0, "Roll"},
  };
  return v;
}

// 114 gauges and readouts
inline const std::vector<Gauge>& get_gauges() {
  static const std::vector<Gauge> v = {
      {"Alternator Voltage", "", "EAPot", 1, 8.0f, 16.0f, "Engines"},
      {"Alternator Voltage", "", "EAPot", 0, 8.0f, 16.0f, "Engines"},
      {"Fuel Rate", "liter/hour", "EFR", 0, 0.0f, 0.0f, "Engines"},
      {"Fuel Rate", "liter/hour", "EFR", 1, 0.0f, 0.0f, "Engines"},
      {"Oil Pressure", "kilopascals", "EOP", 0, 200.0f, 800.0f, "Engines"},
      {"Oil Pressure", "kilopascals", "EOP", 1, 200.0f, 800.0f, "Engines"},
      {"Oil Temperature", "degC", "EOTe", 0, 60.0f, 120.0f, "Engines"},
      {"Oil Temperature", "degC", "EOTe", 1, 60.0f, 120.0f, "Engines"},
      {"Engine Temperature", "degC", "EWTemp", 0, 60.0f, 110.0f, "Engines"},
      {"Engine Temperature", "degC", "EWTemp", 1, 60.0f, 110.0f, "Engines"},
      {"Engine Hours", "Hours", "EHrs", 1, 0.0f, 1000000.0f, "Engines"},
      {"Engine Hours", "Hours", "EHrs", 0, 0.0f, 1000000.0f, "Engines"},
      {"Starboard Tachometer", "RPM", "Tach", 1, 0.0f, 3500.0f, "Engines"},
      {"Port Tachometer", "RPM", "Tach", 0, 0.0f, 3500.0f, "Engines"},
      {"Avg. Current", "Amps", "UAvACC", 130, -100.0f, 100.0f, "AC Systems"},
      {"Shore 1", "Volts", "UAvLLV", 130, 180.0f, 270.0f, "AC Systems"},
      {"Avg. Frequency", "Hz", "UAvACF", 130, 40.0f, 70.0f, "AC Systems"},
      {"Avg. Frequency", "Hz", "UAvACF", 131, 40.0f, 70.0f, "AC Systems"},
      {"Avg. Current", "Amps", "UAvACC", 131, -100.0f, 100.0f, "AC Systems"},
      {"Shore 2", "Volts", "UAvLLV", 131, 180.0f, 270.0f, "AC Systems"},
      {"GEN Coolant Temperature", "degC", "EWTemp", 9, 40.0f, 120.0f, "AC Systems"},
      {"GEN Voltage Maretron", "Volts", "GAvLNV", 132, 220.0f, 250.0f, "AC Systems"},
      {"GEN Frequency Maretron", "Hz", "GAvAF", 132, 48.0f, 52.0f, "AC Systems"},
      {"GEN Current Maretron", "Amps", "GAvAC", 132, 0.0f, 64255.0f, "AC Systems"},
      {"GEN Oil Pressure", "kilopascals", "EOP", 9, 0.0f, 600.0f, "AC Systems"},
      {"GEN Engine Hours", "Hours", "EHrs", 9, 0.0f, 0.0f, "AC Systems"},
      {"Avg. L-N AC Voltage", "Volts", "GAvLNV", 9, 220.0f, 240.0f, "AC Systems"},
      {"Avg. AC Frequency", "Hz", "GAvAF", 9, 48.0f, 52.0f, "AC Systems"},
      {"Date", "MMM-DD-YYYY", "DATE", -1, 0.0f, 0.0f, "Navigation"},
      {"Heading", "", "Hdg", -1, 0.0f, 0.0f, "Navigation"},
      {"Course Over Ground", "", "COG", -1, 0.0f, 0.0f, "Navigation"},
      {"Trip Log", "nautical miles", "TrL", -1, 0.0f, 23191.1447084233f, "Navigation"},
      {"Total Log", "nautical miles", "ToL", -1, 0.0f, 23191.1447084233f, "Navigation"},
      {"Position", "", "Lat/Lon", -1, 0.0f, 0.0f, "Navigation"},
      {"Magnetic Variation", "", "Var", -1, 0.0f, 0.0f, "Navigation"},
      {"Speed Over Ground", "nautical miles/hour", "SOG", -1, 0.0f, 40.0f, "Navigation"},
      {"Time", "24-hour", "TIME", -1, 0.0f, 0.0f, "Navigation"},
      {"Water Depth", "meters", "Dep", -1, 0.0f, 0.0f, "Navigation"},
      {"True Wind", "nautical miles/hour", "WiDi", 0, 0.0f, 360.0f, "Navigation"},
      {"Water Temperature", "degC", "SeaTem", 15, 0.0f, 0.0f, "Navigation"},
      {"Speed Through Water", "nautical miles/hour", "STW", 15, 0.0f, 40.0f, "Navigation"},
      {"Twilight PM", "24-hour", "TwPM", -1, 0.0f, 0.0f, "Environment"},
      {"Wind Direction", "", "WiDi", -1, 0.0f, 0.0f, "Environment"},
      {"Wind Speed", "nautical miles/hour", "WiSp", -1, 0.0f, 79.3088552915767f, "Environment"},
      {"Twilight AM Local", "24-hour", "TwAM", -1, 0.0f, 0.0f, "Environment"},
      {"Sunrise Local", "24-hour", "SRs", -1, 0.0f, 0.0f, "Environment"},
      {"Sunset", "24-hour", "SSt", -1, 0.0f, 0.0f, "Environment"},
      {"Heat Index", "degC", "HeI", -1, 0.0f, 0.0f, "Environment"},
      {"Temperature Outside", "degC", "OutTem", 3, 0.0f, 0.0f, "Environment"},
      {"Humidity Outside", "%", "OutHum", 3, 0.0f, 100.0f, "Environment"},
      {"Humidity Inside", "%", "OutHum", 4, 0.0f, 100.0f, "Environment"},
      {"Barometer", "millibars", "Bar", 3, 0.0f, 0.0f, "Environment"},
      {"Dew Point", "degC", "Dew", 3, 0.0f, 0.0f, "Environment"},
      {"Wind Chill Factor", "degC", "WiCh", -1, 0.0f, 0.0f, "Environment"},
      {"Temperature Inside", "degC", "OutTem", 4, 0.0f, 0.0f, "Environment"},
      {"Wind Direction", "nautical miles/hour", "WiDi", -1, -180.0f, 180.0f, "Environment"},
      {"Sea Temperature", "degC", "SeaTem", -1, 0.0f, 0.0f, "Environment"},
      {"DC Voltage", "Volts", "DCV", 0, 8.0f, 16.0f, "Tanks"},
      {"DC Power", "Kilowatts", "DCP", 0, 0.0f, 1000.0f, "Tanks"},
      {"Battery State of Charge", "%", "BaCap", 0, 0.0f, 100.0f, "Tanks"},
      {"Battery State of Health", "%", "BSH", 0, 0.0f, 100.0f, "Tanks"},
      {"Battery Temperature", "degC", "BaTemp", 0, -20.0f, 100.0f, "Tanks"},
      {"DC Current", "Amps", "DCC", 0, 8.0f, 16.0f, "Tanks"},
      {"ER Port Wall", "Amps", "DCC", 108, 8.0f, 16.0f, "Tanks"},
      {"ER STB Wall", "Amps", "DCC", 107, 8.0f, 16.0f, "Tanks"},
      {"ER Port Bat", "Amps", "DCC", 106, 8.0f, 16.0f, "Tanks"},
      {"Genset", "Amps", "DCC", 102, 8.0f, 16.0f, "Tanks"},
      {"ER STB", "Amps", "DCC", 105, 8.0f, 16.0f, "Tanks"},
      {"Inside Temperature", "degC", "InTem", 2, 0.0f, 0.0f, "Fire Alarms"},
      {"Battery Temperature", "degC", "BaTemp", 60, 0.0f, 0.0f, "Fire Alarms"},
      {"Battery Temperature", "degC", "BaTemp", 64, 0.0f, 0.0f, "Fire Alarms"},
      {"Battery Temperature", "degC", "BaTemp", 66, -20.0f, 100.0f, "Fire Alarms"},
      {"Battery Temperature", "degC", "BaTemp", 1, -20.0f, 100.0f, "Fire Alarms"},
      {"Inside Temperature", "degC", "InTem", 3, -20.0f, 70.0f, "Fire Alarms"},
      {"Freezer Temperature", "degC", "FrzTem", -1, -20.0f, 70.0f, "Fire Alarms"},
      {"Battery Temperature", "degC", "BaTemp", 61, -20.0f, 100.0f, "Fire Alarms"},
      {"Battery Temperature", "degC", "BaTemp", 63, 0.0f, 0.0f, "Fire Alarms"},
      {"Battery Temperature", "degC", "BaTemp", 62, 0.0f, 0.0f, "Fire Alarms"},
      {"Outside Humidity", "PF", "OutHum", 3, 0.0f, 100.0f, "Fire Alarms"},
      {"Outside Temperature", "degC", "OutTem", 3, -20.0f, 70.0f, "Fire Alarms"},
      {"Outside Temperature", "degC", "OutTem", 4, -20.0f, 70.0f, "Fire Alarms"},
      {"Battery Temperature", "degC", "BaTemp", 3, 0.0f, 0.0f, "Fire Alarms"},
      {"DC Voltage", "Volts", "DCV", 3, 8.0f, 16.0f, "Fire Alarms"},
      {"DC Voltage", "Volts", "DCV", 63, 8.0f, 16.0f, "Fire Alarms"},
      {"DC Voltage", "Volts", "DCV", 1, 8.0f, 16.0f, "Fire Alarms"},
      {"DC Voltage", "Volts", "DCV", 61, 8.0f, 16.0f, "Fire Alarms"},
      {"DC Voltage", "Volts", "DCV", 60, 8.0f, 16.0f, "Fire Alarms"},
      {"DC Voltage", "Volts", "DCV", 66, 8.0f, 16.0f, "Fire Alarms"},
      {"Inside Temperature", "degC", "InTem", 22, -20.0f, 70.0f, "Fire Alarms"},
      {"Inside Temperature", "degC", "InTem", 24, -20.0f, 70.0f, "Fire Alarms"},
      {"Inside Temperature", "degC", "InTem", 20, -20.0f, 70.0f, "Fire Alarms"},
      {"Inside Humidity", "PF", "InHum", 21, 0.0f, 100.0f, "Fire Alarms"},
      {"Inside Humidity", "PF", "InHum", 25, 0.0f, 100.0f, "Fire Alarms"},
      {"Inside Humidity", "PF", "InHum", 23, 0.0f, 100.0f, "Fire Alarms"},
      {"Outside Humidity", "PF", "OutHum", 4, 0.0f, 100.0f, "Fire Alarms"},
      {"Brine Water Flow", "liter/hour", "WMBrFl", -1, 0.0f, 5000.0f, "Watermaker"},
      {"Run Time", "hh:mm", "WMRunT", -1, 0.0f, 0.0f, "Watermaker"},
      {"Salinity", "ppm", "WMSal", -1, 0.0f, 2000.0f, "Watermaker"},
      {"System High Pressure", "pounds/square inch", "WMHighPres", -1, 0.0f, 40.0f, "Watermaker"},
      {"Watermaker Operating State", "", "WMState", -1, 0.0f, 0.0f, "Watermaker"},
      {"Post-filter Pressure", "bars", "WMPostPres", -1, 0.0f, 4.0f, "Watermaker"},
      {"Product Water Flow", "liter/hour", "WMProdFl", -1, 0.0f, 5000.0f, "Watermaker"},
      {"Product Water Temperature", "degC", "WMWTemp", -1, 0.0f, 0.0f, "Watermaker"},
      {"DC Current", "Amps", "DCC", 0, 8.0f, 16.0f, "Switches 2"},
      {"DC Voltage", "Volts", "DCV", 0, 8.0f, 16.0f, "Switches 2"},
      {"DC Power", "Kilowatts", "DCP", 0, 0.0f, 1000.0f, "Switches 2"},
      {"Freezer Vorpiek", "degC", "FrzTem", -1, 0.0f, 0.0f, "Switches 2"},
      {"Generratorroom", "degC", "InTem", 2, -20.0f, 70.0f, "Switches 2"},
      {"Inside Temperature", "degC", "InTem", 3, -20.0f, 70.0f, "Switches 2"},
      {"Rate of Turn", "degrees/second", "RoT", -1, -100.0f, 100.0f, "Roll"},
      {"True Wind", "nautical miles/hour", "WiDi", -1, 0.0f, 0.0f, "Roll"},
      {"Apparent Wind", "nautical miles/hour", "WiDi", -1, -300.0f, 300.0f, "Roll"},
      {"Roll", "", "Roll", -1, 0.0f, 0.0f, "Roll"},
      {"Pitch", "", "Pitch", -1, 0.0f, 0.0f, "Roll"},
  };
  return v;
}

}  // namespace cockpit_config
