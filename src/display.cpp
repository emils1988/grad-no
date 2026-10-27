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

// Rohwerte des Touch-Controllers (ggf. anpassen, für Links/Rechts-Tippen reicht grob)
#define TOUCH_X_MIN 200
#define TOUCH_X_MAX 3700
#define TOUCH_Y_MIN 240
#define TOUCH_Y_MAX 3800

#define SCR_W 320
#define SCR_H 240
#define HEADER_H 34
#define FOOTER_H 18
#define ROW_H 26
#define ROWS_Y (HEADER_H + 2)
#define MAX_ROWS ((SCR_H - HEADER_H - 2 - FOOTER_H) / ROW_H)   // = 7

static TFT_eSPI tft;
static TFT_eSprite row(&tft);
static bool rowSpriteOk = false;
static SPIClass touchSpi(VSPI);
static XPT2046_Touchscreen ts(XPT_CS, XPT_IRQ);

static uint16_t C_AMBER, C_HEADER, C_ROWALT, C_GREY, C_DIM, C_RED, C_GREEN;
static uint16_t C_TRAIN, C_SBAHN, C_BUS;

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

  C_AMBER  = tft.color565(255, 176, 0);
  C_HEADER = tft.color565(22, 24, 34);
  C_ROWALT = tft.color565(14, 15, 20);
  C_GREY   = tft.color565(140, 140, 150);
  C_DIM    = tft.color565(70, 70, 80);
  C_RED    = tft.color565(255, 70, 60);
  C_GREEN  = tft.color565(80, 200, 110);
  C_TRAIN  = tft.color565(170, 25, 35);
  C_SBAHN  = tft.color565(0, 100, 185);
  C_BUS    = tft.color565(75, 78, 92);

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

// Text auf maximale Breite kürzen ("Klagenfurt Hbf" -> "Klagenfu..")
static String fit(TFT_eSPI& g, String s, int maxW) {
  if (g.textWidth(s) <= maxW) return s;
  while (s.length() > 1 && g.textWidth(s + "..") > maxW) s.remove(s.length() - 1);
  s.trim();
  return s + "..";
}

