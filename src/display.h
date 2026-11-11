#pragma once
#include <Arduino.h>
#include "config.h"
#include "hafas.h"

void displayInit();
void displayApplySettings();                 // Rotation, Invertierung
void displaySetBrightness(uint8_t value);    // 0..255

// Vollbild-Meldung (Start, WLAN-Setup, Update ...)
void displayMessage(const char* title, const char* l1 = nullptr,
                    const char* l2 = nullptr, const char* l3 = nullptr,
                    const char* l4 = nullptr);

// Abfahrtstafel zeichnen (Farbschema der Haltestelle)
void displayBoard(const Board& b, const Station& st, uint8_t idx, uint8_t total);

// Zeile der Abfahrtstafel unter der y-Koordinate (-1 = keine)
int displayRowAt(int y);

// Wagenreihung: Ladeanzeige und Ergebnis
void displayFormationLoading(const Departure& d, const Station& st);
void displayFormation(const Formation& f, const Departure& d, const Station& st);

// Touch abfragen: true + Bildschirmkoordinaten, solange berührt
bool touchRead(int& x, int& y);
