#include "hafas.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>

// ÖBB Scotty (HAFAS) – inoffizielle, aber seit Jahren stabile Endpunkte der Fahrplanauskunft.
// Scotty enthält ganz Österreich (inkl. Wiener Linien, Graz Linien, Postbus ...).
static const char* BOARD_URL  = "https://fahrplan.oebb.at/bin/stboard.exe/dn";
static const char* SEARCH_URL = "https://fahrplan.oebb.at/bin/ajax-getstop.exe/dn";
// Wiener Linien Open Data (CC BY 4.0, Datenquelle: Stadt Wien – data.wien.gv.at), ohne API-Key
static const char* WL_MONITOR_URL = "https://www.wienerlinien.at/ogd_realtime/monitor?diva=";
static const char* WL_STOPS_URL   = "https://www.wienerlinien.at/ogd_realtime/doku/ogd/wienerlinien-ogd-haltestellen.csv";
// ÖBB Live (Wagenreihung) – inoffiziell
static const char* LIVE_INFO_URL     = "https://live.oebb.at/backend/info";
static const char* LIVE_STATIONS_URL = "https://live.oebb.at/assets/assets/stations.json";

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
  if (!isValidUtf8(s)) s = latin1ToUtf8(s);   // Scotty liefert Latin-1
  // HTML-Auszeichnungen entfernen (Scotty markiert z.B. ein geändertes Ziel mit <b>…</b>)
  if (s.indexOf('<') >= 0) {
    String t;
    t.reserve(s.length());
    bool tag = false;
    for (size_t i = 0; i < s.length(); i++) {
      char c = s[i];
      if (c == '<') tag = true;
      else if (c == '>' && tag) tag = false;
      else if (!tag) t += c;
    }
    s = t;
  }
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

static void httpSetup(HTTPClient& http) {
  http.setConnectTimeout(8000);
  http.setTimeout(10000);
  http.setUserAgent("Mozilla/5.0 (ESP32; GradNo)");
}

// Liest aus der TLS-Verbindung und wartet dabei geduldig auf Nachschub (bis 8 s pro Zeichen).
// Ohne das bricht das JSON-Parsen ab ("IncompleteInput"), wenn der Server kurz stockt.
class PatientStream : public Stream {
 public:
  PatientStream(Stream& s, WiFiClientSecure& c) : s_(s), c_(c) {}
  int available() override { return s_.available(); }
  int read() override { return wait() ? s_.read() : -1; }
  int peek() override { return wait() ? s_.peek() : -1; }
  size_t write(uint8_t) override { return 0; }
 private:
  bool wait() {
    uint32_t t0 = millis();
    while (!s_.available()) {
      if (!c_.connected()) return s_.available() > 0;
      if (millis() - t0 > 8000) return false;
      delay(2);
    }
    return true;
  }
  Stream& s_;
  WiFiClientSecure& c_;
};

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

// ---------------------------------------------------------------- gemeinsame Helfer

// Zusätze, die auf dem kleinen Display nur Platz kosten: "Wien Hbf (Bahnsteige 3-12)" -> "Wien Hbf"
static String shortDest(String s) {
  int p = s.indexOf(" (Bahnsteig");
  if (p > 0) {
    int q = s.indexOf(')', p);
    s.remove(p, q < 0 ? s.length() - p : q - p + 1);
    s.trim();
  }
  return s;
}

static DepKind kindFromLine(const String& pr) {
  String p = pr;
  p.toLowerCase();
  if (p.startsWith("bus") || p.startsWith("ast") || p.startsWith("o-bus") || p.startsWith("obus") ||
      p.startsWith("nachtbus") || p.startsWith("citybus") || p.startsWith("rufbus"))
    return K_BUS;
  if (p.startsWith("tram") || p.startsWith("str")) return K_TRAM;
  if (p.length() >= 2 && p[0] == 'u' && (isdigit((uint8_t)p[1]) || p[1] == ' ')) return K_METRO;
  if (p.length() >= 2 && p[0] == 's' && (isdigit((uint8_t)p[1]) || p[1] == ' ')) return K_SBAHN;
  return K_TRAIN;
}

