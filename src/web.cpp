#include "web.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <ArduinoJson.h>
#include "app.h"
#include "display.h"
#include "webpage.h"

static WebServer server(80);
static bool otaAuthOk = false;
static bool otaFailed = false;

static bool auth() {
  if (cfg.webPass[0] == 0) return true;
  if (server.authenticate("admin", cfg.webPass)) return true;
  server.requestAuthentication(BASIC_AUTH, "Grad no!");
  return false;
}

static void sendJson(int code, const String& body) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", body);
}

static void sendError(int code, const String& msg) {
  JsonDocument d;
  d["error"] = msg;
  String s;
  serializeJson(d, s);
  sendJson(code, s);
}

// ---------------------------------------------------------------- Handler

static void handleIndex() {
  if (!auth()) return;
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

static void handleGetConfig() {
  if (!auth()) return;
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  String s = configToJson();
  xSemaphoreGive(dataMutex);
  sendJson(200, s);
}

static void handlePostConfig() {
  if (!auth()) return;
  String body = server.arg("plain");
  String err;
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  bool ok = configFromJson(body, err);
  if (ok) configSave();
  xSemaphoreGive(dataMutex);
  if (!ok) { sendError(400, err); return; }
  appConfigChanged();
  sendJson(200, "{\"ok\":true}");
}

static void handleSearch() {
  if (!auth()) return;
  String q = server.arg("q");
  q.trim();
  if (q.length() < 2) { sendError(400, "Mindestens 2 Zeichen"); return; }
  if (xSemaphoreTake(netMutex, pdMS_TO_TICKS(15000)) != pdTRUE) { sendError(503, "Beschäftigt, bitte nochmal"); return; }
  JsonDocument doc;
  JsonArray arr = doc["results"].to<JsonArray>();
  int n = hafasSearch(q, arr);
  xSemaphoreGive(netMutex);
  if (n < 0) { sendError(502, "Suche fehlgeschlagen (Debug-Seite prüfen)"); return; }
  String s;
  serializeJson(doc, s);
  sendJson(200, s);
}

static void handleBoard() {
  if (!auth()) return;
  JsonDocument doc;
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  int i = server.hasArg("i") ? server.arg("i").toInt() : cfg.current;
  if (i < 0 || i >= cfg.stationCount) {
    xSemaphoreGive(dataMutex);
    sendError(404, "Keine Haltestelle");
    return;
  }
  const Board& b = boards[i];
  doc["index"]   = i;
  doc["current"] = cfg.current;
  doc["name"]    = cfg.stations[i].name;
  doc["station"] = b.stationName;
  doc["ok"]      = b.ok;
  doc["error"]   = b.error;
  doc["fetched"] = b.fetchedMs ? (long)b.fetchedAt : 0;
  doc["age"]     = b.fetchedMs ? (long)((millis() - b.fetchedMs) / 1000) : -1;
  JsonArray arr = doc["deps"].to<JsonArray>();
  for (int k = 0; k < b.count; k++) {
    const Departure& d = b.deps[k];
    JsonObject o = arr.add<JsonObject>();
    o["time"] = d.time;
    o["rt"] = d.rtTime;
    o["line"] = d.line;
    o["dest"] = d.dest;
    o["platform"] = d.platform;
    o["delay"] = d.delay;
    o["cancelled"] = d.cancelled;
    o["when"] = (long)d.when;
  }
  xSemaphoreGive(dataMutex);
  String s;
  serializeJson(doc, s);
  sendJson(200, s);
}

static void handleShow() {
  if (!auth()) return;
  int i = server.arg("i").toInt();
  appSelectStation((uint8_t)i);
  sendJson(200, "{\"ok\":true}");
}

static void handleRefresh() {
  if (!auth()) return;
  appRequestRefresh();
  sendJson(200, "{\"ok\":true}");
}

static void handleStatus() {
  if (!auth()) return;
  JsonDocument doc;
  doc["version"] = FW_VERSION;
  doc["ip"]      = WiFi.localIP().toString();
  doc["host"]    = HOSTNAME ".local";
  doc["ssid"]    = WiFi.SSID();
  doc["rssi"]    = WiFi.RSSI();
  doc["heap"]    = ESP.getFreeHeap();
  doc["minHeap"] = ESP.getMinFreeHeap();
  doc["uptime"]  = (long)(millis() / 1000);
  doc["timeOk"]  = timeValid();
  doc["now"]     = (long)time(nullptr);
  doc["sketch"]  = ESP.getSketchSize();
  doc["free"]    = ESP.getFreeSketchSpace();
  String s;
  serializeJson(doc, s);
  sendJson(200, s);
}

static void handleRaw() {
  if (!auth()) return;
  String s;
  if (xSemaphoreTake(netMutex, pdMS_TO_TICKS(15000)) == pdTRUE) {
    s = hafasLastRaw;
    xSemaphoreGive(netMutex);
  }
  if (!s.length()) s = "(noch keine Abfrage)";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/plain; charset=utf-8", s);
}

static void handlePassword() {
  if (!auth()) return;
  JsonDocument d;
  if (deserializeJson(d, server.arg("plain"))) { sendError(400, "Ungültig"); return; }
  const char* p = d["pass"] | "";
  if (strlen(p) < 4) { sendError(400, "Mindestens 4 Zeichen"); return; }
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  strlcpy(cfg.webPass, p, sizeof(cfg.webPass));
  configSave();
  xSemaphoreGive(dataMutex);
  sendJson(200, "{\"ok\":true}");
}

static void handleReboot() {
  if (!auth()) return;
  sendJson(200, "{\"ok\":true}");
  delay(300);
  ESP.restart();
}

static void handleWifiReset() {
  if (!auth()) return;
  sendJson(200, "{\"ok\":true}");
  delay(300);
  WiFi.disconnect(true, true);   // gespeicherte Zugangsdaten löschen -> Setup-Hotspot
  delay(300);
  ESP.restart();
}

// ---------------------------------------------------------------- OTA

static void handleUpdateDone() {
  if (!auth()) return;
  bool ok = otaAuthOk && !otaFailed && !Update.hasError();
  server.sendHeader("Connection", "close");
  if (ok) {
    server.send(200, "text/plain; charset=utf-8", "OK – Gerät startet neu ...");
    displayMessage("Firmware-Update", "Erfolgreich", "Neustart ...");
    delay(800);
    ESP.restart();
  } else {
    String msg = String("Fehler: ") + (otaAuthOk ? Update.errorString() : "nicht angemeldet");
    server.send(500, "text/plain; charset=utf-8", msg);
    otaRunning = false;
    displayMessage("Firmware-Update", "fehlgeschlagen", msg.c_str());
    delay(2000);
    appConfigChanged();   // Anzeige wiederherstellen
  }
}

static void handleUpdateUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    otaFailed = false;
    otaAuthOk = (cfg.webPass[0] == 0) || server.authenticate("admin", cfg.webPass);
    if (!otaAuthOk) return;
    otaRunning = true;
    // kurz warten, bis eine laufende Abfrage fertig ist
    if (xSemaphoreTake(netMutex, pdMS_TO_TICKS(15000)) == pdTRUE) xSemaphoreGive(netMutex);
    displaySetBrightness(cfg.brightDay);
    displayMessage("Firmware-Update", "wird installiert ...", up.filename.c_str(), "Nicht ausschalten!");
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) { Update.printError(Serial); otaFailed = true; }
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!otaAuthOk || otaFailed) return;
    if (Update.write(up.buf, up.currentSize) != up.currentSize) { Update.printError(Serial); otaFailed = true; }
  } else if (up.status == UPLOAD_FILE_END) {
    if (!otaAuthOk || otaFailed) return;
    if (!Update.end(true)) { Update.printError(Serial); otaFailed = true; }
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    otaFailed = true;
  }
}

// ---------------------------------------------------------------- Setup

void webBegin() {
  server.on("/", HTTP_GET, handleIndex);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_POST, handlePostConfig);
  server.on("/api/search", HTTP_GET, handleSearch);
  server.on("/api/board", HTTP_GET, handleBoard);
  server.on("/api/show", HTTP_POST, handleShow);
  server.on("/api/refresh", HTTP_POST, handleRefresh);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/raw", HTTP_GET, handleRaw);
  server.on("/api/password", HTTP_POST, handlePassword);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  server.on("/api/wifireset", HTTP_POST, handleWifiReset);
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.onNotFound([]() { server.send(404, "text/plain", "Nicht gefunden"); });
  server.begin();
}

void webLoop() { server.handleClient(); }
