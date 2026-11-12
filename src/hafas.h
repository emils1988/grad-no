#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"

// Art des Verkehrsmittels (bestimmt die Farbe des Linien-Badges)
enum DepKind : uint8_t { K_TRAIN = 0, K_SBAHN, K_BUS, K_TRAM, K_METRO };

struct Departure {
  char     time[6];       // geplante Abfahrt HH:MM
  char     rtTime[6];     // Prognose HH:MM ("" = keine)
  char     line[14];      // z.B. "S 1", "Bus 5110", "RJX 66", "U1"
  char     dest[48];      // Ziel (UTF-8)
  char     platform[8];   // Gleis/Steig
  int16_t  delay;         // Minuten Verspätung, -1 = keine Echtzeitdaten
  bool     cancelled;
  uint8_t  kind;          // DepKind
  bool     destChanged;   // Ziel weicht vom Fahrplan ab (z.B. Zug endet vorzeitig)
  time_t   when;          // tatsächliche Abfahrt (Epoch), 0 = unbekannt
  char     date[11];      // Abfahrtsdatum YYYY-MM-DD (für die Wagenreihung)
  uint32_t trainNr;       // Zugnummer für die Wagenreihung, 0 = keine (Bus, Nahverkehr ...)
};

struct Board {
  Departure deps[MAX_DEPS];
  uint8_t   count;
  char      stationName[48];
  time_t    fetchedAt;   // Uhrzeit der letzten erfolgreichen Abfrage
  uint32_t  fetchedMs;   // millis() der letzten erfolgreichen Abfrage, 0 = nie
  bool      ok;          // letzte Abfrage erfolgreich
  char      error[48];
};

// ---- Wagenreihung (live.oebb.at)
#define MAX_WAGONS 24
#define MAX_PARTS  4

enum WagonKind : uint8_t { W_COACH = 0, W_CAB = 1, W_ENGINE = 2 };

struct Wagon {
  uint8_t  kind;          // WagonKind
  uint16_t nr;            // Wagennummer (0 = keine, z.B. Lok)
  uint16_t first, second, business;   // Sitzplätze je Klasse
  uint8_t  bikes, wheelchairs;
  uint8_t  load;          // Auslastung 0 = unbekannt, 1..3
  uint16_t features;      // Bitmaske aus live.oebb.at (64 = Speisewagen, 128 = Ruhezone, 32 = Familie)
  uint8_t  part;          // Index in Formation.parts (Zugteil-Ziel)
};

struct Formation {
  char    title[40];      // z.B. "RJX 66 / RJX19986"
  char    platform[8];
  char    station[48];
  char    parts[MAX_PARTS][28];
  uint8_t partCount;
  Wagon   wagons[MAX_WAGONS];
  uint8_t count;
  bool    loadInfo;       // Auslastung verfügbar
  bool    ok;
  char    error[48];
};

// Abfahrtstafel holen (blockiert 1–3 s). Aufrufer hält netMutex.
bool fetchBoard(const Station& st, Board& out);

// Haltestellensuche; hängt {id, name} an `out` an, liefert Anzahl (<0 = Fehler). Aufrufer hält netMutex.
int searchStations(uint8_t provider, const String& query, JsonArray out);

// EVA-Nummer(n) für live.oebb.at zu einem Bahnhofsnamen suchen (Komma-getrennt, max. 3)
bool findEva(const String& stationName, char* out, size_t outLen);

// Wagenreihung holen; probiert alle EVA-Nummern aus `evaList` (Komma-getrennt)
bool fetchFormation(uint32_t trainNr, const char* evaList, const char* date, Formation& out);

// Letzte Rohantwort (gekürzt) für die Debug-Seite
extern String hafasLastRaw;

// UTF-8 -> ASCII für die eingebauten TFT-Schriften (ä -> ae, č -> c ...)
String translit(const String& in);

// Gilt die Systemzeit als synchronisiert?
bool timeValid();