static bool isBusLike(DepKind k) { return k == K_BUS || k == K_TRAM || k == K_METRO; }

static bool productMatches(DepKind k, uint8_t filter) {
  if (filter == PF_TRAIN) return !isBusLike(k);
  if (filter == PF_BUS) return isBusLike(k);
  return true;
}

// Kommagetrennte Liste durchgehen; fn(teil) liefert true bei Treffer
template <typename F> static bool anyToken(const char* list, F fn) {
  String f(list);
  int start = 0;
  while (start <= (int)f.length()) {
    int comma = f.indexOf(',', start);
    if (comma < 0) comma = f.length();
    String part = f.substring(start, comma);
    part.trim();
    part.toLowerCase();
    if (part.length() && fn(part)) return true;
    start = comma + 1;
  }
  return false;
}

static bool directionMatches(const String& dest, const char* filter) {
  if (!filter || !filter[0]) return true;
  String d = dest;
  d.toLowerCase();
  return anyToken(filter, [&](const String& part) { return d.indexOf(part) >= 0; });
}

// Linienfilter: "S1", "s 1", "REX 7", "5110", "O", "U1" ...
// Treffer, wenn die ganze Linie (ohne Leerzeichen) oder ihr letztes Wort passt.
static bool lineMatches(const String& line, const char* filter) {
  if (!filter || !filter[0]) return true;
  String l = line;
  l.toLowerCase();
  l.trim();
  String last = l;
  int sp = l.lastIndexOf(' ');
  if (sp >= 0) last = l.substring(sp + 1);
  String compact = l;
  compact.replace(" ", "");
  return anyToken(filter, [&](const String& part) {
    String p = part;
    p.replace(" ", "");
    return p == compact || p == last;
  });
}

// Zugnummer nur für Fernverkehr (dort ist die Nummer im Namen eine echte Zugnummer;
// bei REX 6, S 1, R 81 usw. ist es eine Liniennummer).
static uint32_t trainNumberOf(const String& pr) {
  String p = pr;
  p.trim();
  int i = 0;
  while (i < (int)p.length() && isalpha((uint8_t)p[i])) i++;
  String prefix = p.substring(0, i);
  prefix.toUpperCase();
  static const char* FV[] = { "RJ", "RJX", "IC", "EC", "EN", "NJ", "D", "ICE", "ECE", "TGV", "IR", "EST" };
  bool fv = false;
  for (auto f : FV) if (prefix == f) { fv = true; break; }
  if (!fv) return 0;
  String rest = p.substring(i);
  rest.trim();
  if (rest.length() == 0 || !isdigit((uint8_t)rest[0])) return 0;
  return (uint32_t)strtoul(rest.c_str(), nullptr, 10);
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

// Tage seit 1970-01-01 (proleptischer Gregorianischer Kalender)
static long daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (long)doe - 719468;
}

// "2026-10-31T06:49:00.000+0100" -> Epoch (UTC-korrekt)
static time_t parseIso(const char* s) {
  int y, mo, d, h, mi, se = 0;
  if (!s || sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se) < 5) return 0;
  long t = daysFromCivil(y, mo, d) * 86400L + h * 3600L + mi * 60L + se;
  const char* tz = strpbrk(s + 19, "+-Z");
  if (tz && *tz != 'Z' && strlen(tz) >= 5) {
    int oh = (tz[1] - '0') * 10 + (tz[2] - '0');
    int om = (tz[tz[3] == ':' ? 4 : 3] - '0') * 10 + (tz[tz[3] == ':' ? 5 : 4] - '0');
    long off = oh * 3600L + om * 60L;
    t += (*tz == '+') ? -off : off;
  }
  return (time_t)t;
}

