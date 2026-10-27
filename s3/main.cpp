// Waveshare ESP32-S3-Touch-LCD-1.69 buddy, over USB or Bluetooth.
//
// A stripped-down firmware for the S3 board: no Wi-Fi, no pet, no stats screens.
// It speaks the same newline-delimited JSON as the other builds over the USB
// serial port and over Bluetooth LE (Nordic UART, src/ble_bridge.cpp), so it
// works with host/buddy_bridge.py and with the Claude desktop app's Hardware
// Buddy window. USB wins: while the bridge is talking over the cable,
// Bluetooth heartbeats are ignored, and answers go back on the link a prompt
// came from. Hold the screen (no prompt showing) for the menu. The buzzer chimes
// when Claude needs you and gives feedback for touches; its volume is a setting.
// The panel has rounded corners (about 32 px): nothing important is drawn in them.
//
// Power: on battery the board turns itself off after a while with no bridge
// (setting "auto off"); on USB the screen dims after a while with nothing going
// on ("dim after"). Holding the power key, or the pill in the menu, asks to turn
// off; on USB "off" is a deep sleep that a long press of the power key ends.
// Double-pressing the power key locks the screen (a touch then says so) and
// double-pressing it again unlocks. A locked screen still lets you answer a
// permission prompt.
// One screen, laid out like the other builds' corner layout: a small Clawd top
// left, the status box ("idle", "thinking", "coding", "need input", "offline")
// top right, the transcript below with only the newest line highlighted. When
// Claude asks for permission the transcript turns into the approval prompt: a
// tap on the screen approves it and a long press denies it.
//
// The Clawd faces come from the Claudinho project (github.com/argeuthiesen/claudinho).
//
// Pins (the V2.1 board; the first revision, BOARD_WS_S3_V1, differs in the
// power latch and buzzer, see waveshare_lcd_169.h in ../src/board):
//   LCD SCK 6, MOSI 7, DC 4, CS 5, RST 8, backlight 15; I2C SDA 11, SCL 10;
//   touch RST 13 (CST816 at 0x15).
#include <Arduino.h>
#include <Wire.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <ArduinoJson.h>
#include <time.h>
#include <Preferences.h>
#include <esp_mac.h>
#include <esp_sleep.h>
#include "../src/ble_bridge.h"
#include "fonts.h"

#if defined(BOARD_WS_S3_V1)
constexpr int PIN_LATCH = 35, PIN_BUZZER = 33, PIN_PWR = 36;
#else
constexpr int PIN_LATCH = 41, PIN_BUZZER = 42, PIN_PWR = 40;   // PWR key: active low
#endif

constexpr int W = 280, H = 240;                    // landscape
constexpr uint32_t LINK_TIMEOUT_MS = 30000;        // no heartbeat for this long: bridge is gone
constexpr uint32_t DENY_HOLD_MS = 1200;            // a touch this long denies instead of approving
constexpr uint32_t PWR_HOLD_MS = 1200;             // holding the power key this long asks to turn off
constexpr uint32_t PWR_DOUBLE_MS = 500;            // two presses within this unlock the screen
constexpr uint32_t CONFIRM_MS = 15000;             // the shutdown question cancels itself after this
constexpr uint32_t MENU_HOLD_MS = 700;            // holding the screen this long (no prompt showing) opens the menu
constexpr uint32_t USB_PRIORITY_MS = 10000;        // USB counts as live this long after its last line
constexpr uint32_t PROMPT_SETTLE_MS = 400;         // touches that began earlier than this after a prompt appeared are ignored
constexpr int CORNER_W = 74, CORNER_H = 72;        // pet strip: Clawd on the left, status box to its right
constexpr int LH = 28;                             // transcript row (DM Sans, 26 px)
// The message area under the pet strip. While action is needed a flashing box surrounds it
// and the text moves inside the border: fewer rows, a wider margin.
constexpr int BOX_X = 4, BOX_Y = CORNER_H - 2, BOX_W = W - 8, BOX_H = H - CORNER_H - 1, BOX_BORDER = 12, BOX_R = 28;
static bool boxOn = false;
static int padX = 4, msgTop = H - ((H - CORNER_H) / LH) * LH, msgRows = (H - CORNER_H) / LH;
#define TEXT_W (W - 2 * padX)
static void layoutMessages(bool box) {
  boxOn = box;
  if (box) { padX = BOX_X + BOX_BORDER + 4; msgTop = BOX_Y + BOX_BORDER + 2; msgRows = (BOX_Y + BOX_H - BOX_BORDER - msgTop) / LH; }
  else { padX = 4; msgRows = (H - CORNER_H) / LH; msgTop = H - msgRows * LH; }
}

// ───────────── display ─────────────
class Panel : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 panel; lgfx::Bus_SPI bus; lgfx::Light_PWM light;
 public:
  Panel() {
    { auto c = bus.config();
      c.spi_host = SPI2_HOST; c.spi_mode = 0; c.freq_write = 80000000;
      c.pin_sclk = 6; c.pin_mosi = 7; c.pin_miso = -1; c.pin_dc = 4;
      bus.config(c); panel.setBus(&bus); }
    { auto c = panel.config();
      c.pin_cs = 5; c.pin_rst = 8; c.pin_busy = -1;
      c.memory_width = 240; c.memory_height = 320;     // the ST7789 has 320 rows; the glass shows 280
      c.panel_width = 240; c.panel_height = 280; c.offset_y = 20;
      c.invert = true; c.rgb_order = true;             // glass is BGR: without this orange comes out blue
      c.readable = false; c.bus_shared = false;
      panel.config(c); }
    { auto c = light.config(); c.pin_bl = 15; c.invert = false; c.freq = 5000; c.pwm_channel = 0;
      light.config(c); panel.setLight(&light); }
    setPanel(&panel);
  }
};
static Panel lcd;
static lgfx::LGFX_Sprite spr(&lcd);                // the whole frame; pushed when something changed

static const uint16_t BLACK = 0, TEXT = 0xFFFF, DIM = 0x8410, HOT = 0xFA20;   // HOT: red-orange, same as the other builds
static const uint16_t BODY = 0xF2A3, EYE = 0x20E3;                                 // Clawd orange, eye black
// One colour per session, in the order the bridge first saw them.
static const uint16_t SESSION_COLOR[6] = {0x07FF, 0xF81F, 0xFD20, 0xBFE0, 0x5D9F, 0xFB56};
static const uint16_t NAVY = 0x08C8;                // dark blue: the message area while idle
static uint16_t textBg = BLACK;                    // what the text being drawn sits on (text is drawn opaque, so it has to match)
static const uint16_t GREEN = 0x07E0, RED = 0xF800, YELLOW = 0xFFE0, SKY = 0x867D;

// ───────────── settings (kept in flash, namespace "buddy" like the other builds) ─────────────
static Preferences prefs;
static struct { bool bt = true, hud = true, flip = true; uint8_t bright = 4, vol = 4, offMin = 10, dimMin = 30; } cfg;   // bright 0..4 -> 20..100 %; vol 0 (off)..4; flip: rotated 180 degrees
static char petName[24] = "Buddy", ownerName[32] = "", btName[16] = "Claude";
static uint16_t approvals = 0, denials = 0;
static void cfgLoad() {
  prefs.begin("buddy", true);
  cfg.bt = prefs.getBool("s_bt", true); cfg.hud = prefs.getBool("s_hud", true); cfg.flip = prefs.getBool("flip", true);
  cfg.bright = min<uint8_t>(prefs.getUChar("bright", 4), 4); cfg.vol = min<uint8_t>(prefs.getUChar("vol5b", 4), 4);
  cfg.offMin = prefs.getUChar("offmin", 10); cfg.dimMin = prefs.getUChar("dimmin", 30);
  prefs.getString("petname", petName, sizeof petName); prefs.getString("owner", ownerName, sizeof ownerName);
  prefs.end();
}
static void cfgSave() {
  prefs.begin("buddy", false);
  prefs.putBool("s_bt", cfg.bt); prefs.putBool("s_hud", cfg.hud); prefs.putBool("flip", cfg.flip);
  prefs.putUChar("bright", cfg.bright); prefs.putUChar("vol5b", cfg.vol);
  prefs.putUChar("offmin", cfg.offMin); prefs.putUChar("dimmin", cfg.dimMin);
  prefs.end();
}
// Names go into hand-built JSON: drop quotes, backslashes and control characters.
static void safeCopy(char* dst, size_t n, const char* src) {
  size_t j = 0;
  for (size_t i = 0; src[i] && j < n - 1; i++) if (src[i] != '"' && src[i] != '\\' && (uint8_t)src[i] >= 0x20) dst[j++] = src[i];
  dst[j] = 0;
}

