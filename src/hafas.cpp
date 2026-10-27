#include "hafas.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>

// ÖBB Scotty (HAFAS) – inoffizielle, aber seit Jahren stabile Endpunkte der Fahrplanauskunft.
static const char* BOARD_URL  = "https://fahrplan.oebb.at/bin/stboard.exe/dn";
static const char* SEARCH_URL = "https://fahrplan.oebb.at/bin/ajax-getstop.exe/dn";

// productsFilter: eine Stelle pro HAFAS-Produktklasse
//   0 RJ/RJX/ICE  1 IC/EC  2 NJ/EN  3 D  4 REX/R  5 S-Bahn  6 Bus  7 Schiff
//   8 U-Bahn  9 Straßenbahn  10 –  11 Anrufsammeltaxi  12 Sonstige Züge
static const char* PF_STR[] = {
  "1111111111111",   // alle
  "1111110000001",   // nur Züge
  "0000001011010",   // Bus / Tram / U-Bahn / AST
};

String hafasLastRaw;

bool timeValid() { return time(nullptr) > 1700000000; }

// ---------------------------------------------------------------- Text-Helfer

static bool isValidUtf8(const String& s) {
  const uint8_t* p = (const uint8_t*)s.c_str();
  size_t n = s.length();
  for (size_t i = 0; i < n;) {
    uint8_t c = p[i];
    int extra;
    if (c < 0x80) extra = 0;
    else if ((c & 0xE0) == 0xC0) extra = 1;
    else if ((c & 0xF0) == 0xE0) extra = 2;
    else if ((c & 0xF8) == 0xF0) extra = 3;
    else return false;
    if (extra && i + extra >= n) return false;
    for (int k = 1; k <= extra; k++)
      if ((p[i + k] & 0xC0) != 0x80) return false;
    i += extra + 1;
  }
  return true;
}

static void appendUtf8(String& out, uint32_t cp) {
  if (cp < 0x80) out += (char)cp;
  else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
  else if (cp < 0x10000) {
    out += (char)(0xE0 | (cp >> 12));
    out += (char)(0x80 | ((cp >> 6) & 0x3F));
    out += (char)(0x80 | (cp & 0x3F));
  } else {
    out += (char)(0xF0 | (cp >> 18));
    out += (char)(0x80 | ((cp >> 12) & 0x3F));
    out += (char)(0x80 | ((cp >> 6) & 0x3F));
    out += (char)(0x80 | (cp & 0x3F));
  }
}

static String latin1ToUtf8(const String& s) {
  String out;
  out.reserve(s.length() + 32);
  for (size_t i = 0; i < s.length(); i++) appendUtf8(out, (uint8_t)s[i]);
  return out;
}

// HTML-Entities (&#252; &uuml; &amp; ...) auflösen, Leerzeichen normalisieren
static String cleanText(const char* in) {
  String s(in ? in : "");
  if (s.indexOf('&') >= 0) {
    String out;
    out.reserve(s.length());
    for (size_t i = 0; i < s.length(); i++) {
      if (s[i] == '&') {
        int semi = s.indexOf(';', i);
        if (semi > (int)i && semi - (int)i <= 8) {
          String ent = s.substring(i + 1, semi);
          uint32_t cp = 0;
          if (ent.startsWith("#x") || ent.startsWith("#X")) cp = strtoul(ent.c_str() + 2, nullptr, 16);
          else if (ent.startsWith("#")) cp = strtoul(ent.c_str() + 1, nullptr, 10);
          else if (ent == "amp") cp = '&';
          else if (ent == "quot") cp = '"';
          else if (ent == "apos") cp = '\'';
          else if (ent == "lt") cp = '<';
          else if (ent == "gt") cp = '>';
          else if (ent == "nbsp") cp = ' ';
          else if (ent == "auml") cp = 0xE4;
          else if (ent == "ouml") cp = 0xF6;
          else if (ent == "uuml") cp = 0xFC;
          else if (ent == "Auml") cp = 0xC4;
          else if (ent == "Ouml") cp = 0xD6;
          else if (ent == "Uuml") cp = 0xDC;
          else if (ent == "szlig") cp = 0xDF;
          if (cp) { appendUtf8(out, cp); i = semi; continue; }
        }
      }
      out += s[i];
    }
    s = out;
  }
  // Mehrfach-Leerzeichen zusammenfassen
  String out;
  out.reserve(s.length());
  bool lastSpace = true;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      if (!lastSpace) out += ' ';
      lastSpace = true;
    } else { out += c; lastSpace = false; }
  }
  out.trim();
  return out;
}