static void fmtLocal(time_t t, char* hm, char* date) {
  struct tm lt;
  localtime_r(&t, &lt);
  if (hm) strftime(hm, 6, "%H:%M", &lt);
  if (date) strftime(date, 11, "%Y-%m-%d", &lt);
}

// Gemeinsame Nachbearbeitung: Filter Gehzeit / vergangen / Vorschau
static bool keepByTime(const Departure& d, const Station& st, bool tv, time_t now) {
  if (!tv || !d.when) return true;
  if (d.when < now - 30) return false;                                       // schon weg
  uint16_t ahead = st.aheadMin ? st.aheadMin : 60;
  if (d.when > now + ahead * 60L) return false;                              // weiter in der Zukunft als die Vorschau
  if (!d.cancelled && st.walkMin && d.when < now + st.walkMin * 60) return false;  // nicht mehr erreichbar
  return true;
}

// ---------------------------------------------------------------- Scotty: Abfahrtstafel

static bool scottyFetchBoard(const Station& st, Board& out) {
  String url = BOARD_URL;
  url += "?L=vs_scotty.vs_liveticker&evaId=";
  url += st.id;
  url += "&boardType=dep&productsFilter=";
  url += PF_STR[st.filter <= 2 ? st.filter : 0];
  url += "&dirInput=&tickerID=dep&start=yes&eqstops=false&outputMode=tickerDataOnly&maxJourneys=";
  // mehr holen als angezeigt wird, wenn danach noch gefiltert wird
  bool filtered = st.direction[0] || st.lines[0] || st.walkMin;
  url += String(filtered ? 40 : 14);

  JsonDocument filter;
  filter["stationName"] = true;
  JsonObject jf = filter["journey"].add<JsonObject>();
  jf["id"] = true;        // Fahrt-ID (gleiche Fahrt = gleiche ID)
  jf["ti"] = true;        // geplante Zeit
  jf["da"] = true;        // Datum
  jf["pr"] = true;        // Produkt/Linie
  jf["st"] = true;        // Ziel
  jf["lastStop"] = true;  // Endhalt (Fallback)
  jf["tr"] = true;        // Gleis/Steig
  jf["rt"] = true;        // Echtzeit: false oder {status, dlm, dlt}

  // Die Antwort ("journeysObj = {...};") wird direkt aus der Verbindung gelesen, ohne sie erst
  // komplett in den Speicher zu laden. Das spart RAM (kein großer Puffer, der bei knappem Speicher
  // abgeschnitten werden kann) und wartet geduldig, wenn die Übertragung stockt.
  // Umlaute kommen als Latin-1 und werden pro Feld in cleanText() nach UTF-8 umgewandelt.
  JsonDocument doc;
  DeserializationError e;
  for (int attempt = 0; attempt < 2; attempt++) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    httpSetup(http);
    http.useHTTP10(true);
    if (!http.begin(client, url)) { strlcpy(out.error, "Verbindung fehlgeschlagen", sizeof(out.error)); return false; }
    int code = http.GET();
    if (code != 200) {
      snprintf(out.error, sizeof(out.error), code < 0 ? "Verbindung fehlgeschlagen (%d)" : "HTTP-Fehler %d", code);
      hafasLastRaw = String("HTTP ") + code + "\n" + url;
      http.end();
      if (code < 0 && attempt == 0) continue;
      return false;
    }
    PatientStream ps(http.getStream(), client);
    int c;
    while ((c = ps.peek()) >= 0 && c != '{') ps.read();   // "journeysObj = " überspringen
    if (c < 0) { http.end(); e = DeserializationError::IncompleteInput; continue; }
    doc.clear();
    e = deserializeJson(doc, ps, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(32));
    http.end();
    if (e != DeserializationError::IncompleteInput) break;
  }
  if (e) {
    if (e == DeserializationError::IncompleteInput) strlcpy(out.error, "Uebertragung abgebrochen", sizeof(out.error));
    else snprintf(out.error, sizeof(out.error), "JSON: %s", e.c_str());
    hafasLastRaw = url + "\nJSON: " + e.c_str();
    return false;
  }
  {
    String dbg;
    serializeJson(doc, dbg);
    hafasLastRaw = url + "\n\n" + dbg.substring(0, 4000);
  }

  strlcpy(out.stationName, cleanText(doc["stationName"] | st.name).c_str(), sizeof(out.stationName));

  bool tv = timeValid();
  time_t now = time(nullptr);

  // Sammelhaltestellen (z.B. "Spittal/Drau" = ganze Stadt) liefern dieselbe Fahrt mehrfach,
  // einmal pro Haltepunkt. Nur das erste (früheste) Vorkommen jeder Fahrt behalten.
  String seen[40];
  int seenN = 0;
  for (JsonObject j : doc["journey"].as<JsonArray>()) {
    if (out.count >= MAX_DEPS) break;
    const char* jid = j["id"] | "";
    if (jid[0]) {
      bool dup = false;
      for (int k = 0; k < seenN && !dup; k++) dup = seen[k] == jid;
      if (dup) continue;
      if (seenN < 40) seen[seenN++] = jid;
    }
    String pr = cleanText(j["pr"] | "");
    const char* stRaw = j["st"] | "";
    // Scotty hebt ein geändertes Ziel fett hervor (z.B. Zug endet vorzeitig in Wien Hbf statt am Flughafen)
    bool destChanged = strstr(stRaw, "<b>") || strstr(stRaw, "<B>");
    String dest = shortDest(cleanText(stRaw));
    if (dest.length() == 0) dest = cleanText(j["lastStop"] | "");
    DepKind kind = kindFromLine(pr);
    if (!productMatches(kind, st.filter)) continue;
    if (!directionMatches(dest, st.direction)) continue;
    if (!lineMatches(pr, st.lines)) continue;

    Departure& d = out.deps[out.count];
    memset(&d, 0, sizeof(d));
    strlcpy(d.time, j["ti"] | "", sizeof(d.time));
    strlcpy(d.line, pr.c_str(), sizeof(d.line));
    strlcpy(d.dest, dest.c_str(), sizeof(d.dest));
    strlcpy(d.platform, cleanText(j["tr"] | "").c_str(), sizeof(d.platform));
    d.kind = kind;
    d.destChanged = destChanged;
    d.trainNr = trainNumberOf(pr);
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
    int dd = 0, mm = 0, yy = 0;
    if (sscanf(da, "%d.%d.%d", &dd, &mm, &yy) == 3) {
      if (yy < 100) yy += 2000;
      snprintf(d.date, sizeof(d.date), "%04d-%02d-%02d", yy, mm, dd);
    }
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

    if (!keepByTime(d, st, tv, now)) continue;
    out.count++;
  }

  out.ok = true;
  return true;
}