// ───────────── what the bridge tells us ─────────────
enum Src : uint8_t { SRC_NONE, SRC_USB, SRC_BLE };
struct Prompt { char id[40], tool[20], hint[44], sess[12]; uint8_t sc; Src src; };
static struct {
  uint8_t running = 0, waiting = 0;
  Prompt prompt = {};
  char lines[8][92] = {}; uint8_t nLines = 0;        // oldest first, newest last
  char sess[8][12] = {}; uint8_t sc[8] = {};         // which session each line came from (empty when only one is open)
  int8_t cx[8] = {};                                 // context % of that session, -1 unknown
  uint16_t lineGen = 0;                              // bumps when the lines change
  uint32_t lastLineMs = 0;                           // when the newest line arrived
  char msg[48] = "";
  uint32_t lastHbMs = 0;
} data;

static bool linkUp() { return data.lastHbMs && millis() - data.lastHbMs < LINK_TIMEOUT_MS; }

// Which transport is live: USB while its lines keep coming, else Bluetooth.
static uint32_t usbMs = 0, bleMs = 0;
static bool usbLive() { return usbMs && millis() - usbMs < USB_PRIORITY_MS; }
static Src activeLink() { return usbLive() ? SRC_USB : (bleMs && millis() - bleMs < LINK_TIMEOUT_MS) ? SRC_BLE : SRC_NONE; }

static void sendLine(Src to, const char* json) {
  char out[600]; int n = snprintf(out, sizeof out, "%s\n", json);
  if (to == SRC_BLE) bleWrite((const uint8_t*)out, n); else Serial.write((const uint8_t*)out, n);
}

static bool onUsb();
// Battery. The only sense line is the cell voltage on GPIO1 (through a 1:3 divider); the
// charger chip (ETA6098) has no status pin wired to the ESP32. So charging is inferred:
// on a computer's USB the cell is being topped up until it reads full (>= 4.15 V), and a
// voltage that keeps rising over a minute means a charger even without data. A falling
// voltage means it isn't charging. Readings are averaged over 2 s samples.
static struct { int mv = 0, pct = -1; bool charging = false, full = false; } bat;   // pct -1: no battery found
static int batteryMv() { return analogReadMilliVolts(1) * 3; }
static int pctOfMv(int mv) {                         // a Li-ion discharge curve, roughly
  static const int V[] = {3300, 3500, 3600, 3700, 3800, 3900, 4000, 4100, 4200};
  static const int P[] = {0, 5, 15, 30, 50, 65, 80, 92, 100};
  if (mv <= V[0]) return 0;
  for (int i = 1; i < 9; i++) if (mv <= V[i]) return P[i - 1] + (P[i] - P[i - 1]) * (mv - V[i - 1]) / (V[i] - V[i - 1]);
  return 100;
}
static void batteryTick() {
  static uint32_t last = 0, trendAt = 0; static int trendMv = 0, avg = 0;
  if (last && millis() - last < 2000) return;
  last = millis();
  int mv = 0; for (int i = 0; i < 8; i++) mv += batteryMv(); mv /= 8;
  avg = avg ? (avg * 3 + mv) / 4 : mv;
  bat.mv = avg;
  bat.pct = avg < 2800 ? -1 : pctOfMv(avg);          // far too low for a cell: nothing connected
  if (!trendAt) { trendAt = millis(); trendMv = avg; }
  static int trend = 0;                              // mV change over the last minute
  if (millis() - trendAt >= 60000) { trend = avg - trendMv; trendAt = millis(); trendMv = avg; }
  bat.full = bat.pct >= 0 && avg >= 4150;
  bat.charging = bat.pct >= 0 && !bat.full && ((onUsb() && trend > -10) || trend >= 15);
}

// Commands from the bridge or the desktop app's Hardware Buddy window; every one gets an ack.
// The folder push (char_begin ...) isn't supported: unacked, the desktop gives up on its own.
static void handleCommand(JsonDocument& doc, Src src) {
  const char* cmd = doc["cmd"] | "";
  char out[600];
  if (!strcmp(cmd, "status")) {
    int mv = bat.mv ? bat.mv : batteryMv(), pct = bat.pct >= 0 ? bat.pct : 0;
    snprintf(out, sizeof out,
      "{\"ack\":\"status\",\"ok\":true,\"n\":0,\"data\":{\"name\":\"%s\",\"owner\":\"%s\",\"sec\":%s,"
      "\"bat\":{\"pct\":%d,\"mV\":%d,\"mA\":%d,\"usb\":%s},\"sys\":{\"up\":%lu,\"heap\":%u},"
      "\"stats\":{\"appr\":%u,\"deny\":%u,\"vel\":0,\"nap\":0,\"lvl\":0}}}",
      petName, ownerName, bleSecure() ? "true" : "false", pct, mv, bat.charging ? -100 : 0, onUsb() ? "true" : "false",
      millis() / 1000, (unsigned)ESP.getFreeHeap(), approvals, denials);
    sendLine(src, out);
  } else if (!strcmp(cmd, "name") || !strcmp(cmd, "owner")) {
    const char* v = doc["name"];
    bool isName = !strcmp(cmd, "name");
    if (v) {
      safeCopy(isName ? petName : ownerName, isName ? sizeof petName : sizeof ownerName, v);
      prefs.begin("buddy", false); prefs.putString(isName ? "petname" : "owner", isName ? petName : ownerName); prefs.end();
    }
    snprintf(out, sizeof out, "{\"ack\":\"%s\",\"ok\":%s,\"n\":0}", cmd, v ? "true" : "false");
    sendLine(src, out);
  } else if (!strcmp(cmd, "unpair")) {
    bleClearBonds();
    sendLine(src, "{\"ack\":\"unpair\",\"ok\":true,\"n\":0}");
  }
}

static void applyLine(const char* line, Src src) {
  JsonDocument doc;
  if (deserializeJson(doc, line)) return;
  if (!doc["cmd"].isNull()) { handleCommand(doc, src); return; }
  if (src == SRC_BLE && usbLive()) return;            // USB wins
  if (!doc["time"].isNull()) { data.lastHbMs = millis(); return; }   // clock sync: the buddy has no clock face
  data.running = doc["running"] | 0; data.waiting = doc["waiting"] | 0;
  strlcpy(data.msg, doc["msg"] | "", sizeof data.msg);
  JsonArray en = doc["entries"];
  if (!en.isNull()) {
    uint8_t n = 0; bool changed = false;
    for (JsonVariant v : en) {
      if (n >= 8) break;
      const char* e = v.as<const char*>(); if (!e) e = "";
      if (n >= data.nLines || strncmp(data.lines[n], e, 91) != 0) changed = true;   // heartbeats repeat the same lines
      strlcpy(data.lines[n++], e, 92);
    }
    if (changed || n != data.nLines) { data.lineGen++; data.lastLineMs = millis(); }
    data.nLines = n;
    JsonArray ss = doc["sess"], cc = doc["sc"], cx = doc["cx"];      // parallel to entries; absent with a single session
    for (uint8_t i = 0; i < n; i++) {
      strlcpy(data.sess[i], ss.isNull() ? "" : (ss[i] | ""), sizeof data.sess[i]);
      data.sc[i] = cc.isNull() ? 0 : (cc[i] | 0);
      data.cx[i] = cx.isNull() ? -1 : (cx[i] | -1);
    }
  }
  JsonObject pr = doc["prompt"];
  if (pr.isNull()) data.prompt = {};
  else {
    strlcpy(data.prompt.id, pr["id"] | "", sizeof data.prompt.id);
    strlcpy(data.prompt.tool, pr["tool"] | "", sizeof data.prompt.tool);
    strlcpy(data.prompt.hint, pr["hint"] | "", sizeof data.prompt.hint);
    strlcpy(data.prompt.sess, pr["sess"] | "", sizeof data.prompt.sess);
    data.prompt.sc = pr["sc"] | 0;
    data.prompt.src = src;                            // the answer goes back the way the question came
  }
  data.lastHbMs = millis();
}

