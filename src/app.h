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

void appSelectStation(uint8_t idx);   // Haltestelle wechseln (auch aus der Weboberfläche)
void appConfigChanged();              // nach dem Speichern neuer Einstellungen
void appRequestRefresh();             // sofort neu abfragen
