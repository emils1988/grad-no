#include "config.h"
#include <Preferences.h>
#include <ArduinoJson.h>

Settings cfg;
static Preferences prefs;
// NVS-Namespace bleibt "abfahrt" (alter Projektname), damit Einstellungen bei Updates erhalten bleiben

static void setDefaults() {
  memset(&cfg, 0, sizeof(cfg));
  cfg.intervalSec = 60;
  cfg.brightDay   = 200;
  cfg.brightNight = 15;
  cfg.nightFrom   = 23;
  cfg.nightTo     = 6;
  cfg.autoSec     = 120;
  cfg.rotation    = 1;
  strlcpy(cfg.webPass, "gradno", sizeof(cfg.webPass));
  // Beispiel, damit nach dem ersten Start sofort etwas angezeigt wird
  cfg.stationCount = 1;
  strlcpy(cfg.stations[0].id, "1290401", sizeof(cfg.stations[0].id));
  strlcpy(cfg.stations[0].name, "Wien Hbf", sizeof(cfg.stations[0].name));
  cfg.stations[0].aheadMin = 60;
}

// Liest Einstellungen aus einem JSON-Dokument. Fehlende Felder behalten den aktuellen Wert.
static void applyDoc(JsonDocument& doc) {
  if (doc["stations"].is<JsonArray>()) {
    JsonArray arr = doc["stations"].as<JsonArray>();
    uint8_t n = 0;
    for (JsonObject s : arr) {
      if (n >= MAX_STATIONS) break;
      const char* id = s["id"] | "";
      if (!id[0]) continue;
      Station& st = cfg.stations[n];
      memset(&st, 0, sizeof(st));
      strlcpy(st.id, id, sizeof(st.id));
      strlcpy(st.name, s["name"] | id, sizeof(st.name));
      st.provider = (uint8_t)constrain((int)(s["provider"] | 0), 0, 1);
      st.filter  = (uint8_t)constrain((int)(s["filter"] | 0), 0, 2);
      strlcpy(st.direction, s["direction"] | "", sizeof(st.direction));
      strlcpy(st.lines, s["lines"] | "", sizeof(st.lines));
      st.walkMin = (uint8_t)constrain((int)(s["walk"] | 0), 0, 60);
      st.aheadMin = (uint16_t)constrain((int)(s["ahead"] | 60), 10, MAX_AHEAD);
      st.theme   = (uint8_t)constrain((int)(s["theme"] | 0), 0, THEME_COUNT - 1);
      static const uint32_t defColors[4] = { 0x000000, 0x161822, 0xFFFFFF, 0xFFB000 };
      JsonArray col = s["colors"].as<JsonArray>();
      for (int k = 0; k < 4; k++) {
        const char* c = col.isNull() ? nullptr : col[k].as<const char*>();
        st.colors[k] = (c && c[0] == '#' && strlen(c) == 7) ? strtoul(c + 1, nullptr, 16) : defColors[k];
      }
      strlcpy(st.eva, s["eva"] | "", sizeof(st.eva));
      n++;
    }
    cfg.stationCount = n;
  }
  cfg.current     = (uint8_t)(doc["current"]     | (int)cfg.current);
  cfg.intervalSec = (uint16_t)constrain((int)(doc["interval"] | (int)cfg.intervalSec), MIN_INTERVAL, 600);
  cfg.brightDay   = (uint8_t)constrain((int)(doc["brightDay"]   | (int)cfg.brightDay), 5, 255);
  cfg.brightNight = (uint8_t)constrain((int)(doc["brightNight"] | (int)cfg.brightNight), 0, 255);
  cfg.nightFrom   = (uint8_t)constrain((int)(doc["nightFrom"]   | (int)cfg.nightFrom), 0, 23);
  cfg.nightTo     = (uint8_t)constrain((int)(doc["nightTo"]     | (int)cfg.nightTo), 0, 23);
  { int a = doc["autoSec"] | (int)cfg.autoSec; cfg.autoSec = a <= 0 ? 0 : (uint16_t)constrain(a, 30, 3600); }
  int rot = doc["rotation"] | (int)cfg.rotation;
  cfg.rotation    = (rot == 3) ? 3 : 1;
  cfg.touchMirror = doc["touchMirror"] | cfg.touchMirror;
  cfg.invert      = doc["invert"] | cfg.invert;
  if (doc["webPass"].is<const char*>()) {
    const char* p = doc["webPass"];
    strlcpy(cfg.webPass, p, sizeof(cfg.webPass));
  }
  if (cfg.stationCount == 0) cfg.current = 0;
  else if (cfg.current >= cfg.stationCount) cfg.current = 0;
}

// Ab v1.1 als Blob gespeichert (Strings sind im NVS auf ~4000 Byte begrenzt).
// Ältere Versionen haben "cfg" als String abgelegt – wird beim Laden übernommen.
void configLoad() {
  setDefaults();
  prefs.begin("abfahrt", true);
  String json;
  size_t len = prefs.isKey("cfgb") ? prefs.getBytesLength("cfgb") : 0;
  if (len > 0) {
    char* buf = (char*)malloc(len + 1);
    if (buf) {
      prefs.getBytes("cfgb", buf, len);
      buf[len] = 0;
      json = buf;
      free(buf);
    }
  } else {
    json = prefs.getString("cfg", "");
  }
  prefs.end();
  if (json.length() == 0) return;
  JsonDocument doc;
  if (deserializeJson(doc, json)) return;  // defekt -> Standardwerte
  applyDoc(doc);
}

static String toJson(bool withPass) {
  JsonDocument doc;
  JsonArray arr = doc["stations"].to<JsonArray>();
  for (uint8_t i = 0; i < cfg.stationCount; i++) {
    const Station& st = cfg.stations[i];
    JsonObject o = arr.add<JsonObject>();
    o["id"] = st.id;
    o["name"] = st.name;
    o["filter"] = st.filter;
    o["provider"] = st.provider;
    o["direction"] = st.direction;
    o["lines"] = st.lines;
    o["walk"] = st.walkMin;
    o["ahead"] = st.aheadMin;
    o["theme"] = st.theme;
    JsonArray col = o["colors"].to<JsonArray>();
    for (int k = 0; k < 4; k++) {
      char buf[8];
      snprintf(buf, sizeof(buf), "#%06lX", (unsigned long)(st.colors[k] & 0xFFFFFF));
      col.add(buf);
    }
    o["eva"] = st.eva;
  }
  doc["current"]     = cfg.current;
  doc["interval"]    = cfg.intervalSec;
  doc["brightDay"]   = cfg.brightDay;
  doc["brightNight"] = cfg.brightNight;
  doc["nightFrom"]   = cfg.nightFrom;
  doc["nightTo"]     = cfg.nightTo;
  doc["autoSec"]     = cfg.autoSec;
  doc["rotation"]    = cfg.rotation;
  doc["touchMirror"] = cfg.touchMirror;
  doc["invert"]      = cfg.invert;
  if (withPass) doc["webPass"] = cfg.webPass;
  String out;
  serializeJson(doc, out);
  return out;
}

void configSave() {
  String json = toJson(true);
  prefs.begin("abfahrt", false);
  prefs.putBytes("cfgb", json.c_str(), json.length());
  if (prefs.isKey("cfg")) prefs.remove("cfg");
  prefs.end();
}

String configToJson() { return toJson(false); }

bool configFromJson(const String& json, String& err) {
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, json);
  if (e) { err = String("JSON: ") + e.c_str(); return false; }
  applyDoc(doc);
  return true;
}