// Lines from USB (the bridge writes in 64-byte pieces) and from Bluetooth; the end of a line is the \n.
struct LineBuf {
  char buf[1536]; size_t n = 0; bool overflow = false;
  void feed(char c, Src src) {
    if (c == '\r') return;
    if (c != '\n') { if (n < sizeof buf - 1) buf[n++] = c; else overflow = true; return; }
    buf[n] = 0;
    if (!overflow && buf[0] == '{') { (src == SRC_USB ? usbMs : bleMs) = millis(); applyLine(buf, src); }
    n = 0; overflow = false;
  }
};
static LineBuf usbLine, bleLine;
static void pollLinks() {
  while (Serial.available()) usbLine.feed(Serial.read(), SRC_USB);
  while (bleAvailable()) { int c = bleRead(); if (c < 0) break; if (bleEnabled()) bleLine.feed((char)c, SRC_BLE); }
}

// ───────────── touch (CST816 at 0x15) ─────────────
static bool touchOk = false;
static void touchBegin() {
  Wire.begin(11, 10); Wire.setClock(400000);
  pinMode(13, OUTPUT); digitalWrite(13, LOW); delay(10); digitalWrite(13, HIGH); delay(50);
  Wire.beginTransmission(0x15); touchOk = Wire.endTransmission() == 0;
  if (touchOk) { Wire.beginTransmission(0x15); Wire.write(0xFE); Wire.write(0x01); Wire.endTransmission(); }   // don't auto-sleep
}
// True while a finger is down, with its position in screen pixels (280 x 240, landscape,
// in the orientation the panel is showing). Polled at most every 15 ms.
static bool touchRead(int& ux, int& uy) {
  static bool down = false; static uint32_t last = 0; static int px = 0, py = 0;
  if (touchOk && millis() - last >= 15) {
    last = millis();
    uint8_t d[5] = {0};
    Wire.beginTransmission(0x15); Wire.write(0x02);
    if (Wire.endTransmission(false) == 0 && Wire.requestFrom(0x15, 5) == 5) {
      for (int i = 0; i < 5; i++) d[i] = Wire.read();
      int x = ((d[1] & 0x0F) << 8) | d[2], y = ((d[3] & 0x0F) << 8) | d[4];   // 0..239 x 0..279, portrait
      uint8_t fingers = d[0] & 0x0F;
      down = fingers > 0 && fingers <= 2 && x < 240 && y < 280;               // outside the glass = ghost touch
      if (down) { px = cfg.flip ? 279 - y : y; py = cfg.flip ? x : 239 - x; }
    }
  }
  ux = px; uy = py;
  return down;
}

// ───────────── buzzer ─────────────
// A passive piezo on GPIO42 (GPIO33 on the first board revision), driven by LEDC PWM:
// the pitch is the PWM frequency and the volume is the duty cycle. Sounds are short
// note sequences played from loop() (sfxTick), so nothing blocks.
enum Sfx { SFX_CLICK, SFX_TAP, SFX_ON, SFX_OFF, SFX_APPROVE, SFX_DENY, SFX_ATTENTION, SFX_REMIND, SFX_MENU, SFX_POWER, SFX_CUE, SFX_DONE };
struct Note { uint16_t hz, ms; };
static const Note N_CLICK[] = {{2600, 10}};
static const Note N_TAP[] = {{1900, 22}};
static const Note N_ON[] = {{1500, 25}, {2300, 40}};
static const Note N_OFF[] = {{2300, 25}, {1500, 40}};
static const Note N_APPROVE[] = {{1800, 50}, {2700, 100}};
static const Note N_DENY[] = {{900, 70}, {600, 120}};
static const Note N_ATTENTION[] = {{1319, 90}, {0, 40}, {1760, 90}, {0, 40}, {2093, 150}};   // Claude needs you
static const Note N_REMIND[] = {{1760, 70}, {0, 70}, {1760, 70}};
static const Note N_MENU[] = {{1000, 40}, {1500, 60}};
static const Note N_POWER[] = {{2000, 70}, {1400, 70}, {900, 140}};
static const Note N_CUE[] = {{700, 45}};                                                     // "release now to deny"
static const Note N_DONE[] = {{2349, 60}, {1760, 110}};                                      // a session finished: a short falling "ding-dong"
static const struct { const Note* n; uint8_t len; } SFX[] = {
  {N_CLICK, 1}, {N_TAP, 1}, {N_ON, 2}, {N_OFF, 2}, {N_APPROVE, 2}, {N_DENY, 2},
  {N_ATTENTION, 5}, {N_REMIND, 3}, {N_MENU, 2}, {N_POWER, 3}, {N_CUE, 1}, {N_DONE, 2}};
// Duty cycle (of 255) per volume level: off, then four steps, roughly doubling (loudness is felt
// logarithmically). The top is 48 (19 %), not the 50 % a square wave is loudest at in theory:
// heard on this board, 128 was quieter than 48, so the buzzer peaks lower.
static const uint8_t VOL_DUTY[5] = {0, 4, 10, 24, 48};
static const Note* seq = nullptr; static uint8_t seqLen = 0, seqPos = 0; static uint32_t noteEnd = 0;
static void buzz(uint16_t hz) {
  if (!hz || !cfg.vol) { ledcWrite(PIN_BUZZER, 0); return; }
  ledcChangeFrequency(PIN_BUZZER, hz, 8);
  ledcWrite(PIN_BUZZER, VOL_DUTY[cfg.vol]);
}
static void sfx(Sfx s) {
  if (!cfg.vol) return;
  seq = SFX[s].n; seqLen = SFX[s].len; seqPos = 0;
  buzz(seq[0].hz); noteEnd = millis() + seq[0].ms;
}
static void sfxTick() {
  if (!seq || (int32_t)(millis() - noteEnd) < 0) return;
  if (++seqPos >= seqLen) { seq = nullptr; ledcWrite(PIN_BUZZER, 0); return; }
  buzz(seq[seqPos].hz); noteEnd = millis() + seq[seqPos].ms;
}
static void sfxWait() { while (seq) { sfxTick(); delay(5); } }   // before powering off or restarting

// Nothing important in the panel's rounded corners: how far in (px) a row whose bottom
// edge is at y has to start, so it isn't clipped by the bottom-left / bottom-right corner.
static int cornerInset(int y) {
  if (boxOn) return 0;                               // inside the box, well clear of the corners
  const int R = 32, dy = y - (H - R);
  if (dy <= 0) return 0;
  return R - (int)sqrtf((float)(R * R - min(dy, R - 1) * min(dy, R - 1))) + 1;
}


// ───────────── the view ─────────────
enum State { S_OFFLINE, S_IDLE, S_THINK, S_CODE, S_ATTN };
static const uint16_t STATE_COLOR[5] = {DIM, SKY, YELLOW, GREEN, RED};
static const char* const STATE_LABEL[5] = {"offline", "idle", "thinking", "coding", "need input"};

// Everything that ends up on the screen. The frame is redrawn (and pushed)
// only when this changes; the struct is zeroed first so memcmp is safe.
struct View {
  State state;
  int eyeDx, eyeDy, lid, arm, zzz;
  bool glyph;
  uint16_t lineGen, promptSeq; uint8_t waited; bool sent;
  uint8_t link, menu, armed, locked, toast, flash; int8_t bat; uint8_t charging; uint16_t menuVer; uint32_t passkey;
};

// Clawd on a 40 x 30 grid of 1 px cells, with 8-cell arms and legs: 56 x 38 px.
constexpr int CEL_X = 20, CEL_Y = 16;
static void cell(int cx, int cy, int w, int h, uint16_t c) { spr.fillRect(CEL_X + cx, CEL_Y + cy, w, h, c); }