String translit(const String& in) {
  String out;
  out.reserve(in.length() + 8);
  const uint8_t* p = (const uint8_t*)in.c_str();
  size_t n = in.length();
  for (size_t i = 0; i < n;) {
    uint32_t cp;
    uint8_t c = p[i];
    if (c < 0x80) { cp = c; i += 1; }
    else if ((c & 0xE0) == 0xC0 && i + 1 < n) { cp = ((c & 0x1F) << 6) | (p[i + 1] & 0x3F); i += 2; }
    else if ((c & 0xF0) == 0xE0 && i + 2 < n) { cp = ((c & 0x0F) << 12) | ((p[i + 1] & 0x3F) << 6) | (p[i + 2] & 0x3F); i += 3; }
    else if ((c & 0xF8) == 0xF0 && i + 3 < n) { cp = '?'; i += 4; }
    else { cp = c; i += 1; }  // kaputtes Byte, als Latin-1 behandeln

    if (cp < 0x80) { out += (char)cp; continue; }
    switch (cp) {
      case 0xE4: out += "ae"; break;  case 0xC4: out += "Ae"; break;
      case 0xF6: out += "oe"; break;  case 0xD6: out += "Oe"; break;
      case 0xFC: out += "ue"; break;  case 0xDC: out += "Ue"; break;
      case 0xDF: out += "ss"; break;
      case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE5: out += 'a'; break;
      case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC5: out += 'A'; break;
      case 0xE8: case 0xE9: case 0xEA: case 0xEB: case 0x11B: out += 'e'; break;
      case 0xC8: case 0xC9: case 0xCA: case 0xCB: case 0x11A: out += 'E'; break;
      case 0xEC: case 0xED: case 0xEE: case 0xEF: out += 'i'; break;
      case 0xCC: case 0xCD: case 0xCE: case 0xCF: out += 'I'; break;
      case 0xF2: case 0xF3: case 0xF4: case 0xF5: out += 'o'; break;
      case 0xD2: case 0xD3: case 0xD4: case 0xD5: out += 'O'; break;
      case 0xF9: case 0xFA: case 0xFB: case 0x16F: out += 'u'; break;
      case 0xD9: case 0xDA: case 0xDB: case 0x16E: out += 'U'; break;
      case 0xE7: case 0x107: case 0x10D: out += 'c'; break;   // ç ć č
      case 0xC7: case 0x106: case 0x10C: out += 'C'; break;
      case 0x161: out += 's'; break;  case 0x160: out += 'S'; break;  // š Š
      case 0x17E: out += 'z'; break;  case 0x17D: out += 'Z'; break;  // ž Ž
      case 0x111: out += 'd'; break;  case 0x110: out += 'D'; break;  // đ Đ
      case 0x148: out += 'n'; break;  case 0x147: out += 'N'; break;
      case 0x159: out += 'r'; break;  case 0x158: out += 'R'; break;
      case 0xF1: out += 'n'; break;   case 0xD1: out += 'N'; break;
      case 0xB4: case 0x2019: case 0x2018: out += '\''; break;
      case 0x2013: case 0x2014: out += '-'; break;
      case 0xA0: out += ' '; break;
      default: out += '?'; break;
    }
  }
  return out;
}

static String urlEncode(const String& s, bool latin1) {
  String src = s;
  if (latin1) {
    // UTF-8 -> Latin-1 (Zeichen > U+00FF werden zu '?')
    String l;
    const uint8_t* p = (const uint8_t*)s.c_str();
    size_t n = s.length();
    for (size_t i = 0; i < n;) {
      uint8_t c = p[i];
      if (c < 0x80) { l += (char)c; i++; }
      else if ((c & 0xE0) == 0xC0 && i + 1 < n) {
        uint32_t cp = ((c & 0x1F) << 6) | (p[i + 1] & 0x3F);
        l += (char)(cp <= 0xFF ? cp : '?');
        i += 2;
      } else {
        l += '?';
        i++;
        while (i < n && (p[i] & 0xC0) == 0x80) i++;
      }
    }
    src = l;
  }
  static const char* hex = "0123456789ABCDEF";
  String out;
  for (size_t i = 0; i < src.length(); i++) {
    uint8_t c = (uint8_t)src[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char)c;
    else if (c == ' ') out += "%20";
    else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
  }
  return out;
}

static bool hasNonAscii(const String& s) {
  for (size_t i = 0; i < s.length(); i++) if ((uint8_t)s[i] >= 0x80) return true;
  return false;
}

// ---------------------------------------------------------------- HTTPS

static String httpsGet(const String& url, int& code) {
  WiFiClientSecure client;
  // Fahrplandaten sind öffentlich und es werden keine Zugangsdaten übertragen,
  // daher ohne Zertifikatsprüfung (spart Speicher und Ärger beim Zertifikatswechsel).
  client.setInsecure();
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(10000);
  http.setUserAgent("Mozilla/5.0 (ESP32; GradNo)");
  if (!http.begin(client, url)) { code = -100; return String(); }
  code = http.GET();
  String body;
  if (code == 200) body = http.getString();
  http.end();
  return body;
}

