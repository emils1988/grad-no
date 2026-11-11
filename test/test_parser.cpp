// Host-Test der Datenauswertung (Scotty, Wiener Linien, ÖBB Live) mit Beispieldaten
#include "Arduino.h"
#include "HTTPClient.h"
#include "../src/hafas.h"
#include <cassert>
#include <iostream>
#include <regex>
#include <filesystem>
#include <fstream>
#include <sstream>

std::vector<MockRoute> g_routes;
std::vector<std::string> g_requested;
Settings cfg;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::cout << "FAIL " << __LINE__ << ": " #c "\n"; fails++; } } while (0)

static Station mk(const char* id, uint8_t prov) { Station s; memset(&s, 0, sizeof(s)); strlcpy(s.id, id, sizeof(s.id)); s.provider = prov; s.aheadMin = MAX_AHEAD; return s; }
static void dump(const Board& b) {
  std::cout << "  [" << b.stationName << "] ok=" << b.ok << " n=" << (int)b.count << " " << b.error << "\n";
  for (int i = 0; i < b.count; i++) { auto& d = b.deps[i];
    std::cout << "   " << d.time << " rt=" << d.rtTime << " +" << d.delay << " " << d.line << " k" << (int)d.kind << " -> " << d.dest
              << " [" << d.platform << "] nr=" << d.trainNr << " " << d.date << (d.cancelled ? " AUSFALL" : "") << "\n"; }
}

// Testdaten auf "morgen" umdatieren, damit die Tests an jedem Tag gleich laufen
static std::string withTomorrow(const std::string& src) {
  std::ifstream f(src, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); std::string s = ss.str();
  time_t t = time(nullptr) + 86400; struct tm lt; localtime_r(&t, &lt);
  char de[11], iso[11]; strftime(de, sizeof(de), "%d.%m.%Y", &lt); strftime(iso, sizeof(iso), "%Y-%m-%d", &lt);
  s = std::regex_replace(s, std::regex("[0-9]{2}\\.[0-9]{2}\\.[0-9]{4}"), de);
  s = std::regex_replace(s, std::regex("[0-9]{4}-[0-9]{2}-[0-9]{2}T"), std::string(iso) + "T");
  std::string dst = (std::filesystem::temp_directory_path() / std::filesystem::path(src).filename()).string();
  std::ofstream(dst, std::ios::binary) << s;
  return dst;
}

