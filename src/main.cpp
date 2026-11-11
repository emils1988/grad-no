// Grad no! – Abfahrtsanzeige mit ÖBB Scotty auf dem "Cheap Yellow Display" (ESP32-2432S028R)
//
// Bedienung am Gerät:
//   Tippen rechts / links   -> nächste / vorige Haltestelle
//   Doppeltipp auf einen Zug -> Wagenreihung (ÖBB-Fernverkehr, Daten von live.oebb.at)
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
static bool          detailShown = false;  // Wagenreihung wird angezeigt
static uint32_t      detailUntil = 0;
static uint32_t      wakeUntil = 0;       // Nachtmodus: durch Touch kurz aufwecken
static uint32_t      saveCurrentAt = 0;   // verzögertes Speichern der gewählten Haltestelle

// ---------------------------------------------------------------- Abruf-Task (Core 0)

static Board fetchBuf;   // statisch: nicht auf dem Task-Stack

// Letzter Abrufversuch je Haltestelle. Eine Haltestelle wird frühestens nach Ablauf des
// Abfrageintervalls wieder abgefragt – auch beim Umschalten, "Jetzt abfragen" oder nach Fehlern.
static uint32_t lastAttempt[MAX_STATIONS];
static char     lastAttemptId[MAX_STATIONS][16];

// Sekunden bis zur nächsten erlaubten Abfrage (0 = sofort). Aufrufer hält dataMutex.
static uint32_t secondsUntilAllowed(uint8_t idx) {
  if (idx >= cfg.stationCount) return 0;
  const Station& st = cfg.stations[idx];
  if (lastAttempt[idx] == 0 || strcmp(lastAttemptId[idx], st.id) != 0) return 0;
  uint32_t interval = max((uint32_t)MIN_INTERVAL, (uint32_t)cfg.intervalSec) * 1000UL;
  uint32_t since = millis() - lastAttempt[idx];
  return since >= interval ? 0 : (interval - since + 999) / 1000;
}

uint32_t appSecondsUntilRefresh() {
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  uint32_t s = secondsUntilAllowed(cfg.current);
  xSemaphoreGive(dataMutex);
  return s;
}