// ---------------------------------------------------------------- Wiener Linien: Abfahrtsmonitor

static DepKind wlKind(const char* type, const String& name) {
  if (type) {
    if (!strcmp(type, "ptMetro")) return K_METRO;
    if (!strcmp(type, "ptTram") || !strcmp(type, "ptTramWLB")) return K_TRAM;
    if (!strncmp(type, "ptBus", 5)) return K_BUS;
    if (!strcmp(type, "ptTrainS")) return K_SBAHN;
  }
  return kindFromLine(name);
}

static bool wlFetchBoard(const Station& st, Board& out) {
  String url = WL_MONITOR_URL;
  url += st.id;

  JsonDocument filter;
  JsonObject mf = filter["data"]["monitors"].add<JsonObject>();
  mf["locationStop"]["properties"]["title"] = true;
  JsonObject lf = mf["lines"].add<JsonObject>();
  lf["name"] = true;
  lf["towards"] = true;
  lf["platform"] = true;
  JsonObject df = lf["departures"]["departure"].add<JsonObject>();
  df["departureTime"]["timePlanned"] = true;
  df["departureTime"]["timeReal"] = true;
  df["vehicle"]["type"] = true;
  filter["message"]["value"] = true;

  JsonDocument doc;
  DeserializationError e;
  // bis zu 2 Versuche: bricht die Übertragung ab, gleich noch einmal holen
  for (int attempt = 0; attempt < 2; attempt++) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    httpSetup(http);
    http.useHTTP10(true);   // kein chunked encoding -> JSON direkt aus dem Stream lesen
    if (!http.begin(client, url)) { strlcpy(out.error, "Verbindung fehlgeschlagen", sizeof(out.error)); return false; }
    int code = http.GET();
    if (code != 200) {
      snprintf(out.error, sizeof(out.error), code < 0 ? "Verbindung fehlgeschlagen (%d)" : "HTTP-Fehler %d", code);
      hafasLastRaw = String("HTTP ") + code + "\n" + url;
      http.end();
      if (code < 0 && attempt == 0) continue;
      return false;
    }
    PatientStream ps(http.getStream(), client);
    doc.clear();
    e = deserializeJson(doc, ps, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(32));
    http.end();
    if (e != DeserializationError::IncompleteInput) break;
  }
  if (e) {
    if (e == DeserializationError::IncompleteInput) strlcpy(out.error, "Uebertragung abgebrochen", sizeof(out.error));
    else snprintf(out.error, sizeof(out.error), "JSON: %s", e.c_str());
    hafasLastRaw = url + "\nJSON: " + e.c_str();
    return false;
  }
  {
    String dbg;
    serializeJson(doc, dbg);
    hafasLastRaw = url + "\n\n" + dbg.substring(0, 4000);
  }

  JsonArray mons = doc["data"]["monitors"].as<JsonArray>();
  const char* title = mons.isNull() || mons.size() == 0 ? nullptr : (const char*)mons[0]["locationStop"]["properties"]["title"];
  strlcpy(out.stationName, title ? title : st.name, sizeof(out.stationName));

  bool tv = timeValid();
  time_t now = time(nullptr);

  // Alle Abfahrten sammeln (nach Zeit sortiert einfügen, nur die ersten MAX_DEPS behalten)
  for (JsonObject m : mons) {
    for (JsonObject ln : m["lines"].as<JsonArray>()) {
      String name = cleanText(ln["name"] | "");
      String dest = cleanText(ln["towards"] | "");
      if (!directionMatches(dest, st.direction)) continue;
      if (!lineMatches(name, st.lines)) continue;
      const char* plat = ln["platform"] | "";
      for (JsonObject dep : ln["departures"]["departure"].as<JsonArray>()) {
        Departure d;
        memset(&d, 0, sizeof(d));
        time_t planned = parseIso(dep["departureTime"]["timePlanned"] | "");
        time_t real = parseIso(dep["departureTime"]["timeReal"] | "");
        if (!planned && !real) continue;
        d.kind = wlKind(dep["vehicle"]["type"] | (const char*)nullptr, name);
        if (!productMatches((DepKind)d.kind, st.filter)) continue;
        strlcpy(d.line, name.c_str(), sizeof(d.line));
        strlcpy(d.dest, dest.c_str(), sizeof(d.dest));
        strlcpy(d.platform, plat, sizeof(d.platform));
        fmtLocal(planned ? planned : real, d.time, d.date);
        if (real) {
          fmtLocal(real, d.rtTime, nullptr);
          d.delay = planned ? (int16_t)max(0L, (long)(real - planned) / 60) : 0;
        } else {
          d.delay = -1;
        }
        d.when = real ? real : planned;
        if (!keepByTime(d, st, tv, now)) continue;

        // sortiert einfügen
        int pos = out.count;
        while (pos > 0 && out.deps[pos - 1].when > d.when) pos--;
        if (pos >= MAX_DEPS) continue;
        int last = min((int)out.count, MAX_DEPS - 1);
        for (int k = last; k > pos; k--) out.deps[k] = out.deps[k - 1];
        out.deps[pos] = d;
        if (out.count < MAX_DEPS) out.count++;
      }
    }
  }
  out.ok = true;
  return true;
}

