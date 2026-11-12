#include "display.h"
#include <TFT_eSPI.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <time.h>

// ---- Pins Cheap Yellow Display (ESP32-2432S028R) ----
#define XPT_CS    33
#define XPT_IRQ   36
#define XPT_CLK   25
#define XPT_MOSI  32
#define XPT_MISO  39
#define BL_PIN    21
#define BL_CH     7     // LEDC-Kanal für Hintergrundbeleuchtung

// Rohwerte des Touch-Controllers (ggf. anpassen)
#define TOUCH_X_MIN 200
#define TOUCH_X_MAX 3700
#define TOUCH_Y_MIN 240
#define TOUCH_Y_MAX 3800

#define SCR_W 320
#define SCR_H 240
#define HEADER_H 34
#define ROW_H 25
#define ROWS_Y (HEADER_H + 2)
#define MAX_ROWS ((SCR_H - ROWS_Y) / ROW_H)   // = 8 (keine Fußzeile mehr)

static TFT_eSPI tft;
static TFT_eSprite row(&tft);
static bool rowSpriteOk = false;
static SPIClass touchSpi(VSPI);
static XPT2046_Touchscreen ts(XPT_CS, XPT_IRQ);

// feste Farben
static uint16_t C_RED, C_GREEN, C_YELLOW, C_GREY, C_DIM;
static uint16_t C_TRAIN, C_SBAHN, C_BUS, C_TRAM, C_METRO;

// Farbschema der aktuell angezeigten Haltestelle
struct Pal { uint16_t bg, alt, header, text, accent, sub; };
static Pal pal;

static uint16_t rgb(uint32_t c) { return tft.color565((c >> 16) & 255, (c >> 8) & 255, c & 255); }
static uint32_t lighten(uint32_t c, int d) {
  int r = min(255, (int)((c >> 16) & 255) + d), g = min(255, (int)((c >> 8) & 255) + d),
      b = min(255, (int)(c & 255) + d);
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static Pal makePal(const Station* st) {
  Pal p;
  uint8_t th = st ? st->theme : TH_STANDARD;
  switch (th) {
    case TH_OEBB:   // blaue Abfahrtsmonitore der ÖBB
      p.bg = rgb(0x1B2A80); p.alt = rgb(0x22338F); p.header = rgb(0x0E1A5C);
      p.text = TFT_WHITE; p.accent = rgb(0xFFE14D); p.sub = rgb(0xB8C2EE);
      break;
    case TH_WL:     // Wiener Linien: schwarz / grau
      p.bg = TFT_BLACK; p.alt = rgb(0x1E1E1E); p.header = rgb(0x4A4A4C);
      p.text = TFT_WHITE; p.accent = rgb(0xFFB400); p.sub = rgb(0xA0A0A0);
      break;
    case TH_CUSTOM:
      p.bg = rgb(st->colors[0]); p.alt = rgb(lighten(st->colors[0], 14)); p.header = rgb(st->colors[1]);
      p.text = rgb(st->colors[2]); p.accent = rgb(st->colors[3]); p.sub = C_GREY;
      break;
    default:        // Standard: dunkel mit Bernstein
      p.bg = TFT_BLACK; p.alt = rgb(0x0E0F14); p.header = rgb(0x161822);
      p.text = TFT_WHITE; p.accent = rgb(0xFFB000); p.sub = C_GREY;
      break;
  }
  return p;
}

void displayInit() {
  tft.init();
  tft.setRotation(cfg.rotation);
  tft.fillScreen(TFT_BLACK);

  ledcSetup(BL_CH, 5000, 8);
  ledcAttachPin(BL_PIN, BL_CH);
  ledcWrite(BL_CH, 200);

  touchSpi.begin(XPT_CLK, XPT_MISO, XPT_MOSI, XPT_CS);
  ts.begin(touchSpi);
  ts.setRotation(cfg.rotation);

  C_GREY   = tft.color565(140, 140, 150);
  C_DIM    = tft.color565(90, 90, 100);
  C_RED    = tft.color565(255, 70, 60);
  C_GREEN  = tft.color565(80, 200, 110);
  C_YELLOW = tft.color565(255, 210, 0);
  C_TRAIN  = tft.color565(170, 25, 35);
  C_SBAHN  = tft.color565(0, 100, 185);
  C_BUS    = tft.color565(75, 78, 92);
  C_TRAM   = tft.color565(200, 40, 40);
  C_METRO  = tft.color565(110, 60, 160);
  pal = makePal(nullptr);

  row.setColorDepth(16);
  rowSpriteOk = row.createSprite(SCR_W, ROW_H) != nullptr;
}

void displayApplySettings() {
  tft.setRotation(cfg.rotation);
  ts.setRotation(cfg.rotation);
  tft.invertDisplay(cfg.invert);
}

void displaySetBrightness(uint8_t value) { ledcWrite(BL_CH, value); }

bool touchRead(int& x, int& y) {
  if (!ts.touched()) return false;
  TS_Point p = ts.getPoint();
  if (p.z < 150) return false;
  x = constrain(map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, SCR_W - 1), 0, SCR_W - 1);
  y = constrain(map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, SCR_H - 1), 0, SCR_H - 1);
  if (cfg.touchMirror) { x = SCR_W - 1 - x; y = SCR_H - 1 - y; }
  return true;
}

