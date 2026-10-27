#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"

struct Departure {
  char    time[6];       // geplante Abfahrt HH:MM
  char    rtTime[6];     // Prognose HH:MM ("" = keine)
  char    line[14];      // z.B. "S 1", "Bus 5110", "REX 4812"
  char    dest[48];      // Ziel (UTF-8)
  char    platform[8];   // Gleis/Steig
  int16_t delay;         // Minuten Verspätung, -1 = keine Echtzeitdaten
  bool    cancelled;
  time_t  when;          // tatsächliche Abfahrt (Epoch), 0 = unbekannt
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

// Abfahrtstafel einer Haltestelle holen (blockiert 1–3 s). Aufrufer hält netMutex.
bool hafasFetchBoard(const Station& st, Board& out);

// Haltestellensuche; hängt {id, name} an `out` an, liefert Anzahl. Aufrufer hält netMutex.
int hafasSearch(const String& query, JsonArray out);

// Letzte Rohantwort (gekürzt) für die Debug-Seite
extern String hafasLastRaw;

// UTF-8 -> ASCII für die eingebauten TFT-Schriften (ä -> ae, č -> c ...)
String translit(const String& in);

// Gilt die Systemzeit als synchronisiert?
bool timeValid();