int main() {
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1); tzset();
  const std::string scottyFile = withTomorrow("test/data/scotty_board.txt");
  const std::string wlFile = withTomorrow("test/data/wl_monitor.json");
  g_routes = {
    {"https://fahrplan.oebb.at/bin/stboard.exe", scottyFile, 200},
    {"https://www.wienerlinien.at/ogd_realtime/monitor?diva=60201040", wlFile, 200},
    {"https://www.wienerlinien.at/ogd_realtime/doku/ogd/wienerlinien-ogd-haltestellen.csv", "test/data/wl_stops.csv", 200},
    {"https://live.oebb.at/assets/assets/stations.json", "test/data/live_stations.json", 200},
    {"https://live.oebb.at/backend/info?trainNr=66&station=8103000", "test/data/live_info_66.json", 200},
    {"https://live.oebb.at/backend/info?trainNr=66&station=", "", 404},
  };
  static Board b;

  std::cout << "== Scotty, alle\n";
  Station s = mk("1290401", PROV_SCOTTY);
  CHECK(fetchBoard(s, b)); dump(b);
  CHECK(b.count == 8);   // 7 + Bus 11 (doppelt geliefert, nur einmal angezeigt)
  CHECK(String(b.deps[7].line) == "Bus 11" && String(b.deps[7].platform) == "B2" && String(b.deps[7].time) == "06:58");
  CHECK(String(b.stationName) == "Wien Hbf (U)");
  CHECK(b.deps[0].kind == K_TRAM && b.deps[0].delay == 9 && String(b.deps[0].rtTime) == "06:40");
  CHECK(b.deps[1].trainNr == 463);
  CHECK(b.deps[2].trainNr == 66 && b.deps[2].delay == -1);
  CHECK(b.deps[3].trainNr == 19950 && b.deps[3].cancelled);
  CHECK(String(b.deps[3].dest) == "M\xc3\xbcnchen Hbf");   // Latin-1 -> UTF-8
  CHECK(b.deps[4].trainNr == 0);                            // REX 6 = Linie, keine Zugnummer
  CHECK(b.deps[5].kind == K_BUS && b.deps[6].kind == K_SBAHN);
  CHECK(String(b.deps[2].date).length() == 10);
  CHECK(g_requested.back().find("maxJourneys=14") != std::string::npos);

  std::cout << "== Scotty, Übertragung bricht einmal ab -> zweiter Versuch\n";
  for (auto& r : g_routes) if (r.file == scottyFile) r.truncateOnce = 1;
  { Station v = mk("1290401", PROV_SCOTTY); CHECK(fetchBoard(v, b)); CHECK(b.count == 8);
    for (auto& r : g_routes) if (r.file == scottyFile) r.truncateOnce = 2;
    CHECK(!fetchBoard(v, b)); std::cout << "  zweimal abgebrochen: " << b.error << "\n"; }

  std::cout << "== Vorschau\n";
  {
    // Testdaten liegen morgen früh -> mit 60 min Vorschau nichts, mit 24 h alles
    Station v = mk("1290401", PROV_SCOTTY); v.aheadMin = 60;
    CHECK(fetchBoard(v, b)); time_t first = 0; (void)first;
    int n60 = b.count;
    v.aheadMin = 1440;
    CHECK(fetchBoard(v, b)); int n24 = b.count;
    std::cout << "  60 min: " << n60 << "  24 h: " << n24 << "\n";
    CHECK(n24 == 8);
    CHECK(n60 <= n24);
  }

  std::cout << "== Scotty, Linienfilter 'S1, REX 6, 13A'\n";
  strlcpy(s.lines, "S1, REX 6, 13A", sizeof(s.lines));
  CHECK(fetchBoard(s, b)); dump(b);
  CHECK(b.count == 3);
  CHECK(g_requested.back().find("maxJourneys=40") != std::string::npos);

  std::cout << "== Scotty, Linie 'O' + Richtung 'bruno'\n";
  strlcpy(s.lines, "o", sizeof(s.lines)); strlcpy(s.direction, "bruno", sizeof(s.direction));
  CHECK(fetchBoard(s, b)); dump(b); CHECK(b.count == 1 && String(b.deps[0].line) == "Tram O");
  strlcpy(s.direction, "leopoldau", sizeof(s.direction));
  CHECK(fetchBoard(s, b)); CHECK(b.count == 0);

  std::cout << "== Scotty, nur Busse/Tram\n";
  s = mk("1290401", PROV_SCOTTY); s.filter = PF_BUS;
  CHECK(fetchBoard(s, b)); dump(b); CHECK(b.count == 3);

  std::cout << "== Wiener Linien\n";
  Station w = mk("60201040", PROV_WIENER_LINIEN);
  CHECK(fetchBoard(w, b)); dump(b);
  CHECK(b.count == 5);
  CHECK(String(b.stationName) == "Praterstern S U");
  CHECK(String(b.deps[0].line) == "5" && b.deps[0].kind == K_TRAM && b.deps[0].delay == 0);   // früher dran -> 0
  CHECK(String(b.deps[0].time) == "07:01" && String(b.deps[0].rtTime) == "06:59");
  CHECK(String(b.deps[1].line) == "U1" && b.deps[1].delay == 1 && b.deps[1].kind == K_METRO);
  CHECK(String(b.deps[2].line) == "82A" && b.deps[2].kind == K_BUS && String(b.deps[2].platform) == "3");
  CHECK(String(b.deps[3].line) == "U1" && b.deps[3].delay == -1);
  for (int i = 1; i < b.count; i++) CHECK(b.deps[i - 1].when <= b.deps[i].when);
  strlcpy(w.lines, "U1", sizeof(w.lines));
  CHECK(fetchBoard(w, b)); CHECK(b.count == 2);

  std::cout << "== Wiener Linien, Übertragung bricht einmal ab -> zweiter Versuch\n";
  for (auto& r : g_routes) if (r.file == wlFile) r.truncateOnce = 1;
  w.lines[0] = 0;
  CHECK(fetchBoard(w, b)); std::cout << "  ok=" << b.ok << " n=" << (int)b.count << " " << b.error << "\n"; CHECK(b.count == 5);
  for (auto& r : g_routes) if (r.file == wlFile) r.truncateOnce = 2;
  CHECK(!fetchBoard(w, b)); std::cout << "  zweimal abgebrochen: " << b.error << "\n";

  std::cout << "== Suche\n";
  { JsonDocument d; JsonArray a = d.to<JsonArray>(); int n = searchStations(PROV_WIENER_LINIEN, "prater", a);
    std::string out; serializeJson(d, out); std::cout << "  " << out << "\n"; CHECK(n == 2); }
  { JsonDocument d; JsonArray a = d.to<JsonArray>(); int n = searchStations(PROV_WIENER_LINIEN, "B\xc3\xb6rse", a); CHECK(n == 1); }
  { JsonDocument d; JsonArray a = d.to<JsonArray>(); int n = searchStations(PROV_WIENER_LINIEN, "Wien Hauptbahnhof", a);
    std::string out; serializeJson(d, out); std::cout << "  Wien Hauptbahnhof: " << out << "\n"; CHECK(n == 2); }
  { JsonDocument d; JsonArray a = d.to<JsonArray>(); CHECK(searchStations(PROV_WIENER_LINIEN, "Hbf S\xc3\xbc" "d", a) == 1); }
  { JsonDocument d; JsonArray a = d.to<JsonArray>(); CHECK(searchStations(PROV_WIENER_LINIEN, "boerse", a) == 1); }
  { JsonDocument d; JsonArray a = d.to<JsonArray>(); CHECK(searchStations(PROV_WIENER_LINIEN, "Wien", a) == 0); }

  std::cout << "== EVA-Suche\n";
  char eva[32];
  CHECK(findEva("Wien Hbf (U)", eva, sizeof(eva))); std::cout << "  Wien Hbf (U) -> " << eva << "\n";
  CHECK(String(eva).startsWith("8103000,"));
  CHECK(findEva("Villach Hbf", eva, sizeof(eva)) && String(eva) == "8100147");
  CHECK(findEva("Spittal/Drau-Millst\xc3\xa4tter See", eva, sizeof(eva)) && String(eva) == "8100092");
  CHECK(findEva("Spittal/Drau", eva, sizeof(eva)) && String(eva) == "8100092");   // Präfix
  CHECK(!findEva("Amlach", eva, sizeof(eva)));

  std::cout << "== Wagenreihung\n";
  static Formation f;
  CHECK(fetchFormation(66, "8103000,8101590", "2026-11-01", f));
  std::cout << "  " << f.title << " Gl." << f.platform << " Teile=" << (int)f.partCount << " Wagen=" << (int)f.count << " load=" << f.loadInfo << "\n";
  CHECK(f.count == 16 && f.partCount == 2 && f.loadInfo);
  CHECK(f.wagons[0].kind == W_ENGINE && f.wagons[7].kind == W_CAB && f.wagons[7].business == 16);
  CHECK(f.wagons[5].features & 64);  // Speisewagen
  CHECK(f.wagons[1].bikes == 1 && f.wagons[12].load == 3 && f.wagons[12].part == 1);
  CHECK(String(f.parts[1]) == "Muenchen Hbf");
  // zweite EVA wird probiert, wenn die erste 404 liefert
  CHECK(fetchFormation(66, "8101590, 8103000", "2026-11-01", f));
  CHECK(!fetchFormation(66, "8101590", "2026-11-01", f)); std::cout << "  Fehlertext: " << f.error << "\n";
  CHECK(!fetchFormation(0, "8103000", "2026-11-01", f));

  std::cout << "== translit\n";
  CHECK(translit("Gro\xc3\x9f" "feldsiedlung \xc3\x9c" "ber") == "Grossfeldsiedlung Ueber");
  CHECK(translit("Zagreb Glavni kol. \xc4\x8d") == "Zagreb Glavni kol. c");

  std::cout << (fails ? "\nFEHLER: " : "\nALLE TESTS OK") << (fails ? std::to_string(fails) : "") << "\n";
  return fails ? 1 : 0;
}