int displayRowAt(int y) {
  if (y < ROWS_Y || y >= ROWS_Y + MAX_ROWS * ROW_H) return -1;
  return (y - ROWS_Y) / ROW_H;
}

// Text auf maximale Breite kürzen ("Klagenfurt Hbf" -> "Klagenfu..")
static String fit(TFT_eSPI& g, String s, int maxW) {
  if (g.textWidth(s) <= maxW) return s;
  while (s.length() > 1 && g.textWidth(s + "..") > maxW) s.remove(s.length() - 1);
  s.trim();
  return s + "..";
}

void displayMessage(const char* title, const char* l1, const char* l2, const char* l3, const char* l4) {
  Pal p = makePal(nullptr);
  tft.fillScreen(TFT_BLACK);
  tft.fillRect(0, 0, SCR_W, HEADER_H, p.header);
  tft.drawFastHLine(0, HEADER_H, SCR_W, p.accent);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(4);
  tft.setTextColor(p.accent, p.header);
  tft.drawString(fit(tft, translit(title), SCR_W - 10), SCR_W / 2, HEADER_H / 2 + 1);

  const char* lines[] = { l1, l2, l3, l4 };
  tft.setTextFont(2);
  int y = 80;
  for (int i = 0; i < 4; i++) {
    if (!lines[i]) continue;
    tft.setTextColor(i == 0 ? TFT_WHITE : C_GREY, TFT_BLACK);
    tft.drawString(fit(tft, translit(lines[i]), SCR_W - 16), SCR_W / 2, y);
    y += 30;
  }
}

static uint16_t badgeColor(const Departure& d) {
  switch (d.kind) {
    case K_SBAHN: return C_SBAHN;
    case K_BUS:   return C_BUS;
    case K_TRAM:  return C_TRAM;
    case K_METRO: {
      // Wiener U-Bahn-Linienfarben
      if (d.line[0] == 'U' && d.line[1] && !d.line[2]) {
        switch (d.line[1]) {
          case '1': return tft.color565(227, 0, 15);
          case '2': return tft.color565(160, 95, 165);
          case '3': return tft.color565(235, 120, 25);
          case '4': return tft.color565(30, 150, 70);
          case '5': return tft.color565(0, 150, 160);
          case '6': return tft.color565(160, 100, 40);
        }
      }
      return C_METRO;
    }
    default:      return C_TRAIN;
  }
}

// Spalten (x in Pixel)
#define COL_DELAY   42
#define COL_BADGE   66
#define BADGE_W     62
#define COL_DEST    134
#define PLAT_RIGHT  282     // rechter Rand der Gleis-Spalte
#define PLAT_W      40      // max. Breite der Gleisangabe