static void drawEye(int centerX, const View& v) {
  const int w = v.state == S_ATTN ? 6 : 5, h = v.state == S_CODE ? 7 : v.state == S_ATTN ? 14 : 11;
  int x0 = centerX + v.eyeDx - w / 2, y0 = 11 + v.eyeDy - h / 2;
  if (h - v.lid > 0) cell(x0, y0 + v.lid, w, h - v.lid, EYE);
}
static void drawZ(int x, int y, int u) {
  const uint16_t c = 0x8A22;
  x += CEL_X; y += CEL_Y;
  spr.fillRect(x, y, 5 * u, u, c);
  for (int k = 0; k < 3; k++) spr.fillRect(x + (3 - k) * u, y + (k + 1) * u, u, u, c);
  spr.fillRect(x, y + 4 * u, 5 * u, u, c);
}
static void drawClawd(const View& v) {
  cell(0, 0, 40, 30, BODY);
  cell(-8, v.arm, 8, 8, BODY); cell(40, v.arm, 8, 8, BODY);
  const int LEGS[] = {4, 12, 24, 32};
  for (int x : LEGS) cell(x, 30, 4, 8, BODY);
  drawEye(10, v); drawEye(30, v);
  if (v.state == S_ATTN) cell(19, 22, 3, 2, EYE);    // small "o" mouth
  if (v.glyph) {                                     // "?" between the eyes
    cell(19, 2, 3, 1, EYE); cell(18, 3, 1, 1, EYE); cell(22, 3, 1, 2, EYE);
    cell(21, 5, 1, 1, EYE); cell(20, 6, 1, 1, EYE); cell(20, 8, 1, 1, EYE);
  }
  if (v.zzz >= 1) drawZ(14, 18, 1);                  // zzz rising between the eyes
  if (v.zzz >= 2) drawZ(17, 9, 2);
  if (v.zzz >= 3) drawZ(22, 0, 2);
}

// The status light: a rounded box from the pet to the right edge, a coloured
// outline (thicker on the right) around black, the label in the same colour.
static void drawStatus(State st) {
  const uint16_t col = STATE_COLOR[st];
  const int bx = CORNER_W, by = 2, bw = W - bx - 2, bh = CORNER_H - 6;
  const int R = bh / 2, EDGE = 7, EDGE_R = 24;   // R = half the box height: the ends are semicircles, and the inner ones (R - EDGE) are the largest the black area allows
  const int ix = bx + EDGE, iy = by + EDGE, iw = bw - EDGE - EDGE_R, ih = bh - 2 * EDGE;
  spr.fillRoundRect(bx, by, bw, bh, R, col);
  spr.fillRoundRect(ix, iy, iw, ih, R - EDGE, BLACK);
  spr.setTextSize(3);
  spr.setTextColor(col, BLACK);
  spr.setTextDatum(middle_center);
  spr.drawString(STATE_LABEL[st], ix + iw / 2, iy + ih / 2);
  spr.setTextDatum(top_left);
}

// The transcript font is DM Sans (the same as Claudinho's messages).
static const uint8_t* loadedFont = nullptr;
static void useFont(const uint8_t* f) {
  if (f == loadedFont) return;
  if (loadedFont) spr.unloadFont();
  if (f) spr.loadFont(f);
  loadedFont = f;
}
constexpr int ROW_MAX = 96;

// Greedy word-wrap to pixel widths in the current font: the first row may be narrower
// (a leading column sits beside it); later rows start at the left edge. Words wider
// than a row are broken. Returns the number of rows written.
static uint8_t wrapInto(const char* in, char out[][ROW_MAX], uint8_t maxRows, int firstW, int restW) {
  uint8_t row = 0; char cur[ROW_MAX] = ""; int width = firstW;
  auto commit = [&]() { if (row < maxRows) strlcpy(out[row], cur, ROW_MAX); row++; cur[0] = 0; width = restW; };
  const char* p = in;
  while (*p && row < maxRows) {
    while (*p == ' ') p++;
    const char* w = p;
    while (*p && *p != ' ') p++;
    size_t wlen = p - w;
    if (!wlen) break;
    char word[ROW_MAX]; strlcpy(word, w, min(wlen + 1, sizeof word));
    char cand[ROW_MAX]; snprintf(cand, sizeof cand, cur[0] ? "%s %s" : "%s%s", cur, word);
    if (spr.textWidth(cand) <= width) { strlcpy(cur, cand, ROW_MAX); continue; }
    if (cur[0]) commit();
    while (row < maxRows && spr.textWidth(word) > width && strlen(word) > 1) {   // break a long word
      size_t k = strlen(word) - 1;
      while (k > 1) { char t[ROW_MAX]; snprintf(t, sizeof t, "%.*s", (int)k, word); if (spr.textWidth(t) <= width) break; k--; }
      snprintf(cur, sizeof cur, "%.*s", (int)k, word); commit();
      memmove(word, word + k, strlen(word + k) + 1);
    }
    strlcpy(cur, word, ROW_MAX);
  }
  if (cur[0] && row < maxRows) commit();
  return min<uint8_t>(row, maxRows);
}

// Small text (built-in 6x8 font, the smallest there is) for the leading column of a row.
static void drawSmall(const char* t, int x, int y, uint16_t color, int size = 1) {
  useFont(nullptr); spr.setTextSize(size);
  spr.setTextColor(color, textBg); spr.setTextDatum(top_left); spr.drawString(t, x, y);
  spr.setTextSize(1);
  useFont(FONT_MSG);
}

// A row of text starting at x.
static void textAt(int x, int y, const char* s, uint16_t color) {
  spr.setTextColor(color, textBg); spr.setTextDatum(top_left); spr.drawString(s, x, y);
}

// Context bar: a dark track with the used share filled in green, amber from 70 %, red from 90 %.
static void drawBar(int x, int y, int w, int pct) {
  if (pct < 0) return;
  spr.fillRect(x, y, w, 4, 0x2104);
  spr.fillRect(x, y, w * min(pct, 100) / 100, 4, pct >= 90 ? RED : pct >= 70 ? 0xFD20 : GREEN);
}

// The bridge sends "HH:MM text": returns the text and copies the time out ("" if none).
static const char* splitTime(const char* line, char* hhmm) {
  hhmm[0] = 0;
  if (strlen(line) > 6 && isdigit(line[0]) && isdigit(line[1]) && line[2] == ':' && isdigit(line[3]) && isdigit(line[4]) && line[5] == ' ') {
    memcpy(hhmm, line, 5); hhmm[5] = 0; return line + 6;
  }
  return line;
}

