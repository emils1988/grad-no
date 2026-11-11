#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "config.h"
#include "hafas.h"

// Gemeinsamer Zustand zwischen Anzeige (loop), Abruf-Task und Webserver
extern Board             boards[MAX_STATIONS];   // Cache: letzte Tafel je Haltestelle
extern SemaphoreHandle_t dataMutex;              // schützt cfg + boards
extern SemaphoreHandle_t netMutex;               // nur eine HTTPS-Verbindung gleichzeitig
extern volatile bool     otaRunning;

void appSelectStation(uint8_t idx, bool remember = true);   // Haltestelle wechseln; remember = als Start-Haltestelle speichern
void appConfigChanged();              // nach dem Speichern neuer Einstellungen
void appRequestRefresh();             // neu abfragen, sobald das Abfrageintervall es erlaubt
uint32_t appSecondsUntilRefresh();    // Sekunden bis zur nächsten erlaubten Abfrage der aktuellen Haltestelle