static void drawRow(TFT_eSPI& g, int y, const Departure& d, bool alt, time_t now, bool tv) {
  uint16_t bg = alt ? pal.alt : pal.bg;
  int cy = y + ROW_H / 2;
  g.fillRect(0, y, SCR_W, ROW_H, bg);
  g.setTextFont(2);

  // geplante Zeit
  g.setTextDatum(ML_DATUM);
  g.setTextColor(d.cancelled ? C_DIM : pal.accent, bg);
  g.drawString(d.time, 4, cy);

  // Verspätung
  if (d.cancelled) {
    g.setTextColor(C_RED, bg);
    g.drawString("X", COL_DELAY + 2, cy);
  } else if (d.delay > 0) {
    g.setTextColor(C_RED, bg);
    g.drawString("+" + String(d.delay), COL_DELAY, cy);
  } else if (d.delay == 0) {
    g.fillCircle(COL_DELAY + 4, cy, 2, C_GREEN);   // Echtzeit: pünktlich
  }

  // Linien-Badge
  uint16_t badge = badgeColor(d);
  g.fillRoundRect(COL_BADGE, y + 4, BADGE_W, ROW_H - 8, 4, badge);
  String ln(d.line);
  if (g.textWidth(ln) > BADGE_W - 6) {
    // "Bus 5110" -> "5110", "Tram O" -> "O" (die Farbe zeigt schon das Verkehrsmittel)
    if (d.kind == K_BUS || d.kind == K_TRAM) { int sp = ln.indexOf(' '); if (sp > 0) ln = ln.substring(sp + 1); }
    // Zugnummer nie abschneiden: erst Leerzeichen weg ("RJX 1234" -> "RJX1234"), dann kleinere Schrift
    if (g.textWidth(ln) > BADGE_W - 6) ln.replace(" ", "");
    if (g.textWidth(ln) > BADGE_W - 6) g.setTextFont(1);
    ln = fit(g, ln, BADGE_W - 4);
  }
  g.setTextDatum(MC_DATUM);
  g.setTextColor(TFT_WHITE, badge);
  g.drawString(ln, COL_BADGE + BADGE_W / 2, cy);
  g.setTextFont(2);

  // Gleis/Steig (breitere Spalte, z.B. "2A-C")
  int destRight = PLAT_RIGHT;
  if (d.platform[0]) {
    String pl = translit(d.platform);
    int w = g.textWidth(pl);
    if (w > PLAT_W) { g.setTextFont(1); pl = fit(g, pl, PLAT_W); w = g.textWidth(pl); }
    g.setTextDatum(MR_DATUM);
    g.setTextColor(pal.sub, bg);
    g.drawString(pl, PLAT_RIGHT, cy);
    g.setTextFont(2);
    destRight = PLAT_RIGHT - w - 6;
  }

  // Ziel
  g.setTextDatum(ML_DATUM);
  // geändertes Ziel gelb hervorheben, mit "!" davor
  String dest = translit(d.dest);
  if (d.destChanged && !d.cancelled) dest = "! " + dest;
  g.setTextColor(d.cancelled ? C_RED : (d.destChanged ? C_YELLOW : pal.text), bg);
  g.drawString(fit(g, dest, destRight - COL_DEST), COL_DEST, cy);

  // Countdown
  g.setTextDatum(MR_DATUM);
  if (d.cancelled) {
    g.setTextColor(C_RED, bg);
    g.drawString("-", SCR_W - 4, cy);
  } else if (tv && d.when) {
    long mins = (long)(d.when - now) / 60;
    String t;
    if (mins <= 0) t = "jetzt";
    else if (mins < 60) t = String(mins) + "'";
    if (t.length()) {
      g.setTextFont(mins <= 0 ? 1 : 2);
      g.setTextColor(mins <= 2 ? pal.accent : pal.text, bg);
      g.drawString(t, SCR_W - 4, cy);
    }
  }
}

