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

// Abfahrtstafel zeichnen
void displayBoard(const Board& b, const Station& st, uint8_t idx, uint8_t total);

// Touch abfragen: true + Bildschirmkoordinaten, solange berührt
bool touchRead(int& x, int& y);