static void fetchTask(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
    if (otaRunning || WiFi.status() != WL_CONNECTED) continue;

    Station st;
    uint8_t idx;
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    bool have = cfg.stationCount > 0;
    idx = cfg.current;
    if (have) st = cfg.stations[idx];
    bool due = have && secondsUntilAllowed(idx) == 0;
    xSemaphoreGive(dataMutex);
    if (!due) continue;
    forceFetch = false;

    if (xSemaphoreTake(netMutex, pdMS_TO_TICKS(20000)) != pdTRUE) continue;
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    lastAttempt[idx] = millis();
    if (lastAttempt[idx] == 0) lastAttempt[idx] = 1;
    strlcpy(lastAttemptId[idx], st.id, sizeof(lastAttemptId[idx]));
    xSemaphoreGive(dataMutex);
    bool ok = fetchBoard(st, fetchBuf);
    xSemaphoreGive(netMutex);

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

static uint32_t lastSwitchMs = 0;   // letzter Wechsel/letzte Bedienung (für das automatische Weiterschalten)

void appSelectStation(uint8_t idx, bool remember) {
  bool stale;
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  if (idx >= cfg.stationCount) { xSemaphoreGive(dataMutex); return; }
  cfg.current = idx;
  stale = secondsUntilAllowed(idx) == 0;   // sonst bleibt die zwischengespeicherte Tafel
  xSemaphoreGive(dataMutex);
  if (remember) saveCurrentAt = millis() + 5000;   // automatisches Weiterschalten schreibt nicht in den Flash
  lastSwitchMs = millis();
  infoShown = false;
  detailShown = false;
  dirty = true;
  if (stale) appRequestRefresh();
}

void appConfigChanged() {
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  for (int i = 0; i < MAX_STATIONS; i++) {
    bool same = i < cfg.stationCount && lastAttempt[i] && !strcmp(lastAttemptId[i], cfg.stations[i].id);
    if (!same) {
      // andere Haltestelle auf diesem Platz: alte Tafel verwerfen, sofort abfragen
      memset(&boards[i], 0, sizeof(Board));
      lastAttempt[i] = 0;
    } else if (millis() - lastAttempt[i] >= MIN_INTERVAL * 1000UL) {
      // gleiche Haltestelle, Filter evtl. geändert: neu abfragen, aber nie öfter als alle MIN_INTERVAL Sekunden
      lastAttempt[i] = 0;
    }
  }
  xSemaphoreGive(dataMutex);
  displayApplySettings();
  infoShown = false;
  detailShown = false;
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

static void closeOverlays() {
  infoShown = false;
  detailShown = false;
  dirty = true;
}

static void onShortPress(int x) {
  lastSwitchMs = millis();   // nach Bedienung erst wieder nach der vollen Zeit automatisch weiter
  bool wasDark = screenDark();
  wakeUntil = millis() + 30000;
  if (wasDark) return;                       // erster Tipp weckt nur auf
  if (infoShown || detailShown) { closeOverlays(); return; }
  stepStation(x >= 160 ? +1 : -1);
}

// Doppeltipp auf eine Zeile: Wagenreihung des Zuges anzeigen
static Formation formation;   // statisch: nicht auf dem Stack
static void showFormation(int y) {
  int rowIdx = displayRowAt(y);
  Departure dep;
  Station st;
  String stationName;
  uint8_t idx;
  bool have = false;
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  idx = cfg.current;
  if (idx < cfg.stationCount && rowIdx >= 0 && rowIdx < boards[idx].count) {
    dep = boards[idx].deps[rowIdx];
    st = cfg.stations[idx];
    stationName = boards[idx].stationName[0] ? String(boards[idx].stationName) : String(st.name);
    have = true;
  }
  xSemaphoreGive(dataMutex);
  if (!have) return;

  detailShown = true;
  detailUntil = millis() + 60000;
  memset(&formation, 0, sizeof(formation));
  if (st.provider != PROV_SCOTTY || dep.trainNr == 0) {
    strlcpy(formation.error, "Wagenreihung nur fuer OeBB-Fernverkehr", sizeof(formation.error));
    displayFormation(formation, dep, st);
    return;
  }
  displayFormationLoading(dep, st);
  if (xSemaphoreTake(netMutex, pdMS_TO_TICKS(20000)) != pdTRUE) {
    strlcpy(formation.error, "Netzwerk beschaeftigt", sizeof(formation.error));
    displayFormation(formation, dep, st);
    return;
  }
  // EVA-Nummer für live.oebb.at einmalig suchen und merken
  if (st.eva[0] == 0) {
    char eva[sizeof(st.eva)];
    if (findEva(stationName, eva, sizeof(eva))) {
      strlcpy(st.eva, eva, sizeof(st.eva));
      xSemaphoreTake(dataMutex, portMAX_DELAY);
      if (idx < cfg.stationCount && !strcmp(cfg.stations[idx].id, st.id)) {
        strlcpy(cfg.stations[idx].eva, eva, sizeof(cfg.stations[idx].eva));
        configSave();
      }
      xSemaphoreGive(dataMutex);
    }
  }
  if (st.eva[0] == 0) strlcpy(formation.error, "Bahnhof bei OeBB Live nicht gefunden", sizeof(formation.error));
  else fetchFormation(dep.trainNr, st.eva, dep.date, formation);
  xSemaphoreGive(netMutex);
  if (!detailShown) return;   // inzwischen geschlossen
  displayFormation(formation, dep, st);
  detailUntil = millis() + 60000;
}

static void onDoubleTap(int x, int y) {
  lastSwitchMs = millis();
  bool wasDark = screenDark();
  wakeUntil = millis() + 30000;
  if (wasDark) return;
  if (infoShown || detailShown) { closeOverlays(); return; }
  showFormation(y);
}

static void onLongPress() {
  wakeUntil = millis() + 30000;
  if (infoShown || detailShown) closeOverlays();
  else showInfo();
}

#define DOUBLE_TAP_MS 350

static void handleInput() {
  // ---- Touch: Tipp, Doppeltipp, lange drücken
  static bool     down = false, longDone = false;
  static uint32_t downAt = 0, lastSeen = 0;
  static int      downX = 0, downY = 0;
  static bool     tapPending = false;     // erster Tipp wartet auf einen möglichen zweiten
  static uint32_t tapAt = 0;
  static int      tapX = 0, tapY = 0;
  int x, y;
  bool touching = touchRead(x, y);
  if (touching) {
    lastSeen = millis();
    if (!down) { down = true; longDone = false; downAt = millis(); downX = x; downY = y; }
  }
  if (down) {
    if (!longDone && millis() - downAt > 1200) { longDone = true; tapPending = false; onLongPress(); }
    if (millis() - lastSeen > 80) {          // losgelassen (entprellt)
      down = false;
      if (!longDone) {
        if (tapPending && millis() - tapAt < DOUBLE_TAP_MS + 150 &&
            abs(downX - tapX) < 40 && abs(downY - tapY) < 30) {
          tapPending = false;
          onDoubleTap(tapX, tapY);
        } else {
          tapPending = true;
          tapAt = millis();
          tapX = downX;
          tapY = downY;
        }
      }
    }
  }
  if (tapPending && !down && millis() - tapAt > DOUBLE_TAP_MS) {
    tapPending = false;
    onShortPress(tapX);
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

  if (detailShown && (int32_t)(millis() - detailUntil) >= 0) closeOverlays();

  // Haltestellen automatisch weiterschalten (nicht während Info/Wagenreihung oder bei dunklem Display)
  if (cfg.autoSec && cfg.stationCount > 1 && !infoShown && !detailShown && !screenDark() &&
      millis() - lastSwitchMs >= cfg.autoSec * 1000UL) {
    uint8_t next = (cfg.current + 1) % cfg.stationCount;
    appSelectStation(next, false);
  }

  if (dirty && !infoShown && !detailShown) {
    dirty = false;
    redraw();
  }
  delay(5);
}