void displayBoard(const Board& b, const Station& st, uint8_t idx, uint8_t total) {
  pal = makePal(&st);
  time_t now = time(nullptr);
  bool tv = timeValid();
  struct tm lt;
  localtime_r(&now, &lt);

  // ---- Kopfzeile: Haltestelle + Uhr
  tft.fillRect(0, 0, SCR_W, HEADER_H, pal.header);
  tft.drawFastHLine(0, HEADER_H, SCR_W, pal.accent);
  tft.drawFastHLine(0, HEADER_H + 1, SCR_W, pal.bg);
  tft.setTextFont(4);
  char clk[6] = "--:--";
  if (tv) strftime(clk, sizeof(clk), "%H:%M", &lt);
  int clkW = tft.textWidth(clk);
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(pal.accent, pal.header);
  tft.drawString(clk, SCR_W - 6, HEADER_H / 2 + 1);
  // roter Punkt neben der Uhr: letzte Abfrage fehlgeschlagen, es werden ältere Daten gezeigt
  if (!b.ok && b.error[0] && b.fetchedMs) {
    tft.fillCircle(SCR_W - clkW - 14, HEADER_H / 2, 3, C_RED);
    clkW += 10;
  }
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(pal.text, pal.header);
  String name = st.name[0] ? String(st.name) : String(b.stationName);
  tft.drawString(fit(tft, translit(name), SCR_W - clkW - 22), 6, HEADER_H / 2 + 1);

  // ---- Abfahrten
  int shown = min((int)b.count, MAX_ROWS);
  for (int i = 0; i < MAX_ROWS; i++) {
    int y = ROWS_Y + i * ROW_H;
    if (i < shown) {
      if (rowSpriteOk) {
        drawRow(row, 0, b.deps[i], i & 1, now, tv);
        row.pushSprite(0, y);
      } else {
        drawRow(tft, y, b.deps[i], i & 1, now, tv);
      }
    } else {
      tft.fillRect(0, y, SCR_W, ROW_H, pal.bg);
    }
  }
  if (shown == 0) {
    tft.setTextFont(2);
    tft.setTextDatum(MC_DATUM);
    const char* msg;
    if (b.fetchedMs == 0) msg = b.error[0] ? b.error : "Lade Abfahrten ...";
    else msg = "Keine Abfahrten";
    tft.setTextColor(b.fetchedMs == 0 && b.error[0] ? C_RED : pal.sub, pal.bg);
    tft.drawString(translit(msg), SCR_W / 2, ROWS_Y + 3 * ROW_H);
  }

  // Rest unter der letzten Zeile löschen (z.B. Reste der Wagenreihungs-Ansicht)
  int yEnd = ROWS_Y + MAX_ROWS * ROW_H;
  if (yEnd < SCR_H) tft.fillRect(0, yEnd, SCR_W, SCR_H - yEnd, pal.bg);
}

// ---------------------------------------------------------------- Wagenreihung

void displayFormationLoading(const Departure& d, const Station& st) {
  pal = makePal(&st);
  tft.fillScreen(pal.bg);
  tft.fillRect(0, 0, SCR_W, HEADER_H, pal.header);
  tft.drawFastHLine(0, HEADER_H, SCR_W, pal.accent);
  tft.setTextFont(4);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(pal.text, pal.header);
  tft.drawString(fit(tft, translit(String(d.line) + "  " + d.dest), SCR_W - 12), 6, HEADER_H / 2 + 1);
  tft.setTextFont(2);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(pal.sub, pal.bg);
  tft.drawString("Lade Wagenreihung ...", SCR_W / 2, SCR_H / 2);
}

static void drawLoad(int x, int y, uint8_t load) {
  // 3 Kästchen, gefüllt bis zur Auslastungsstufe
  uint16_t col = load >= 3 ? C_RED : (load == 2 ? C_YELLOW : C_GREEN);
  for (int i = 0; i < 3; i++) {
    if (i < load) tft.fillRect(x + i * 5, y, 4, 4, col);
    else tft.drawRect(x + i * 5, y, 4, 4, C_DIM);
  }
}

