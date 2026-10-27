// Grad no! – Abfahrtsanzeige mit ÖBB Scotty auf dem "Cheap Yellow Display" (ESP32-2432S028R)
//
// Bedienung am Gerät:
//   Tippen rechts / links   -> nächste / vorige Haltestelle
//   lange drücken (1,2 s)   -> Info (IP-Adresse, WLAN, Version)
//   BOOT-Taste kurz / lang  -> nächste Haltestelle / Info
// Einrichtung: http://grad-no.local  (Benutzer admin, Passwort Standard "gradno")

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>
#include <time.h>
#include "app.h"
#include "display.h"
#include "web.h"

#define BOOT_BTN 0

Board             boards[MAX_STATIONS];
SemaphoreHandle_t dataMutex;
SemaphoreHandle_t netMutex;
volatile bool     otaRunning = false;

static TaskHandle_t  fetchTaskHandle = nullptr;
static volatile bool dirty = true;        // Anzeige neu zeichnen
static volatile bool forceFetch = true;   // sofort abfragen
static bool          infoShown = false;
static uint32_t      wakeUntil = 0;       // Nachtmodus: durch Touch kurz aufwecken
static uint32_t      saveCurrentAt = 0;   // verzögertes Speichern der gewählten Haltestelle

// ---------------------------------------------------------------- Abruf-Task (Core 0)

static Board fetchBuf;   // statisch: nicht auf dem Task-Stack

static void fetchTask(void*) {
  uint32_t nextDue = 0;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
    if (otaRunning || WiFi.status() != WL_CONNECTED) continue;

    Station st;
    uint8_t idx;
    uint16_t interval;
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    bool have = cfg.stationCount > 0;
    idx = cfg.current;
    if (have) st = cfg.stations[idx];
    interval = cfg.intervalSec;
    xSemaphoreGive(dataMutex);
    if (!have) continue;

    if (!forceFetch && (int32_t)(millis() - nextDue) < 0) continue;
    forceFetch = false;

    if (xSemaphoreTake(netMutex, pdMS_TO_TICKS(20000)) != pdTRUE) continue;
    bool ok = hafasFetchBoard(st, fetchBuf);
    xSemaphoreGive(netMutex);

    // bei Fehler nach 20 s erneut versuchen
    nextDue = millis() + (ok ? interval * 1000UL : 20000UL);

    xSemaphoreTake(dataMutex, portMAX_DELAY);
    // nur übernehmen, wenn die Haltestelle inzwischen nicht geändert wurde
    if (idx < cfg.stationCount && strcmp(cfg.stations[idx].id, st.id) == 0) {
      Board& b = boards[idx];
      if (ok) {
        b = fetchBuf;
        b.fetchedAt = time(nullptr);
        b.fetchedMs = millis();
        if (b.fetchedMs == 0) b.fetchedMs = 1;
      } else {
        // alte Abfahrten behalten, nur Fehler merken
        b.ok = false;
        strlcpy(b.error, fetchBuf.error, sizeof(b.error));
      }
    }
    xSemaphoreGive(dataMutex);
    dirty = true;
  }
}

void appRequestRefresh() {
  forceFetch = true;
  if (fetchTaskHandle) xTaskNotifyGive(fetchTaskHandle);
}

void appSelectStation(uint8_t idx) {
  bool stale;
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  if (idx >= cfg.stationCount) { xSemaphoreGive(dataMutex); return; }
  cfg.current = idx;
  stale = boards[idx].fetchedMs == 0 || millis() - boards[idx].fetchedMs > 30000;
  xSemaphoreGive(dataMutex);
  saveCurrentAt = millis() + 5000;
  infoShown = false;
  dirty = true;
  if (stale) appRequestRefresh();
}

void appConfigChanged() {
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  for (int i = 0; i < MAX_STATIONS; i++) memset(&boards[i], 0, sizeof(Board));
  xSemaphoreGive(dataMutex);
  displayApplySettings();
  infoShown = false;
  dirty = true;
  appRequestRefresh();
}

// ---------------------------------------------------------------- Anzeige

static void redraw() {
  static Board   b;
  static Station st;
  uint8_t idx, total;
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  total = cfg.stationCount;
  idx = cfg.current;
  if (total) { b = boards[idx]; st = cfg.stations[idx]; }
  xSemaphoreGive(dataMutex);
  if (!total) {
    String ip = WiFi.localIP().toString();
    displayMessage("Keine Haltestelle", "Im Browser einrichten:",
                   "http://" HOSTNAME ".local", ("oder http://" + ip).c_str());
    return;
  }
  displayBoard(b, st, idx, total);
}

static void showInfo() {
  infoShown = true;
  String ip   = "http://" + WiFi.localIP().toString();
  String wifi = "WLAN: " + WiFi.SSID() + " (" + String(WiFi.RSSI()) + " dBm)";
  String ver  = "Firmware " FW_VERSION "  |  Benutzer: admin";
  displayMessage("Info", ip.c_str(), "http://" HOSTNAME ".local", wifi.c_str(), ver.c_str());
}

static bool isNight() {
  if (!timeValid() || cfg.nightFrom == cfg.nightTo) return false;
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  int h = t.tm_hour;
  if (cfg.nightFrom < cfg.nightTo) return h >= cfg.nightFrom && h < cfg.nightTo;
  return h >= cfg.nightFrom || h < cfg.nightTo;   // über Mitternacht, z.B. 23–6
}