void displayMessage(const char* title, const char* l1, const char* l2, const char* l3, const char* l4) {
  tft.fillScreen(TFT_BLACK);
  tft.fillRect(0, 0, SCR_W, HEADER_H, C_HEADER);
  tft.drawFastHLine(0, HEADER_H, SCR_W, C_AMBER);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(4);
  tft.setTextColor(C_AMBER, C_HEADER);
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

static bool isBus(const char* line) {
  String p(line);
  p.toLowerCase();
  return p.startsWith("bus") || p.startsWith("ast") || p.startsWith("tram") || p.startsWith("str") ||
         p.startsWith("nachtbus") || p.startsWith("citybus") || p.startsWith("rufbus") ||
         (p.length() >= 2 && p[0] == 'u' && (isdigit((uint8_t)p[1]) || p[1] == ' '));
}

static void drawRow(TFT_eSPI& g, int y, const Departure& d, bool alt, time_t now, bool tv) {
  uint16_t bg = alt ? C_ROWALT : TFT_BLACK;
  int cy = y + ROW_H / 2;
  g.fillRect(0, y, SCR_W, ROW_H, bg);
  g.setTextFont(2);

  // geplante Zeit
  g.setTextDatum(ML_DATUM);
  g.setTextColor(d.cancelled ? C_DIM : C_AMBER, bg);
  g.drawString(d.time, 4, cy);

  // Verspätung
  if (d.cancelled) {
    g.setTextColor(C_RED, bg);
    g.drawString("X", 44, cy);
  } else if (d.delay > 0) {
    g.setTextColor(C_RED, bg);
    g.drawString("+" + String(d.delay), 42, cy);
  } else if (d.delay == 0) {
    g.fillCircle(46, cy, 2, C_GREEN);   // Echtzeit: pünktlich
  }

  // Linien-Badge
  const int bx = 66, bw = 56;
  bool bus = isBus(d.line);
  uint16_t badge = bus ? C_BUS : (d.line[0] == 'S' && (d.line[1] == ' ' || isdigit((uint8_t)d.line[1])) ? C_SBAHN : C_TRAIN);
  g.fillRoundRect(bx, y + 4, bw, ROW_H - 8, 4, badge);
  String ln(d.line);
  if (g.textWidth(ln) > bw - 6) {
    if (bus) { ln.replace("Bus ", ""); ln.replace("BUS ", ""); }
    if (g.textWidth(ln) > bw - 6) { int sp = ln.indexOf(' '); if (sp > 0) ln = ln.substring(0, sp); }
    ln = fit(g, ln, bw - 6);
  }
  g.setTextDatum(MC_DATUM);
  g.setTextColor(TFT_WHITE, badge);
  g.drawString(ln, bx + bw / 2, cy);

  // Gleis/Steig
  const int platRight = 280;
  bool hasPlat = d.platform[0] != 0;
  if (hasPlat) {
    g.setTextDatum(MR_DATUM);
    g.setTextColor(C_GREY, bg);
    g.drawString(fit(g, translit(d.platform), 24), platRight, cy);
  }

  // Ziel
  const int dx = 128;
  int maxW = (hasPlat ? platRight - 28 : platRight) - dx;
  g.setTextDatum(ML_DATUM);
  g.setTextColor(d.cancelled ? C_RED : TFT_WHITE, bg);
  g.drawString(fit(g, translit(d.dest), maxW), dx, cy);

  // Countdown
  g.setTextDatum(MR_DATUM);
  if (d.cancelled) {
    g.setTextColor(C_RED, bg);
    g.drawString("faellt", SCR_W - 4, cy);
  } else if (tv && d.when) {
    long mins = (long)(d.when - now) / 60;
    String t;
    if (mins <= 0) t = "jetzt";
    else if (mins < 60) t = String(mins) + "'";
    if (t.length()) {
      g.setTextColor(mins <= 2 ? C_AMBER : TFT_WHITE, bg);
      g.drawString(t, SCR_W - 4, cy);
    }
  }
}

void displayBoard(const Board& b, const Station& st, uint8_t idx, uint8_t total) {
  time_t now = time(nullptr);
  bool tv = timeValid();
  struct tm lt;
  localtime_r(&now, &lt);

  // ---- Kopfzeile: Haltestelle + Uhr
  tft.fillRect(0, 0, SCR_W, HEADER_H, C_HEADER);
  tft.drawFastHLine(0, HEADER_H, SCR_W, C_AMBER);
  tft.drawFastHLine(0, HEADER_H + 1, SCR_W, TFT_BLACK);
  tft.setTextFont(4);
  char clk[6] = "--:--";
  if (tv) strftime(clk, sizeof(clk), "%H:%M", &lt);
  int clkW = tft.textWidth(clk);
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(C_AMBER, C_HEADER);
  tft.drawString(clk, SCR_W - 6, HEADER_H / 2 + 1);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(TFT_WHITE, C_HEADER);
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
      tft.fillRect(0, y, SCR_W, ROW_H, TFT_BLACK);
    }
  }
  if (shown == 0) {
    tft.setTextFont(2);
    tft.setTextDatum(MC_DATUM);
    const char* msg;
    if (b.fetchedMs == 0) msg = b.error[0] ? b.error : "Lade Abfahrten ...";
    else msg = "Keine Abfahrten";
    tft.setTextColor(b.fetchedMs == 0 && b.error[0] ? C_RED : C_GREY, TFT_BLACK);
    tft.drawString(translit(msg), SCR_W / 2, ROWS_Y + 3 * ROW_H);
  }

  // ---- Fußzeile: Status + Seitenpunkte
  int fy = SCR_H - FOOTER_H;
  tft.fillRect(0, fy, SCR_W, FOOTER_H, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextDatum(ML_DATUM);
  String status;
  if (!b.ok && b.error[0] && b.fetchedMs) {
    tft.setTextColor(C_RED, TFT_BLACK);
    status = "! " + String(b.error);
  } else if (b.fetchedMs && tv) {
    struct tm ft;
    time_t fa = b.fetchedAt;
    localtime_r(&fa, &ft);
    char buf[16];
    strftime(buf, sizeof(buf), "Stand %H:%M", &ft);
    tft.setTextColor(C_DIM, TFT_BLACK);
    status = buf;
  }
  tft.drawString(fit(tft, translit(status), 120), 4, fy + FOOTER_H / 2);

  if (total > 1) {
    int dotsW = total * 12;
    int x0 = SCR_W / 2 - dotsW / 2 + 6;
    for (int i = 0; i < total; i++) {
      if (i == idx) tft.fillCircle(x0 + i * 12, fy + FOOTER_H / 2, 3, C_AMBER);
      else tft.drawCircle(x0 + i * 12, fy + FOOTER_H / 2, 3, C_DIM);
    }
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(C_DIM, TFT_BLACK);
    tft.drawString("< tippen >", SCR_W - 4, fy + FOOTER_H / 2);
  }
}