// ---------------------------------------------------------------- Abfahrtstafel

static bool isBusLike(const String& pr) {
  String p = pr;
  p.toLowerCase();
  if (p.startsWith("bus") || p.startsWith("ast") || p.startsWith("tram") ||
      p.startsWith("str") || p.startsWith("o-bus") || p.startsWith("obus") ||
      p.startsWith("nachtbus") || p.startsWith("citybus") || p.startsWith("rufbus"))
    return true;
  // U-Bahn: "U1", "U 1"
  if (p.length() >= 2 && p[0] == 'u' && (isdigit((uint8_t)p[1]) || p[1] == ' ')) return true;
  return false;
}

static bool productMatches(const String& pr, uint8_t filter) {
  if (filter == PF_TRAIN) return !isBusLike(pr);
  if (filter == PF_BUS) return isBusLike(pr);
  return true;
}

static bool directionMatches(const String& dest, const char* filter) {
  if (!filter || !filter[0]) return true;
  String d = dest;
  d.toLowerCase();
  String f(filter);
  int start = 0;
  while (start <= (int)f.length()) {
    int comma = f.indexOf(',', start);
    if (comma < 0) comma = f.length();
    String part = f.substring(start, comma);
    part.trim();
    part.toLowerCase();
    if (part.length() && d.indexOf(part) >= 0) return true;
    start = comma + 1;
  }
  return false;
}

// "dd.mm.yyyy" oder "dd.mm.yy" + "HH:MM" -> Epoch (lokale Zeit)
static time_t toEpoch(const char* da, const char* hm) {
  int d = 0, m = 0, y = 0, hh = 0, mm = 0;
  if (sscanf(da, "%d.%d.%d", &d, &m, &y) != 3) return 0;
  if (sscanf(hm, "%d:%d", &hh, &mm) != 2) return 0;
  if (y < 100) y += 2000;
  struct tm t = {};
  t.tm_year = y - 1900; t.tm_mon = m - 1; t.tm_mday = d;
  t.tm_hour = hh; t.tm_min = mm; t.tm_sec = 0; t.tm_isdst = -1;
  return mktime(&t);
}

bool hafasFetchBoard(const Station& st, Board& out) {
  memset(&out, 0, sizeof(out));
  if (WiFi.status() != WL_CONNECTED) { strlcpy(out.error, "Kein WLAN", sizeof(out.error)); return false; }

  String url = BOARD_URL;
  url += "?L=vs_scotty.vs_liveticker&evaId=";
  url += st.id;
  url += "&boardType=dep&productsFilter=";
  url += PF_STR[st.filter <= 2 ? st.filter : 0];
  url += "&dirInput=&tickerID=dep&start=yes&eqstops=false&outputMode=tickerDataOnly&showJourneys=";
  // mehr holen als angezeigt wird, weil danach noch gefiltert wird
  url += String(st.direction[0] || st.walkMin ? 30 : 15);

  int code = 0;
  String body = httpsGet(url, code);
  if (code != 200) {
    snprintf(out.error, sizeof(out.error), code < 0 ? "Verbindung fehlgeschlagen (%d)" : "HTTP-Fehler %d", code);
    hafasLastRaw = String("HTTP ") + code + "\n" + url;
    return false;
  }
  hafasLastRaw = url + "\n\n" + body.substring(0, 4000);

  // Antwort sieht aus wie:  journeysObj = {...};
  int a = body.indexOf('{');
  int b = body.lastIndexOf('}');
  if (a < 0 || b <= a) { strlcpy(out.error, "Antwort unbekannt", sizeof(out.error)); return false; }
  String json = body.substring(a, b + 1);
  body = String();  // Speicher freigeben
  if (!isValidUtf8(json)) json = latin1ToUtf8(json);

  JsonDocument filter;
  filter["stationName"] = true;
  JsonObject jf = filter["journey"].add<JsonObject>();
  jf["ti"] = true;        // geplante Zeit
  jf["da"] = true;        // Datum
  jf["pr"] = true;        // Produkt/Linie
  jf["st"] = true;        // Ziel
  jf["lastStop"] = true;  // Endhalt (Fallback)
  jf["tr"] = true;        // Gleis/Steig
  jf["rt"] = true;        // Echtzeit: false oder {status, dlm, dlt}

  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, json, DeserializationOption::Filter(filter));
  json = String();
  if (e) { snprintf(out.error, sizeof(out.error), "JSON: %s", e.c_str()); return false; }

  strlcpy(out.stationName, cleanText(doc["stationName"] | st.name).c_str(), sizeof(out.stationName));

  bool tv = timeValid();
  time_t now = time(nullptr);

  for (JsonObject j : doc["journey"].as<JsonArray>()) {
    if (out.count >= MAX_DEPS) break;
    String pr = cleanText(j["pr"] | "");
    String dest = cleanText(j["st"] | "");
    if (dest.length() == 0) dest = cleanText(j["lastStop"] | "");
    if (!productMatches(pr, st.filter)) continue;
    if (!directionMatches(dest, st.direction)) continue;

    Departure& d = out.deps[out.count];
    memset(&d, 0, sizeof(d));
    strlcpy(d.time, j["ti"] | "", sizeof(d.time));
    strlcpy(d.line, pr.c_str(), sizeof(d.line));
    strlcpy(d.dest, dest.c_str(), sizeof(d.dest));
    strlcpy(d.platform, cleanText(j["tr"] | "").c_str(), sizeof(d.platform));
    d.delay = -1;

    JsonVariant rt = j["rt"];
    if (rt.is<JsonObject>()) {
      d.delay = 0;
      String status = cleanText(rt["status"] | "");
      status.toLowerCase();
      if (status.indexOf("ausfall") >= 0 || status.indexOf("entf") >= 0 || status.indexOf("cancel") >= 0)
        d.cancelled = true;
      const char* dlt = rt["dlt"] | "";
      if (strlen(dlt) == 5) strlcpy(d.rtTime, dlt, sizeof(d.rtTime));
      JsonVariant dlm = rt["dlm"];
      if (dlm.is<int>()) d.delay = dlm.as<int>();
      else if (dlm.is<const char*>()) d.delay = atoi(dlm.as<const char*>());
    }

    const char* da = j["da"] | "";
    time_t planned = toEpoch(da, d.time);
    time_t when = planned;
    if (planned && d.rtTime[0]) {
      when = toEpoch(da, d.rtTime);
      if (when && when < planned - 12 * 3600) when += 24 * 3600;  // Verspätung über Mitternacht
      if (d.delay <= 0 && when > planned) d.delay = (when - planned) / 60;
    } else if (planned && d.delay > 0) {
      when = planned + d.delay * 60;
    }
    d.when = when;

    if (tv && when) {
      if (when < now - 30) continue;                                       // schon weg
      if (!d.cancelled && st.walkMin && when < now + st.walkMin * 60) continue;  // nicht mehr erreichbar
    }
    out.count++;
  }

  out.ok = true;
  return true;
}