// Transcript under the pet strip: the newest entry bright and the older ones dim.
// Each entry leads with a small column (session name when several are open, time,
// context bar); its first row sits beside the column, later rows start at the
// left edge. The bottom row is kept clear of the rounded corners.
static void drawTranscript() {
  const int SHOW = msgRows, top = msgTop;
  if (!cfg.hud) return;
  if (data.nLines == 0) {
    static char mrow[8][ROW_MAX];
    uint8_t n = wrapInto(linkUp() ? data.msg : "No Claude connected", mrow, 8, TEXT_W, TEXT_W);
    for (uint8_t i = 0; i < n && i < SHOW; i++) textAt(padX, top + i * LH, mrow[i], TEXT);
    return;
  }
  // The column is as wide as the longest name (or the time).
  int col = 0;
  for (uint8_t i = 0; i < data.nLines; i++) col = max(col, (int)strlen(data.sess[i]) * 6);
  col = max(col, 5 * 6) + 5;
  static char disp[32][ROW_MAX], hhmm[8][6]; static uint8_t srcOf[32]; static bool first[32];
  uint8_t nDisp = 0;
  for (uint8_t i = 0; i < data.nLines && nDisp < 32; i++) {
    uint8_t got = wrapInto(splitTime(data.lines[i], hhmm[i]), &disp[nDisp], 32 - nDisp, TEXT_W - col, TEXT_W);
    for (uint8_t j = 0; j < got; j++) { srcOf[nDisp + j] = i; first[nDisp + j] = j == 0; }
    nDisp += got;
  }
  // With a full screen the last row sits in the bottom corners: re-wrap it narrower.
  if (nDisp >= SHOW) {
    const int ins = cornerInset(top + (SHOW - 1) * LH + 22), last = nDisp - 1;
    const int lim = (first[last] ? TEXT_W - col : TEXT_W) - 2 * ins;
    if (spr.textWidth(disp[last]) > lim) {
      static char tmp[3][ROW_MAX]; char src[ROW_MAX]; strlcpy(src, disp[last], ROW_MAX);
      uint8_t m = wrapInto(src, tmp, 3, lim, TEXT_W - 2 * ins);
      uint8_t keepSrc = srcOf[last]; bool wasFirst = first[last];
      for (uint8_t k = 0; k < m && last + k < 32; k++) {
        strlcpy(disp[last + k], tmp[k], ROW_MAX); srcOf[last + k] = keepSrc; first[last + k] = wasFirst && k == 0;
      }
      nDisp = min<int>(32, last + m);
    }
  }
  int start = nDisp > SHOW ? nDisp - SHOW : 0;       // the newest rows always fit
  for (int i = 0; start + i < nDisp; i++) {
    uint8_t src = srcOf[start + i]; int r = start + i, y = top + i * LH;
    bool newest = src == data.nLines - 1;
    const int ins = i == SHOW - 1 ? cornerInset(y + 22) : 0, x0 = padX + ins;
    textAt(x0 + (first[r] ? col : 0), y, disp[r], newest ? TEXT : DIM);
    if (!first[r]) continue;
    // Leading column in the 28 px row: name (when several sessions), time, context bar.
    const bool named = data.sess[src][0];
    int ty = y + (named ? 10 : 6);
    if (named) drawSmall(data.sess[src], x0, y + 1, SESSION_COLOR[data.sc[src] % 6]);
    drawSmall(hhmm[src], x0, ty, newest ? TEXT : DIM);
    drawBar(x0, ty + 10, col - 5, data.cx[src]);
  }
}

// Battery icon, 17 x 9 px: an outline with a nub, filled to the charge level. Grey when fine,
// orange under 30 %, red under 15 %. A lightning bolt (7 px) in the same colour follows it while charging.
static void drawBattery(int x, int y, int pct, bool charging) {
  const uint16_t c = pct < 15 ? RED : pct < 30 ? 0xFD20 : DIM;
  spr.drawRect(x, y, 15, 9, c);
  spr.fillRect(x + 15, y + 3, 2, 3, c);
  const int fill = (11 * pct + 50) / 100;
  if (fill > 0) spr.fillRect(x + 2, y + 2, max(fill, 1), 5, c);
  if (charging) {                                    // bolt
    const int bx = x + 19;
    spr.drawLine(bx + 4, y - 1, bx + 1, y + 4, c); spr.drawLine(bx + 5, y - 1, bx + 2, y + 4, c);
    spr.drawLine(bx + 1, y + 4, bx + 5, y + 4, c);
    spr.drawLine(bx + 5, y + 4, bx + 2, y + 9, c); spr.drawLine(bx + 4, y + 4, bx + 1, y + 9, c);
  }
}

static void drawApproval(const View& v) {
  const int SHOW = msgRows, top = msgTop;
  char t[48]; snprintf(t, sizeof t, "approve? %us", v.waited);
  textAt(padX, top, t, v.waited >= 10 ? HOT : DIM);
  if (data.prompt.sess[0])                           // which session is asking, at the right
    drawSmall(data.prompt.sess, W - padX - (int)strlen(data.prompt.sess) * 6, top + 9, SESSION_COLOR[data.prompt.sc % 6]);
  textAt(padX, top + LH, data.prompt.tool, TEXT);
  static char rows[4][ROW_MAX];
  uint8_t n = wrapInto(data.prompt.hint, rows, SHOW - 3, TEXT_W, TEXT_W);   // what's left between the tool and the footer
  for (uint8_t i = 0; i < n; i++) textAt(padX, top + (2 + i) * LH, rows[i], DIM);
  const int fy = top + (SHOW - 1) * LH, ins = cornerInset(fy + 22);
  if (v.sent) textAt(padX + ins, fy, "sent...", DIM);
  else {
    textAt(padX + ins, fy, "tap: approve", GREEN);
    spr.setTextColor(HOT, textBg); spr.setTextDatum(top_right); spr.drawString("hold: deny", W - padX - ins, fy);
  }
}

// ───────────── menu (hold the screen; tap a row) ─────────────
enum Page : uint8_t { PG_NONE, PG_MAIN, PG_SETTINGS, PG_TIMERS, PG_RESET, PG_ABOUT, PG_CONFIRM };
static Page page = PG_NONE, confirmBack = PG_NONE;
static uint16_t menuVer = 0;                       // bumps when anything on a menu page changes
static uint32_t armedUntil = 0;                    // factory reset: first tap arms it, a second within 3 s does it
static uint32_t confirmAt = 0;                     // when the shutdown question appeared
// A thin title bar (back pill top left, title in the middle, power pill top right on the main
// page) over a grid of six pill buttons, 2 columns x 3 rows, that the rows of text used to be.
constexpr int BAR_H = 40;
constexpr int BTN_X0 = 10, BTN_GAP = 8, BTN_W = (W - 2 * BTN_X0 - BTN_GAP) / 2, BTN_H = 58, BTN_Y0 = 44, BTN_PITCH = 64;
constexpr int BACK_X = 10, BACK_Y = 6, BACK_W = 52, BACK_H = 28;
constexpr int PILL_W = 52, PILL_H = 28, PILL_X = W - 10 - PILL_W, PILL_Y = 6;   // power button, top right
constexpr int MENU_X = 20, MENU_TOP = 44;          // the about page's text

static const char* const MAIN_ITEMS[] = {"brightness", "volume", "settings", "help"};
static const char* const SETTINGS_ITEMS[] = {"bluetooth", "transcript", "rotate", "timers", "reset"};
static const uint8_t OFF_CHOICES[] = {0, 5, 10, 30, 60};     // minutes with no bridge, on battery, before turning off (0 = never)
static const uint8_t DIM_CHOICES[] = {0, 10, 30, 60};        // minutes idle, on USB, before dimming (0 = never)
static int pageRows() { return page == PG_MAIN ? 4 : page == PG_SETTINGS ? 5 : page == PG_TIMERS ? 2 : page == PG_RESET ? 1 : page == PG_CONFIRM ? 2 : 0; }

// Which button a touch is on (0..5, row by row), or -1.
static int buttonAt(int tx, int ty) {
  for (int i = 0; i < 6; i++) {
    const int x = BTN_X0 + (i % 2) * (BTN_W + BTN_GAP), y = BTN_Y0 + (i / 2) * BTN_PITCH;
    if (tx >= x - 3 && tx < x + BTN_W + 3 && ty >= y - 3 && ty < y + BTN_H + 3) return i;
  }
  return -1;
}
static void menuChanged() { menuVer++; }
static void nextChoice(uint8_t& v, const uint8_t* choices, int n) {
  int i = 0; while (i < n && choices[i] != v) i++;
  v = choices[(i + 1) % n];
}
static void minutesText(uint8_t m, char* out, size_t n) { if (m) snprintf(out, n, "%um", m); else strlcpy(out, "off", n); }

static void applyBrightness() { lcd.setBrightness((20 + cfg.bright * 20) * 255 / 100); }
static void applyRotation() { lcd.setRotation(cfg.flip ? 3 : 1); }

// "On USB" means a serial connection with a computer is up: the bridge's lines are arriving, or
// a host has the port open (a serial monitor). A bare cable or a charger doesn't count.
static bool onUsb() { return usbLive() || (bool)Serial; }
static bool pwrDown() { return digitalRead(PIN_PWR) == LOW; }