static bool screenDark() {
  return isNight() && cfg.brightNight == 0 && (int32_t)(millis() - wakeUntil) >= 0;
}

static void updateBrightness() {
  static int last = -1;
  int v = cfg.brightDay;
  if (isNight() && (int32_t)(millis() - wakeUntil) >= 0) v = cfg.brightNight;
  if (v != last) { displaySetBrightness(v); last = v; }
}

// ---------------------------------------------------------------- Eingabe

static void stepStation(int dir) {
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  uint8_t total = cfg.stationCount, cur = cfg.current;
  xSemaphoreGive(dataMutex);
  if (total < 2) { dirty = true; return; }
  appSelectStation((cur + total + dir) % total);
}

static void onShortPress(int x) {
  bool wasDark = screenDark();
  wakeUntil = millis() + 30000;
  if (wasDark) return;                       // erster Tipp weckt nur auf
  if (infoShown) { infoShown = false; dirty = true; return; }
  stepStation(x >= 160 ? +1 : -1);
}

static void onLongPress() {
  wakeUntil = millis() + 30000;
  if (infoShown) { infoShown = false; dirty = true; }
  else showInfo();
}

static void handleInput() {
  // ---- Touch
  static bool     down = false, longDone = false;
  static uint32_t downAt = 0, lastSeen = 0;
  static int      downX = 0;
  int x, y;
  bool touching = touchRead(x, y);
  if (touching) {
    lastSeen = millis();
    if (!down) { down = true; longDone = false; downAt = millis(); downX = x; }
  }
  if (down) {
    if (!longDone && millis() - downAt > 1200) { longDone = true; onLongPress(); }
    if (millis() - lastSeen > 80) {          // losgelassen (entprellt)
      down = false;
      if (!longDone) onShortPress(downX);
    }
  }

  // ---- BOOT-Taste
  static bool     bDown = false, bLong = false;
  static uint32_t bAt = 0;
  bool pressed = digitalRead(BOOT_BTN) == LOW;
  if (pressed && !bDown) { bDown = true; bLong = false; bAt = millis(); }
  if (pressed && bDown && !bLong && millis() - bAt > 1200) { bLong = true; onLongPress(); }
  if (!pressed && bDown) {
    bDown = false;
    if (!bLong && millis() - bAt > 30) onShortPress(319);
  }
}

// ---------------------------------------------------------------- Setup / Loop

static bool shouldSaveParams = false;

void setup() {
  Serial.begin(115200);
  pinMode(BOOT_BTN, INPUT_PULLUP);
  dataMutex = xSemaphoreCreateMutex();
  netMutex  = xSemaphoreCreateMutex();
  memset(boards, 0, sizeof(boards));

  configLoad();
  displayInit();
  displayApplySettings();
  displaySetBrightness(cfg.brightDay);
  displayMessage("Grad no!", "Abfahrtsanzeige", "Verbinde mit WLAN ...", nullptr, "v" FW_VERSION);

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);

  WiFiManager wm;
  wm.setTitle("Grad no!");
  wm.setConnectTimeout(20);
  wm.setConfigPortalTimeout(0);   // Portal bleibt offen, bis eingerichtet
  WiFiManagerParameter pPass("webpass", "Passwort für die Weboberfläche (leer lassen = gradno)", "", 32);
  wm.addParameter(&pPass);
  wm.setSaveConfigCallback([]() { shouldSaveParams = true; });
  wm.setAPCallback([](WiFiManager*) {
    displayMessage("WLAN einrichten", "Mit WLAN \"GradNo-Setup\" verbinden",
                   "Passwort: gradno123", "Seite oeffnet sich automatisch,",
                   "sonst http://192.168.4.1");
  });

  if (!wm.autoConnect("GradNo-Setup", "gradno123")) {
    displayMessage("WLAN", "Verbindung fehlgeschlagen", "Neustart ...");
    delay(3000);
    ESP.restart();
  }
  if (shouldSaveParams && strlen(pPass.getValue()) > 0) {
    strlcpy(cfg.webPass, pPass.getValue(), sizeof(cfg.webPass));
    configSave();
  }
  WiFi.setAutoReconnect(true);

  if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.cloudflare.com");

  webBegin();

  String ip = WiFi.localIP().toString();
  displayMessage("Verbunden", ("http://" + ip).c_str(), "http://" HOSTNAME ".local", "Lade Abfahrten ...");

  // auf Uhrzeit warten (max. 5 s), damit Countdown und Filter stimmen
  for (int i = 0; i < 50 && !timeValid(); i++) delay(100);

  xTaskCreatePinnedToCore(fetchTask, "fetch", 16384, nullptr, 1, &fetchTaskHandle, 0);
  appRequestRefresh();
}

void loop() {
  webLoop();
  if (otaRunning) { delay(2); return; }

  handleInput();
  updateBrightness();

  // jede Minute neu zeichnen (Uhr + Countdown)
  static int lastMin = -1;
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  if (t.tm_min != lastMin) { lastMin = t.tm_min; dirty = true; }

  if (saveCurrentAt && (int32_t)(millis() - saveCurrentAt) >= 0) {
    saveCurrentAt = 0;
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    configSave();
    xSemaphoreGive(dataMutex);
  }

  if (dirty && !infoShown) {
    dirty = false;
    redraw();
  }
  delay(5);
}