// ---------------------------------------------------------------- Haltestellensuche

static int parseSearch(const String& body, JsonArray out) {
  int a = body.indexOf('{');
  int b = body.lastIndexOf('}');
  if (a < 0 || b <= a) return 0;
  String json = body.substring(a, b + 1);
  if (!isValidUtf8(json)) json = latin1ToUtf8(json);

  JsonDocument filter;
  JsonObject sf = filter["suggestions"].add<JsonObject>();
  sf["value"] = true;
  sf["extId"] = true;
  sf["id"] = true;
  sf["type"] = true;

  JsonDocument doc;
  if (deserializeJson(doc, json, DeserializationOption::Filter(filter))) return 0;

  int n = 0;
  for (JsonObject s : doc["suggestions"].as<JsonArray>()) {
    String type = s["type"].is<const char*>() ? String(s["type"].as<const char*>()) : String(s["type"].as<int>());
    if (type != "1") continue;  // nur Haltestellen (keine Adressen/POIs)
    String id = s["extId"] | "";
    if (id.length() == 0) {
      String full = s["id"] | "";
      int p = full.indexOf("@L=");
      if (p >= 0) {
        int q = full.indexOf('@', p + 3);
        id = full.substring(p + 3, q < 0 ? full.length() : q);
      }
    }
    if (id.length() == 0) continue;
    JsonObject o = out.add<JsonObject>();
    o["id"] = id;
    o["name"] = cleanText(s["value"] | "");
    if (++n >= 12) break;
  }
  return n;
}

int hafasSearch(const String& query, JsonArray out) {
  if (WiFi.status() != WL_CONNECTED) return -1;
  // Erst Latin-1 (klassische HAFAS-Kodierung), bei Umlauten ohne Treffer nochmal als UTF-8
  for (int attempt = 0; attempt < 2; attempt++) {
    bool latin1 = (attempt == 0);
    if (!latin1 && !hasNonAscii(query)) break;
    String url = SEARCH_URL;
    url += "?REQ0JourneyStopsS0A=1&REQ0JourneyStopsB=12&S=";
    url += urlEncode(query, latin1);
    url += "?&js=true&";
    int code = 0;
    String body = httpsGet(url, code);
    hafasLastRaw = url + "\n\n" + body.substring(0, 4000);
    if (code != 200) return -1;
    int n = parseSearch(body, out);
    if (n > 0) return n;
  }
  return 0;
}