void displayFormation(const Formation& f, const Departure& d, const Station& st) {
  pal = makePal(&st);
  tft.fillScreen(pal.bg);

  // ---- Kopfzeile: Zug, Ziel, Gleis
  tft.fillRect(0, 0, SCR_W, HEADER_H, pal.header);
  tft.drawFastHLine(0, HEADER_H, SCR_W, pal.accent);
  tft.setTextFont(4);
  String gl = String("Gl. ") + (f.platform[0] ? f.platform : d.platform);
  int glW = tft.textWidth(gl);
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(pal.accent, pal.header);
  tft.drawString(gl, SCR_W - 6, HEADER_H / 2 + 1);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(pal.text, pal.header);
  tft.drawString(fit(tft, translit(String(d.line) + "  " + d.dest), SCR_W - glW - 22), 6, HEADER_H / 2 + 1);

  // ---- Unterzeile: Abfahrt + Bahnhof
  tft.setTextFont(1);
  tft.setTextDatum(TL_DATUM);
  String sub = String("ab ") + d.time;
  if (d.delay > 0) sub += " (+" + String(d.delay) + ")";
  sub += "   " + String(f.title);
  tft.setTextColor(pal.sub, pal.bg);
  tft.drawString(fit(tft, translit(sub), SCR_W - 8), 4, HEADER_H + 5);

  if (!f.ok) {
    tft.setTextFont(2);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_RED, pal.bg);
    tft.drawString(translit(f.error[0] ? f.error : "Keine Wagenreihung"), SCR_W / 2, SCR_H / 2);
    tft.setTextFont(1);
    tft.setTextColor(C_DIM, pal.bg);
    tft.drawString("Tippen zum Schliessen", SCR_W / 2, SCR_H - 10);
    return;
  }

  // ---- Wagen: eine oder zwei Reihen
  int n = f.count;
  int perRow = n <= 12 ? n : (n + 1) / 2;
  int gap = 2;
  int w = min(40, (SCR_W - 8 - (perRow - 1) * gap) / max(1, perRow));
  int rowW = perRow * w + (perRow - 1) * gap;
  int x0 = (SCR_W - rowW) / 2;
  const int rowH = 66;
  int y0 = HEADER_H + 22;

  uint16_t coach = tft.color565(225, 228, 235);
  uint16_t coachText = TFT_BLACK;
  uint16_t engine = tft.color565(150, 25, 35);
  uint16_t dining = tft.color565(130, 70, 40);

  for (int r = 0; r * perRow < n; r++) {
    int ry = y0 + r * rowH;
    // Zugteil-Ziele über den Wagen
    if (f.partCount > 1) {
      int i = r * perRow, end = min(n, (r + 1) * perRow);
      while (i < end) {
        int p = f.wagons[i].part, j = i;
        while (j < end && f.wagons[j].part == p) j++;
        int xa = x0 + (i - r * perRow) * (w + gap), xb = x0 + (j - r * perRow) * (w + gap) - gap;
        tft.drawFastHLine(xa, ry + 4, xb - xa, pal.accent);
        tft.setTextFont(1);
        tft.setTextDatum(TC_DATUM);
        tft.setTextColor(pal.accent, pal.bg);
        tft.drawString(fit(tft, translit(String("> ") + f.parts[p]), max(10, xb - xa)), (xa + xb) / 2, ry - 6);
        i = j;
      }
    }
    for (int k = 0; k < perRow; k++) {
      int i = r * perRow + k;
      if (i >= n) break;
      const Wagon& g = f.wagons[i];
      int x = x0 + k * (w + gap);
      int by = ry + 8, bh = 30;
      if (g.kind == W_ENGINE) {
        tft.fillRoundRect(x, by + 4, w, bh - 4, 5, engine);
        continue;
      }
      bool isDining = g.features & 64;
      uint16_t fill = isDining ? dining : coach;
      tft.fillRoundRect(x, by, w, bh, g.kind == W_CAB ? 6 : 2, fill);
      if (g.first > 0 || g.business > 0) tft.fillRect(x + 1, by + 1, w - 2, 4, C_YELLOW);   // 1. Klasse gelb markiert
      // Klasse
      String cls = g.business > 0 ? "B" : (g.first > 0 ? "1" : (g.second > 0 ? "2" : ""));
      if (isDining) cls = g.first > 0 ? "1S" : "S";
      tft.setTextFont(2);
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(isDining ? TFT_WHITE : coachText, fill);
      if (tft.textWidth(cls) > w - 2) tft.setTextFont(1);
      tft.drawString(cls, x + w / 2, by + 14);
      // Symbole unten im Wagen: F = Fahrrad, R = Rollstuhl, Q = Ruhezone, K = Familie
      String ic;
      if (g.bikes) ic += "F";
      if (g.wheelchairs) ic += "R";
      if (g.features & 128) ic += "Q";
      if (g.features & 32) ic += "K";
      if (ic.length()) {
        tft.setTextFont(1);
        tft.setTextColor(isDining ? TFT_WHITE : tft.color565(0, 90, 170), fill);
        tft.drawString(fit(tft, ic, w - 2), x + w / 2, by + bh - 5);
      }
      // Wagennummer + Auslastung darunter
      if (g.nr) {
        tft.setTextFont(1);
        tft.setTextColor(pal.text, pal.bg);
        tft.drawString(String(g.nr), x + w / 2, by + bh + 7);
      }
      if (f.loadInfo && g.load) drawLoad(x + w / 2 - 7, by + bh + 13, g.load);
    }
  }

  // ---- Legende
  tft.setTextFont(1);
  tft.setTextDatum(BL_DATUM);
  tft.setTextColor(C_DIM, pal.bg);
  tft.drawString("1/2 Klasse  B Business  S Speisewagen  gelb = 1. Kl.", 4, SCR_H - 13);
  tft.drawString("F Rad  R Rollstuhl  Q Ruhe  K Familie", 4, SCR_H - 3);
  if (f.loadInfo) {
    tft.setTextDatum(BR_DATUM);
    tft.drawString("Auslastung", SCR_W - 26, SCR_H - 3);
    drawLoad(SCR_W - 20, SCR_H - 8, 2);
  }
}