bool fetchBoard(const Station& st, Board& out) {
  memset(&out, 0, sizeof(out));
  if (WiFi.status() != WL_CONNECTED) { strlcpy(out.error, "Kein WLAN", sizeof(out.error)); return false; }
  if (st.provider == PROV_WIENER_LINIEN) return wlFetchBoard(st, out);
  return scottyFetchBoard(st, out);
}

// ---------------------------------------------------------------- Haltestellensuche

static int parseScottySearch(const String& body, JsonArray out) {
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
  if (deserializeJson(doc, json, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(32))) return 0;

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

static int scottySearch(const String& query, JsonArray out) {
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
    int n = parseScottySearch(body, out);
    if (n > 0) return n;
  }
  return 0;
}

// Kleinbuchstaben nur für ASCII (Umlaute bleiben unverändert, reicht für die Suche)
static String lowerAscii(String s) {
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c >= 'A' && c <= 'Z') s.setCharAt(i, c + 32);
  }
  return s;
}

// Wiener Linien: Haltestellenliste (CSV, ~130 KB) zeilenweise durchsuchen
static int wlSearch(const String& query, JsonArray out) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  httpSetup(http);
  http.useHTTP10(true);
  if (!http.begin(client, WL_STOPS_URL)) return -1;
  int code = http.GET();
  if (code != 200) { http.end(); hafasLastRaw = String("HTTP ") + code + "\n" + WL_STOPS_URL; return -1; }
  PatientStream s(http.getStream(), client);
  // Suchwörter: Umlaute vereinheitlicht (ü = ue), "Wien" wird ignoriert (alle Haltestellen sind in Wien),
  // "Hbf" = "Hauptbahnhof". Alle übrigen Wörter müssen im Namen vorkommen.
  String words[6];
  int wn = 0;
  {
    String q = lowerAscii(translit(query));
    q.replace(",", " ");
    q.replace("-", " ");
    int start = 0;
    while (start < (int)q.length() && wn < 6) {
      int sp = q.indexOf(' ', start);
      if (sp < 0) sp = q.length();
      String w = q.substring(start, sp);
      w.trim();
      start = sp + 1;
      if (w.length() == 0 || w == "wien") continue;
      if (w == "hbf") w = "hauptbahnhof";
      if (w == "bf" || w == "bhf" || w == "bahnhof") w = "bahnhof";
      words[wn++] = w;
    }
  }
  if (wn == 0) { http.end(); return 0; }
  int n = 0;
  uint32_t t0 = millis();
  while (millis() - t0 < 20000) {
    String line = s.readStringUntil('\n');
    if (line.length() == 0 && !s.available() && !client.connected()) break;
    // Format: DIVA;PlatformText;Municipality;...
    int a = line.indexOf(';');
    if (a <= 0) continue;
    int b = line.indexOf(';', a + 1);
    if (b < 0) continue;
    String diva = line.substring(0, a);
    String name = line.substring(a + 1, b);
    if (!isdigit((uint8_t)diva[0])) continue;   // Kopfzeile
    String hay = lowerAscii(translit(name));
    hay.replace("-", " ");
    bool all = true;
    for (int k = 0; k < wn && all; k++) all = hay.indexOf(words[k]) >= 0;
    if (!all) continue;
    JsonObject o = out.add<JsonObject>();
    o["id"] = diva;
    o["name"] = name;
    if (++n >= 15) break;
  }
  http.end();
  hafasLastRaw = String(WL_STOPS_URL) + "\n\n" + n + " Treffer für \"" + query + "\"";
  return n;
}

