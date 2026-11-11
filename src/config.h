#pragma once
#include <Arduino.h>

// Bei Release-Builds ersetzt GitHub Actions diesen Wert durch die Version aus dem Git-Tag
#ifndef FW_VERSION
#define FW_VERSION   "1.1.0-dev"
#endif
#define HOSTNAME     "grad-no"
#define MAX_STATIONS 8
#define MIN_INTERVAL 20    // kürzestes erlaubtes Abfrageintervall in Sekunden (schont die Server)
#define MAX_AHEAD    1440  // längste Vorschau in Minuten (24 h)
#define MAX_DEPS     8     // so viele Zeilen passen aufs Display (7 sichtbar)

// Verkehrsmittel-Filter pro Haltestelle
enum ProductFilter : uint8_t { PF_ALL = 0, PF_TRAIN = 1, PF_BUS = 2 };

// Datenquelle pro Haltestelle
enum Provider : uint8_t { PROV_SCOTTY = 0, PROV_WIENER_LINIEN = 1 };

// Farbschema pro Haltestelle
enum Theme : uint8_t { TH_STANDARD = 0, TH_OEBB = 1, TH_WL = 2, TH_CUSTOM = 3 };
#define THEME_COUNT 4

struct Station {
  char     id[16];         // Scotty: HAFAS-ID (z.B. "1290401"), Wiener Linien: DIVA (z.B. "60201040")
  char     name[48];       // Anzeigename (UTF-8)
  uint8_t  provider;       // Provider
  uint8_t  filter;         // ProductFilter (nur Scotty)
  char     direction[48];  // optional: Zielfilter, Komma-getrennt ("Villach, Spittal")
  char     lines[48];      // optional: Linienfilter, Komma-getrennt ("S1, REX 7, 5110")
  uint8_t  walkMin;        // Gehzeit: Abfahrten, die früher abfahren, ausblenden
  uint16_t aheadMin;       // Vorschau: nur Abfahrten in den nächsten x Minuten zeigen (Standard 60)
  uint8_t  theme;          // Theme
  uint32_t colors[4];      // eigene Farben (0xRRGGBB): Hintergrund, Kopfzeile, Schrift, Akzent
  char     eva[32];        // EVA-Nr(n). für die Wagenreihung (live.oebb.at), Komma-getrennt; leer = automatisch suchen
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
  uint16_t autoSec;        // Haltestellen automatisch weiterschalten alle x Sekunden (0 = aus)
  bool     touchMirror;    // Touch links/rechts vertauscht
  bool     invert;         // Farben invertieren (manche CYD-Varianten)
  char     webPass[33];    // Passwort Weboberfläche (Benutzer: admin)
};

extern Settings cfg;

void   configLoad();
void   configSave();
String configToJson();                              // ohne Passwort
bool   configFromJson(const String& json, String& err);