// Off. On battery that cuts the power (the power key turns it on again, in hardware).
// On USB the board stays powered, so it sleeps with the screen and radio off and
// wakes, restarting, when the power key is held for a moment.
static void powerOff() {
  sfx(SFX_POWER); sfxWait();
  bleSetEnabled(false);
  lcd.setBrightness(0); lcd.sleep();
  digitalWrite(PIN_LATCH, LOW);                      // cuts power when running from the battery
  delay(300);
  gpio_wakeup_enable((gpio_num_t)PIN_PWR, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  for (;;) {
    esp_light_sleep_start();
    uint32_t t0 = millis();
    while (pwrDown() && millis() - t0 < PWR_HOLD_MS) delay(20);
    if (pwrDown()) { ESP.restart(); }
    delay(50);                                       // a short press: back to sleep
  }
}

static void askPowerOff(Page back) {
  confirmBack = back; page = PG_CONFIRM; confirmAt = millis();
  sfx(SFX_REMIND); menuChanged();
}

// The back button (top left): one level up; from the main page it closes the menu.
static void menuBack() {
  sfx(page == PG_MAIN ? SFX_MENU : SFX_TAP);
  armedUntil = 0;
  switch (page) {
    case PG_MAIN: page = PG_NONE; break;
    case PG_SETTINGS: case PG_ABOUT: page = PG_MAIN; break;
    case PG_TIMERS: case PG_RESET: page = PG_SETTINGS; break;
    case PG_CONFIRM: page = confirmBack; break;
    default: break;
  }
  menuChanged();
}

static void menuTap(int tx, int ty) {
  if (ty < BAR_H) {
    if (tx < BACK_X + BACK_W + 10) menuBack();
    else if (page == PG_MAIN && tx >= PILL_X - 8) askPowerOff(PG_MAIN);
    return;
  }
  if (page == PG_ABOUT) { menuBack(); return; }
  const int row = buttonAt(tx, ty);
  if (row < 0 || row >= pageRows()) return;
  sfx(SFX_TAP);
  if (page == PG_MAIN) {
    switch (row) {
      case 0: cfg.bright = (cfg.bright + 1) % 5; applyBrightness(); cfgSave(); break;
      case 1: cfg.vol = (cfg.vol + 1) % 5; cfgSave(); sfx(SFX_TAP); break;           // a sample at the new volume
      case 2: page = PG_SETTINGS; break;
      case 3: page = PG_ABOUT; break;
    }
  } else if (page == PG_SETTINGS) {
    switch (row) {
      case 0: cfg.bt = !cfg.bt; bleSetEnabled(cfg.bt); sfx(cfg.bt ? SFX_ON : SFX_OFF); break;   // off = not advertising and nobody connected
      case 1: cfg.hud = !cfg.hud; sfx(cfg.hud ? SFX_ON : SFX_OFF); break;
      case 2: cfg.flip = !cfg.flip; applyRotation(); sfx(SFX_ON); break;   // flips at once; the menu redraws the right way up
      case 3: page = PG_TIMERS; break;
      case 4: page = PG_RESET; armedUntil = 0; break;
    }
    cfgSave();
  } else if (page == PG_TIMERS) {
    if (row == 0) nextChoice(cfg.offMin, OFF_CHOICES, 5);
    else nextChoice(cfg.dimMin, DIM_CHOICES, 4);
    cfgSave();
  } else if (page == PG_CONFIRM) {
    if (row == 0) { powerOff(); return; }
    page = confirmBack;                               // cancel
  } else if (page == PG_RESET) {
    if (armedUntil && (int32_t)(millis() - armedUntil) < 0) {
      sfx(SFX_POWER); sfxWait();
      prefs.begin("buddy", false); prefs.clear(); prefs.end();   // settings and names
      bleClearBonds();                                           // the next desktop has to pair again
      delay(300); ESP.restart();
    } else { armedUntil = millis() + 3000; sfx(SFX_REMIND); }
  }
  menuChanged();
}

// One pill button in the grid: the label (DM Sans) with, optionally, its current value under it.
static void drawButton(int i, const char* label, const char* value = nullptr, uint16_t valueColor = DIM, uint16_t edge = 0x4208) {
  const int x = BTN_X0 + (i % 2) * (BTN_W + BTN_GAP), y = BTN_Y0 + (i / 2) * BTN_PITCH;
  spr.fillRoundRect(x, y, BTN_W, BTN_H, BTN_H / 2, edge);
  spr.fillRoundRect(x + 2, y + 2, BTN_W - 4, BTN_H - 4, BTN_H / 2 - 2, 0x1082);
  spr.setTextDatum(middle_center); spr.setTextColor(TEXT, 0x1082);
  spr.drawString(label, x + BTN_W / 2, y + (value ? 20 : BTN_H / 2));
  if (value) { useFont(nullptr); spr.setTextSize(2); spr.setTextColor(valueColor, 0x1082); spr.drawString(value, x + BTN_W / 2, y + 42); spr.setTextSize(1); useFont(FONT_MSG); }
}

static void drawTitle(const char* t, uint16_t color) {
  spr.setTextDatum(middle_center); spr.setTextColor(color, BLACK); spr.drawString(t, W / 2, BAR_H / 2);
}

// The back button: a pill with a chevron, top left (a pill's end follows the panel's rounded corner).
static void drawBackPill() {
  spr.fillRoundRect(BACK_X, BACK_Y, BACK_W, BACK_H, BACK_H / 2, 0x4208);
  spr.fillRoundRect(BACK_X + 2, BACK_Y + 2, BACK_W - 4, BACK_H - 4, BACK_H / 2 - 2, 0x1082);
  const int cx = BACK_X + BACK_W / 2, cy = BACK_Y + BACK_H / 2;
  for (int d = 0; d < 2; d++) { spr.drawLine(cx + 5 + d, cy - 8, cx - 3 + d, cy, TEXT); spr.drawLine(cx - 3 + d, cy, cx + 5 + d, cy + 8, TEXT); }
}

// A pill-shaped power button in the top-right corner.
static void drawPowerPill() {
  spr.fillRoundRect(PILL_X, PILL_Y, PILL_W, PILL_H, PILL_H / 2, HOT);
  spr.fillRoundRect(PILL_X + 2, PILL_Y + 2, PILL_W - 4, PILL_H - 4, PILL_H / 2 - 2, BLACK);
  const int cx = PILL_X + PILL_W / 2, cy = PILL_Y + PILL_H / 2;
  spr.fillCircle(cx, cy + 1, 7, HOT); spr.fillCircle(cx, cy + 1, 5, BLACK);   // the power symbol: a ring ...
  spr.fillRect(cx - 4, cy - 8, 9, 7, BLACK);                                  // ... open at the top ...
  spr.fillRect(cx - 1, cy - 8, 3, 9, HOT);                                    // ... with a stroke through the gap
}

static void drawMenu() {
  char val[16];
  drawBackPill();
  switch (page) {
    case PG_MAIN: {
      drawTitle("menu", YELLOW); drawPowerPill();
      static const char* const VOL[] = {"off", "1/4", "2/4", "3/4", "4/4"};
      snprintf(val, sizeof val, "%u/4", cfg.bright); drawButton(0, MAIN_ITEMS[0], val);
      drawButton(1, MAIN_ITEMS[1], VOL[cfg.vol], cfg.vol ? GREEN : DIM);
      drawButton(2, MAIN_ITEMS[2]);
      drawButton(3, MAIN_ITEMS[3]);
    } break;
    case PG_SETTINGS:
      drawTitle("settings", YELLOW);
      drawButton(0, SETTINGS_ITEMS[0], cfg.bt ? "on" : "off", cfg.bt ? GREEN : DIM);
      drawButton(1, SETTINGS_ITEMS[1], cfg.hud ? "on" : "off", cfg.hud ? GREEN : DIM);
      drawButton(2, SETTINGS_ITEMS[2], cfg.flip ? "180 deg" : "0 deg");
      drawButton(3, SETTINGS_ITEMS[3]);
      drawButton(4, SETTINGS_ITEMS[4]);
      break;
    case PG_TIMERS: {
      drawTitle("timers", YELLOW);
      minutesText(cfg.offMin, val, sizeof val); drawButton(0, "auto off", val, cfg.offMin ? GREEN : DIM);
      minutesText(cfg.dimMin, val, sizeof val); drawButton(1, "dim after", val, cfg.dimMin ? GREEN : DIM);
      static const char* const N[] = {"auto off: turn off after this long on", "battery with no bridge connected", "dim after: dim the screen after this", "long idle, while on USB"};
      for (int i = 0; i < 4; i++) drawSmall(N[i], MENU_X, BTN_Y0 + BTN_PITCH + 14 + i * 14, i % 2 ? DIM : TEXT);
    } break;
    case PG_CONFIRM:
      drawTitle("power off?", HOT);
      drawButton(0, "power off", nullptr, DIM, HOT);
      drawButton(1, "cancel");
      break;
    case PG_RESET: {
      drawTitle("reset", HOT);
      bool armed = armedUntil && (int32_t)(millis() - armedUntil) < 0;
      drawButton(0, armed ? "tap again" : "reset all", armed ? "really?" : nullptr, HOT, HOT);
    } break;
    case PG_ABOUT: {
      drawTitle("help / about", YELLOW);
      static const char* const L[] = {"tap: allow a request", "hold: deny it", "hold (idle): menu", "power key x2: lock / unlock", "power key hold: off", "Claude buddy - USB + BT"};
      for (int i = 0; i < 6; i++) textAt(i < 5 ? MENU_X : MENU_X + 4, MENU_TOP + i * 26, L[i], i < 5 ? TEXT : DIM);
    } break;
    default: break;
  }
}

static void drawPasskey(uint32_t pk) {
  char t[12]; snprintf(t, sizeof t, "%06lu", (unsigned long)pk);
  spr.setTextDatum(middle_center);
  spr.setTextColor(YELLOW, BLACK); spr.drawString("Bluetooth pairing", W / 2, 60);
  spr.setTextColor(DIM, BLACK);    spr.drawString("type this on the computer", W / 2, 90);
  useFont(nullptr); spr.setTextSize(5); spr.setTextColor(TEXT, BLACK); spr.drawString(t, W / 2, 145);
  spr.setTextSize(1); useFont(FONT_MSG);
}

static void draw(const View& v) {
  spr.fillScreen(BLACK);
  textBg = BLACK;
  useFont(nullptr);
  spr.setTextSize(1);
  useFont(FONT_MSG);
  if (v.passkey) drawPasskey(v.passkey);
  else if (page != PG_NONE) drawMenu();
  else {
    useFont(nullptr);                                // built-in font for the status label
    drawClawd(v);
    drawStatus(v.state);
    // Under Clawd, away from the corners: battery (with a bolt while charging), which link is
    // live, and a padlock when the screen is locked, centred together as one row.
    const char* l = v.link == SRC_USB ? "USB" : v.link == SRC_BLE ? "BT" : "";
    const int batW = v.bat >= 0 ? 17 + (v.charging ? 7 : 0) : 0, tagW = (int)strlen(l) * 6, lockW = v.locked ? 9 : 0;
    const int parts = (batW > 0) + (tagW > 0) + (lockW > 0), GAP = 5;
    const int total = batW + tagW + lockW + GAP * max(parts - 1, 0);
    int x = CEL_X + 20 - total / 2; const int y0 = CEL_Y + 42;
    if (batW) { drawBattery(x, y0, v.bat, v.charging); x += batW + GAP; }
    if (tagW) { drawSmall(l, x, y0, DIM); x += tagW + GAP; }
    if (lockW) {                                     // a padlock: shackle over a body with a keyhole, 9 x 9 px
      spr.drawRoundRect(x + 2, y0, 5, 6, 2, YELLOW);
      spr.fillRect(x, y0 + 4, 9, 6, YELLOW);
      spr.fillRect(x + 4, y0 + 6, 1, 3, BLACK);
    }
    spr.setTextSize(1);
    useFont(FONT_MSG);
    layoutMessages(v.flash);
    if (v.state == S_IDLE) { spr.fillRect(0, CORNER_H, W, H - CORNER_H, NAVY); textBg = NAVY; }   // idle: dark blue message area
    if (v.flash) {                                   // action needed: a box around the message block, flashing red and green
      const uint16_t c = v.flash == 1 ? RED : GREEN;
      spr.fillRoundRect(BOX_X, BOX_Y, BOX_W, BOX_H, BOX_R, c);
      spr.fillRoundRect(BOX_X + BOX_BORDER, BOX_Y + BOX_BORDER, BOX_W - 2 * BOX_BORDER, BOX_H - 2 * BOX_BORDER, BOX_R - BOX_BORDER, BLACK);
    }
    if (data.prompt.id[0]) drawApproval(v); else drawTranscript();
    if (v.toast) {                                   // a note over the middle of the screen
      spr.fillRoundRect(15, 96, 250, 74, 16, 0x2104); spr.drawRoundRect(15, 96, 250, 74, 16, v.toast == 1 ? YELLOW : GREEN);
      spr.setTextDatum(middle_center); spr.setTextColor(TEXT, 0x2104);
      spr.drawString(v.toast == 1 ? "Screen locked" : "Screen unlocked", W / 2, 124);
      if (v.toast == 1) { useFont(nullptr); spr.setTextSize(1); spr.setTextColor(DIM, 0x2104); spr.setTextDatum(middle_center);
                          spr.drawString("double-press the power button to unlock", W / 2, 150); useFont(FONT_MSG); }
    }
  }
  spr.pushSprite(0, 0);
}

// ───────────── logic ─────────────
static char answeredId[40] = "";

static void answer(bool allow) {
  char cmd[160];
  snprintf(cmd, sizeof cmd, "{\"cmd\":\"permission\",\"id\":\"%s\",\"decision\":\"%s\"}", data.prompt.id, allow ? "once" : "deny");
  sendLine(data.prompt.src, cmd);
  strlcpy(answeredId, data.prompt.id, sizeof answeredId);
  if (allow) approvals++; else denials++;
  sfx(allow ? SFX_APPROVE : SFX_DENY);
}

static bool locked = false, dimmed = false;
static uint32_t toastUntil = 0, toastIsUnlock = 0, lastTouchMs = 0;

// On a prompt: a tap approves, a long press denies (a low tick says "release now to deny").
// Only a touch that began after the prompt had been on screen for a moment counts, so a
// finger already resting on the glass (or a stray brush) can't approve something unread.
// A locked screen still takes this. Otherwise: holding the screen opens the menu and a
// tap picks a menu row; a touch on a locked screen only says so. The touch that wakes
// a dimmed screen does nothing else.
static void handleTouch(uint32_t promptShownMs) {
  static bool was = false, held = false, cued = false, eligible = false, swallow = false; static uint32_t downAt = 0; static int lastX = 0, lastY = 0;
  int tx, ty; bool now = touchRead(tx, ty);
  const bool prompting = data.prompt.id[0] && strcmp(data.prompt.id, answeredId) != 0 && page == PG_NONE;
  if (now) lastTouchMs = millis();
  if (now && !was) {
    downAt = millis(); held = cued = false; eligible = promptShownMs && downAt - promptShownMs >= PROMPT_SETTLE_MS;
    swallow = dimmed;                                // waking the screen isn't a command
    if (prompting && eligible && !swallow) sfx(SFX_CLICK);       // the touch registered
    if (locked && !prompting && page == PG_NONE && !swallow) { toastUntil = millis() + 2200; toastIsUnlock = 0; }   // the note only: no sound
  }
  if (now) { lastX = tx; lastY = ty; }
  if (swallow) { was = now; return; }
  if (now && prompting && eligible && !cued && millis() - downAt >= DENY_HOLD_MS) { cued = true; sfx(SFX_CUE); }
  if (now && page == PG_NONE && !locked && !data.prompt.id[0] && !held && millis() - downAt >= MENU_HOLD_MS) {
    held = true; page = PG_MAIN; sfx(SFX_MENU); menuChanged();   // swallow the release that follows
  }
  if (!now && was && !held) {
    if (page != PG_NONE) menuTap(lastX, lastY);
    else if (prompting && eligible) answer(millis() - downAt < DENY_HOLD_MS);
  }
  was = now;
}

// The power key: hold to ask to turn off, double-press to lock or unlock the screen.
// Any press also wakes a dimmed screen.
static void handlePower() {
  static bool was = false, fired = false; static uint32_t downAt = 0, lastClick = 0;
  bool now = pwrDown();
  if (now) lastTouchMs = millis();
  if (now && !was) { downAt = millis(); fired = false; }
  if (now && !fired && millis() - downAt >= PWR_HOLD_MS && page != PG_CONFIRM) { fired = true; lastClick = 0; askPowerOff(PG_NONE); }
  if (!now && was && !fired) {
    if (lastClick && millis() - lastClick < PWR_DOUBLE_MS) {
      lastClick = 0;
      locked = !locked; toastUntil = millis() + 1500; toastIsUnlock = !locked; sfx(locked ? SFX_OFF : SFX_ON);
      if (locked && page != PG_NONE) { page = PG_NONE; menuChanged(); }   // a menu left open would go dead
    } else lastClick = millis();
  }
  was = now;
}

static State deriveState() {
  if (!linkUp()) return S_OFFLINE;
  if (data.prompt.id[0] || data.waiting > 0) return S_ATTN;
  if (data.running > 0) return data.lastLineMs && millis() - data.lastLineMs < 8000 ? S_CODE : S_THINK;   // a new tool line = coding
  return S_IDLE;
}

static void buildView(View& v, State st, uint16_t promptSeq, uint32_t promptShownMs) {
  memset(&v, 0, sizeof v);
  v.state = st; v.arm = 13; v.lineGen = data.lineGen; v.promptSeq = promptSeq;
  v.link = activeLink(); v.menu = page; v.menuVer = menuVer; v.passkey = blePasskey();
  v.armed = armedUntil && (int32_t)(millis() - armedUntil) < 0;
  v.flash = st == S_ATTN ? 1 + (millis() / 400) % 2 : 0;   // red / green, 0.4 s each
  v.bat = bat.pct; v.charging = bat.charging;
  v.locked = locked; v.toast = (int32_t)(millis() - toastUntil) < 0 ? (toastIsUnlock ? 2 : 1) : 0;
  uint32_t now = millis();
  uint32_t phase = now / 250;                        // animation step
  switch (st) {
    case S_OFFLINE:
      v.lid = 10; v.eyeDy = 2; v.zzz = (phase / 4) % 4;
      break;
    case S_IDLE: {                                   // blink every few seconds, glance sideways now and then
      static uint32_t nextBlink = 0, blinkAt = 0, nextLook = 0, lookUntil = 0; static int lookDx = 0;
      if (!nextBlink) { nextBlink = now + 2500; nextLook = now + 4000; }
      if (!blinkAt && now >= nextBlink) blinkAt = now;
      if (blinkAt) {
        static const int LID[] = {4, 10, 4, 0};
        uint32_t f = (now - blinkAt) / 60;
        if (f < 4) v.lid = LID[f]; else { blinkAt = 0; nextBlink = now + 2500 + random(4000); }
      }
      if (!lookUntil && now >= nextLook) { lookUntil = now + 700; lookDx = random(2) ? 2 : -2; }
      if (lookUntil) { if (now < lookUntil) v.eyeDx = lookDx; else { lookUntil = 0; nextLook = now + 4000 + random(6000); } }
    } break;
    case S_THINK: v.eyeDx = 1; v.eyeDy = -1; v.lid = (phase / 2) % 4 == 3 ? 4 : 0; break;
    case S_CODE:  v.eyeDy = 2; v.eyeDx = (phase & 1) ? 3 : -3; break;     // narrow eyes sweeping side to side
    case S_ATTN: {
      static const int ARM[4] = {5, 12, 19, 12};
      v.glyph = (phase / 2) % 2 == 0; v.arm = ARM[phase % 4];
      if (promptShownMs) v.waited = min<uint32_t>((now - promptShownMs) / 1000, 255);
      v.sent = data.prompt.id[0] && !strcmp(data.prompt.id, answeredId);
    } break;
  }
}

void setup() {
  pinMode(PIN_LATCH, OUTPUT); digitalWrite(PIN_LATCH, HIGH);   // stay on when running from the battery
  pinMode(PIN_PWR, INPUT_PULLUP);
  analogSetPinAttenuation(1, ADC_11db);                        // battery, through a 1:3 divider
  // The bridge sends a whole heartbeat at once; the default buffer would drop the overflow.
  Serial.setRxBufferSize(2048);
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);                          // nobody reading: don't block on every print
  cfgLoad();
  lcd.init(); applyRotation(); applyBrightness(); lcd.fillScreen(BLACK);
  ledcAttach(PIN_BUZZER, 2000, 8); ledcWrite(PIN_BUZZER, 0);   // buzzer: LEDC PWM, 8 bits
  touchBegin();
  // The frame first, then Bluetooth: it takes a share of internal RAM, and a full
  // 280x240 frame is 134 KB at 16 bits (no PSRAM here).
  spr.setPsram(false);
  spr.setColorDepth(16);
  if (!spr.createSprite(W, H)) { spr.setColorDepth(8); spr.createSprite(W, H); }   // no room for 16 bits: approximate colours
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_BT);
  snprintf(btName, sizeof btName, "Claude-%02X%02X", mac[4], mac[5]);   // distinguishes buddies in the desktop picker
  bleInit(btName);
  bleSetEnabled(cfg.bt);
}