int searchStations(uint8_t provider, const String& query, JsonArray out) {
  if (WiFi.status() != WL_CONNECTED) return -1;
  if (provider == PROV_WIENER_LINIEN) return wlSearch(query, out);
  return scottySearch(query, out);
}

// ---------------------------------------------------------------- Wagenreihung (live.oebb.at)

// Namen vergleichbar machen: Kleinbuchstaben, Klammerzusätze und "Bahnhof"/"Bhf" weg
static String normName(const String& in) {
  String s = lowerAscii(in);
  int p;
  while ((p = s.indexOf('(')) >= 0) {
    int q = s.indexOf(')', p);
    s.remove(p, q < 0 ? s.length() - p : q - p + 1);
  }
  s.replace(" bahnhof", "");
  s.replace(" bhf", "");
  s.replace("  ", " ");
  s.trim();
  return s;
}

bool findEva(const String& stationName, char* out, size_t outLen) {
  out[0] = 0;
  String want = normName(stationName);
  if (want.length() < 2) return false;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  httpSetup(http);
  http.useHTTP10(true);
  if (!http.begin(client, LIVE_STATIONS_URL)) return false;
  int code = http.GET();
  if (code != 200) { http.end(); return false; }
  PatientStream s(http.getStream(), client);

  // Liste im Format  { "eva": "8100147", "name": "Villach Hbf" }, ...  (eine Angabe pro Zeile)
  String eva;
  String exact[3];      // gleicher Name (ohne Klammerzusatz zuerst)
  int exactN = 0;
  String prefixEva;     // Name beginnt gleich – kürzester gewinnt
  int prefixLen = 9999;
  uint32_t t0 = millis();
  while (millis() - t0 < 25000) {
    String line = s.readStringUntil('\n');
    if (line.length() == 0 && !s.available() && !client.connected()) break;
    int k = line.indexOf("\"eva\"");
    if (k >= 0) {
      int a = line.indexOf('"', line.indexOf(':', k) + 1);
      int b = line.indexOf('"', a + 1);
      if (a >= 0 && b > a) eva = line.substring(a + 1, b);
      continue;
    }
    k = line.indexOf("\"name\"");
    if (k < 0 || eva.length() == 0) continue;
    int a = line.indexOf('"', line.indexOf(':', k) + 1);
    int b = line.lastIndexOf('"');
    if (a < 0 || b <= a) continue;
    String raw = line.substring(a + 1, b);
    String nn = normName(raw);
    if (nn == want) {
      bool plain = raw.indexOf('(') < 0;
      if (plain) {              // ohne Klammerzusatz nach vorne
        for (int i = min(exactN, 2); i > 0; i--) exact[i] = exact[i - 1];
        exact[0] = eva;
        if (exactN < 3) exactN++;
      } else if (exactN < 3) {
        exact[exactN++] = eva;
      }
    } else if ((nn.startsWith(want) || want.startsWith(nn)) && nn.length() >= 4 && (int)raw.length() < prefixLen) {
      prefixEva = eva;
      prefixLen = raw.length();
    }
    eva = "";
  }
  http.end();

  String res;
  if (exactN > 0) {
    for (int i = 0; i < exactN; i++) { if (i) res += ","; res += exact[i]; }
  } else if (prefixEva.length()) {
    res = prefixEva;
  }
  strlcpy(out, res.c_str(), outLen);
  hafasLastRaw = String(LIVE_STATIONS_URL) + "\n\nSuche \"" + stationName + "\" -> " + (res.length() ? res : String("nichts gefunden"));
  return res.length() > 0;
}

