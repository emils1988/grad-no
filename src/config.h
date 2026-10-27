#pragma once
#include <Arduino.h>

// Bei Release-Builds setzt GitHub Actions die Version aus dem Git-Tag (-DFW_VERSION=...)
#ifndef FW_VERSION
#define FW_VERSION   "1.0.0-dev"
#endif
#define HOSTNAME     "grad-no"
#define MAX_STATIONS 8
#define MAX_DEPS     8     // so viele Zeilen passen aufs Display (7 sichtbar)

// Verkehrsmittel-Filter pro Haltestelle
enum ProductFilter : uint8_t { PF_ALL = 0, PF_TRAIN = 1, PF_BUS = 2 };

struct Station {
  char    id[16];         // HAFAS-ID (evaId / extId), z.B. "1290401"
  char    name[48];       // Anzeigename (UTF-8)
  uint8_t filter;         // ProductFilter
  char    direction[48];  // optional: Zielfilter, Komma-getrennt ("Villach, Spittal")
  uint8_t walkMin;        // Gehzeit: Abfahrten, die früher abfahren, ausblenden
};

struct Settings {
  Station  stations[MAX_STATIONS];
  uint8_t  stationCount;
  uint8_t  current;        // aktuell angezeigte Haltestelle
  uint16_t intervalSec;    // Abfrageintervall
  uint8_t  brightDay;      // 0..255
  uint8_t  brightNight;    // 0..255 (0 = aus, Touch weckt für 30 s)
  uint8_t  nightFrom;      // Stunde 0..23
  uint8_t  nightTo;        // Stunde 0..23
  uint8_t  rotation;       // 1 = Querformat, 3 = Querformat gedreht
  bool     touchMirror;    // Touch links/rechts vertauscht
  bool     invert;         // Farben invertieren (manche CYD-Varianten)
  char     webPass[33];    // Passwort Weboberfläche (Benutzer: admin)
};

extern Settings cfg;

void   configLoad();
void   configSave();
String configToJson();                              // ohne Passwort
bool   configFromJson(const String& json, String& err);