void loop() {
  pollLinks();
  batteryTick();
  sfxTick();
  handlePower();
  if (page == PG_CONFIRM && millis() - confirmAt > CONFIRM_MS) { page = confirmBack; menuChanged(); }   // nobody answered: carry on
  State st = deriveState();

  // A new prompt: chirp, and start the settle timer.
  static char shownId[40] = ""; static uint32_t promptShownMs = 0; static uint16_t promptSeq = 0;
  if (strcmp(shownId, data.prompt.id) != 0) {
    strlcpy(shownId, data.prompt.id, sizeof shownId);
    promptSeq++;
    promptShownMs = data.prompt.id[0] ? millis() : 0;
    if (promptShownMs) { sfx(SFX_ATTENTION); if (page != PG_NONE) { page = PG_NONE; menuChanged(); } }   // a prompt closes the menu
  }
  handleTouch(promptShownMs);

  // Needs you without a prompt to answer (a question, a plan): chime once on arrival.
  // And while anything waits, remind every 30 s so it isn't missed.
  static State prevSt = S_OFFLINE; static uint32_t lastChime = 0;
  if (st == S_ATTN && prevSt != S_ATTN && !data.prompt.id[0]) { sfx(SFX_ATTENTION); lastChime = millis(); }
  if (data.prompt.id[0] && strcmp(data.prompt.id, answeredId) != 0 && page == PG_NONE && promptShownMs && millis() - max(lastChime, promptShownMs) > 30000) { sfx(SFX_REMIND); lastChime = millis(); }
  // Claude finished (working or asking -> idle): a short tone, so you know it's your turn.
  // Coming online (offline -> idle) isn't a session finishing, so it stays quiet.
  if (st == S_IDLE && (prevSt == S_THINK || prevSt == S_CODE || prevSt == S_ATTN)) sfx(SFX_DONE);
  prevSt = st;

  static View last; static bool first = true;
  View v; buildView(v, st, promptSeq, promptShownMs);
  if (first || memcmp(&v, &last, sizeof v) != 0) { draw(v); last = v; first = false; }

  // Timers. Activity (a touch, a key, Claude working or asking) restarts the idle one.
  static uint32_t activeAt = 0, offlineSince = 0;
  if (st == S_THINK || st == S_CODE || st == S_ATTN) activeAt = millis();
  else if (lastTouchMs > activeAt) activeAt = lastTouchMs;
  if (st != S_OFFLINE) offlineSince = 0; else if (!offlineSince) offlineSince = millis();
  // On battery with no bridge for a while: turn off.
  if (!onUsb() && cfg.offMin && offlineSince && millis() - offlineSince > (uint32_t)cfg.offMin * 60000UL) powerOff();
  // On USB with nothing going on for a while: dim.
  dimmed = onUsb() && cfg.dimMin && millis() - activeAt > (uint32_t)cfg.dimMin * 60000UL && st != S_ATTN;
  static int bright = -1;
  int want = dimmed ? 5 : 20 + cfg.bright * 20;
  if (want != bright) { bright = want; lcd.setBrightness(want * 255 / 100); }

  delay(10);
}