static bool fetchFormationAt(uint32_t trainNr, const String& eva, const char* date, Formation& out, int& code) {
  String url = LIVE_INFO_URL;
  url += "?trainNr=" + String(trainNr) + "&station=" + eva + "&date=" + date + "&cref=live_web";
  String body = httpsGet(url, code);
  hafasLastRaw = url + "\n\n" + (code == 200 ? body.substring(0, 4000) : String("HTTP ") + code);
  if (code != 200) return false;

  JsonDocument filter;
  filter["timeTableInfo"]["platform"] = true;
  filter["timeTableInfo"]["renderedTrainName"] = true;
  filter["timeTableInfo"]["stationName"] = true;
  filter["train"]["displayLoadInfo"] = true;
  JsonObject wf = filter["train"]["wagons"].add<JsonObject>();
  for (const char* k : { "kind", "features", "ranking", "capacityBusinessClass", "capacityFirstClass",
                         "capacitySecondClass", "capacityWheelChair", "capacityBicycle",
                         "destinationName", "numPassengerIcons" })
    wf[k] = true;

  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(32))) { code = -2; return false; }

  memset(&out, 0, sizeof(out));
  strlcpy(out.title, doc["timeTableInfo"]["renderedTrainName"] | "", sizeof(out.title));
  strlcpy(out.platform, doc["timeTableInfo"]["platform"] | "", sizeof(out.platform));
  strlcpy(out.station, doc["timeTableInfo"]["stationName"] | "", sizeof(out.station));
  out.loadInfo = doc["train"]["displayLoadInfo"] | false;

  for (JsonObject w : doc["train"]["wagons"].as<JsonArray>()) {
    if (out.count >= MAX_WAGONS) break;
    Wagon& g = out.wagons[out.count];
    int kind = w["kind"] | 0;
    g.kind = kind == 256 ? W_ENGINE : (kind == 1 ? W_CAB : W_COACH);
    g.nr = w["ranking"] | 0;
    g.first = w["capacityFirstClass"] | 0;
    g.second = w["capacitySecondClass"] | 0;
    g.business = w["capacityBusinessClass"] | 0;
    g.bikes = w["capacityBicycle"] | 0;
    g.wheelchairs = w["capacityWheelChair"] | 0;
    g.load = w["numPassengerIcons"] | 0;
    g.features = w["features"] | 0;
    // Zugteil-Ziel
    String dest = cleanText(w["destinationName"] | "");
    int part = -1;
    for (int i = 0; i < out.partCount; i++) if (dest == out.parts[i]) part = i;
    if (part < 0 && out.partCount < MAX_PARTS && dest.length()) {
      part = out.partCount++;
      strlcpy(out.parts[part], dest.c_str(), sizeof(out.parts[part]));
    }
    g.part = part < 0 ? 0 : part;
    out.count++;
  }
  if (out.count == 0) { code = -3; return false; }
  out.ok = true;
  return true;
}

bool fetchFormation(uint32_t trainNr, const char* evaList, const char* date, Formation& out) {
  memset(&out, 0, sizeof(out));
  if (WiFi.status() != WL_CONNECTED) { strlcpy(out.error, "Kein WLAN", sizeof(out.error)); return false; }
  if (!trainNr) { strlcpy(out.error, "Keine Zugnummer", sizeof(out.error)); return false; }
  String list(evaList);
  int start = 0, lastCode = 0;
  while (start < (int)list.length()) {
    int comma = list.indexOf(',', start);
    if (comma < 0) comma = list.length();
    String eva = list.substring(start, comma);
    eva.trim();
    start = comma + 1;
    if (eva.length() == 0) continue;
    if (fetchFormationAt(trainNr, eva, date, out, lastCode)) return true;
  }
  memset(&out, 0, sizeof(out));
  if (lastCode == 404 || lastCode == -3) strlcpy(out.error, "Keine Wagenreihung verfuegbar", sizeof(out.error));
  else if (lastCode == 0) strlcpy(out.error, "Bahnhof nicht gefunden", sizeof(out.error));
  else snprintf(out.error, sizeof(out.error), "Fehler %d", lastCode);
  return false;
}
