#pragma once
// Waveshare ESP32-C6-LCD-1.47: ESP32-C6FH4 (4 MB flash, no PSRAM), 172x320
// ST7789 panel, addressable RGB LED, TF slot. No touch, IMU, RTC, battery
// gauge or buzzer, and only BOOT and RESET keys. Presents the subset of the
// M5StickCPlus API the firmware uses, with stand-ins for what's missing.
//
//   LCD SCK  7    LCD CS   14   LCD BL  22
//   LCD MOSI 6    LCD DC   15   LCD RST 21
//   RGB LED  8    BOOT (key) 9
//
// One key has to stand in for both M5 buttons (see WsKeys): a tap is
// BtnA (approve / next), a double-tap is BtnB (deny / select / scroll) and a
// hold is BtnA held (menu). Wire an optional real BtnB to a free GPIO with
// -DBOARD_WS147_BTN_B=<gpio> (pulled up, switch to ground); it takes over the
// BtnB role, and a double-tap still works too.
//
// Landscape by default (320x172), rotation 3 (180 degrees from rotation 1).
// Wide enough for the size-2 message text.

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <time.h>
#include <esp_mac.h>

#define BOARD_LED_PIN 8
#define BOARD_LED_WS2812
#define BOARD_UI_W 320
#define BOARD_UI_H 172
#define BOARD_HOME_ROTATION 3
#define BOARD_BUDDY_SCALE 1
#define BOARD_PET_CORNER 1     // pet in the top-left corner, status light beside it
#define BOARD_CORNER_W 100
#define BOARD_CORNER_H 66
#define BOARD_CLOCK_BUDDY_SCALE 1
#define BOARD_MSG_BUDDY_SCALE 1

#define RED   0xF800
#define GREEN 0x07E0

namespace ws147 {
constexpr int PIN_SCK = 7, PIN_MOSI = 6, PIN_CS = 14, PIN_DC = 15, PIN_RST = 21, PIN_BL = 22;
constexpr int PIN_BOOT = 9;
#if defined(BOARD_WS147_BTN_B)
constexpr int PIN_BTN_B = BOARD_WS147_BTN_B;
#else
constexpr int PIN_BTN_B = -1;
#endif
constexpr int PANEL_W = 172, PANEL_H = 320;
}

using TFT_eSPI    = lgfx::LovyanGFX;
using TFT_eSprite = lgfx::LGFX_Sprite;

class WsLcd : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel;
  lgfx::Bus_SPI      _bus;
  lgfx::Light_PWM    _light;
 public:
  WsLcd() {
    auto b = _bus.config();
    b.spi_host   = SPI2_HOST;
    b.spi_mode   = 0;
    b.freq_write = 40000000;
    b.pin_sclk   = ws147::PIN_SCK;
    b.pin_mosi   = ws147::PIN_MOSI;
    b.pin_miso   = -1;
    b.pin_dc     = ws147::PIN_DC;
    _bus.config(b);
    _panel.setBus(&_bus);

    auto c = _panel.config();
    c.pin_cs   = ws147::PIN_CS;
    c.pin_rst  = ws147::PIN_RST;
    c.pin_busy = -1;
    c.memory_width  = 240;
    c.memory_height = 320;
    c.panel_width   = ws147::PANEL_W;
    c.panel_height  = ws147::PANEL_H;
    c.offset_x = (240 - ws147::PANEL_W) / 2;   // 172 columns sit in the middle of the 240-wide RAM
    c.offset_y = 0;
    c.invert   = true;
    c.readable = false;
    c.bus_shared = false;
    _panel.config(c);

    auto l = _light.config();
    l.pin_bl = ws147::PIN_BL;
    l.freq   = 5000;
    l.pwm_channel = 0;
    _light.config(l);
    _panel.setLight(&_light);
    setPanel(&_panel);
  }
  void begin() {
    init();
    setRotation(BOARD_HOME_ROTATION);
    fillScreen(TFT_BLACK);
  }
};

// Polled button with the M5 Button API subset the firmware uses.
class WsButton {
 protected:
  bool     _now = false, _prev = false;
  uint32_t _since = 0;
 public:
  void drive(bool down, uint32_t since = 0) {
    _prev = _now;
    _now = down;
    if (_now && !_prev) _since = since ? since : millis();
  }
  bool isPressed()   const { return _now; }
  bool wasPressed()  const { return _now && !_prev; }
  bool wasReleased() const { return !_now && _prev; }
  bool pressedFor(uint32_t ms) const { return _now && millis() - _since >= ms; }
};

// Turns the single BOOT key (plus an optional real BtnB) into the two M5
// buttons. A tap can't be told from the start of a double-tap until the
// double-tap window closes, so a tap acts ~DOUBLE_MS after release.
//   hold >= HOLD_MS   BtnA held (the firmware's 600 ms hold opens the menu)
//   tap               BtnA pressed + released
//   double-tap        BtnB pressed
class WsKeys {
  static constexpr uint32_t HOLD_MS = 550, DOUBLE_MS = 300, DEBOUNCE_MS = 25;
  bool     _raw = false, _held = false;
  uint32_t _down = 0, _lastUp = 0, _edge = 0;
  uint8_t  _taps = 0;
  uint8_t  _aPulse = 0;   // 2 = press frame next, 1 = release frame next
  bool     _bPulse = false;
 public:
  WsButton* a; WsButton* b;
  WsKeys(WsButton* a_, WsButton* b_) : a(a_), b(b_) {}
  void begin() {
    pinMode(ws147::PIN_BOOT, INPUT_PULLUP);
    if (ws147::PIN_BTN_B >= 0) pinMode(ws147::PIN_BTN_B, INPUT_PULLUP);
  }
  void update() {
    uint32_t now = millis();
    bool raw = digitalRead(ws147::PIN_BOOT) == LOW;
    if (raw != _raw && now - _edge >= DEBOUNCE_MS) {
      _edge = now;
      _raw = raw;
      if (raw) { _down = now; _held = false; }
      else if (!_held) { _taps++; _lastUp = now; }
      else _taps = 0;
    }
    bool aDown = false, bDown = false;
    if (_raw && now - _down >= HOLD_MS) { _held = true; _taps = 0; }
    if (_held && _raw) aDown = true;
    if (_taps && !_raw && now - _lastUp >= DOUBLE_MS) {
      if (_taps == 1) _aPulse = 2; else _bPulse = true;
      _taps = 0;
    }
    if (_aPulse == 2) { aDown = true; _aPulse = 1; }
    else if (_aPulse == 1) _aPulse = 0;
    if (_bPulse) { bDown = true; _bPulse = false; }
    if (ws147::PIN_BTN_B >= 0 && digitalRead(ws147::PIN_BTN_B) == LOW) bDown = true;
    a->drive(aDown, _held ? _down : 0);
    b->drive(bDown);
  }
};

// No AXP: the backlight is the only power control. USB powers the board.
class WsPower {
  uint8_t _pct = 100;
  bool    _on = true;
  WsLcd*  _lcd;
 public:
  explicit WsPower(WsLcd* lcd) : _lcd(lcd) {}
  void begin() {}
  void ScreenBreath(uint8_t pct) {
    _pct = pct > 100 ? 100 : pct;
    if (_on) _lcd->setBrightness(_pct * 255 / 100);
  }
  void SetLDO2(bool on) {
    _on = on;
    if (on) { _lcd->wakeup(); _lcd->setBrightness(_pct * 255 / 100); }
    else    { _lcd->setBrightness(0); _lcd->sleep(); }
  }
  float GetBatVoltage() { return 0; }
  float GetVBusVoltage() { return 5.0f; }   // always on USB, so the clock screen is available
  float GetBatCurrent() { return 0; }
  float GetTempInAXP192() { return temperatureRead(); }
  // Nothing to cut; park the chip. RESET restarts it.
  void PowerOff() {
    SetLDO2(false);
    rgbLedWrite(BOARD_LED_PIN, 0, 0, 0);
    delay(300);
    esp_deep_sleep_start();
  }
  uint8_t GetBtnPress() { return 0; }
};

// No IMU: report the device lying flat, screen up (no nap, no shake).
struct WsImu {
  void Init() {}
  void getAccelData(float* ax, float* ay, float* az) { *ax = 0; *ay = 0; *az = 1; }
};

struct RTC_TimeTypeDef { uint8_t Hours, Minutes, Seconds; };
struct RTC_DateTypeDef { uint8_t WeekDay, Month, Date; uint16_t Year; };

// No RTC chip: keep time in software from whatever the bridge last set.
// It runs from boot, so the clock shows again once the desktop reconnects.
class WsRtc {
  time_t   _base = 0;      // local epoch seconds at _baseMs
  uint32_t _baseMs = 0;
  bool     _set = false;
  tm cur() {
    time_t t = _base + (_set ? (millis() - _baseMs) / 1000 : 0);
    tm out; gmtime_r(&t, &out);
    return out;
  }
  void store(int y, int mo, int d, int h, int mi, int s) {
    tm t = {}; t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d;
    t.tm_hour = h; t.tm_min = mi; t.tm_sec = s;
    _base = mktime(&t);   // TZ is UTC, so this round-trips with gmtime_r
    _baseMs = millis(); _set = true;
  }
 public:
  void GetTime(RTC_TimeTypeDef* t) { tm c = cur();
#if defined(BOARD_DEBUG_LOOP)
    static uint32_t lastDbg = 0;
    if (millis() - lastDbg > 2000) { lastDbg = millis(); Serial.printf("[rtc] get base=%ld baseMs=%lu set=%d now=%lu\n", (long)_base, (unsigned long)_baseMs, _set, (unsigned long)millis()); }
#endif
    *t = { (uint8_t)c.tm_hour, (uint8_t)c.tm_min, (uint8_t)c.tm_sec }; }
  void GetDate(RTC_DateTypeDef* d) { tm c = cur(); *d = { (uint8_t)c.tm_wday, (uint8_t)(c.tm_mon + 1), (uint8_t)c.tm_mday, (uint16_t)(c.tm_year + 1900) }; }
  void SetTime(RTC_TimeTypeDef* t) {
#if defined(BOARD_DEBUG_LOOP)
    Serial.printf("[rtc] set %02u:%02u:%02u at %lu ms\n", t->Hours, t->Minutes, t->Seconds, (unsigned long)millis());
#endif
    tm c = cur(); store(c.tm_year + 1900, c.tm_mon + 1, c.tm_mday, t->Hours, t->Minutes, t->Seconds);
  }
  void SetDate(RTC_DateTypeDef* d) {
    tm c = cur(); store(d->Year, d->Month, d->Date, c.tm_hour, c.tm_min, c.tm_sec);
  }
};

// No buzzer: sound is a no-op (the Sound setting still toggles harmlessly).
struct WsBeep {
  void begin() {}
  void update() {}
  void tone(uint16_t, uint16_t) {}
};

class WsBoard {
 public:
  WsLcd    Lcd;
  WsButton BtnA, BtnB;
  WsKeys   Keys{&BtnA, &BtnB};
  WsPower  Axp{&Lcd};
  WsImu    Imu;
  WsRtc    Rtc;
  WsBeep   Beep;

  void begin() {
    Serial.setRxBufferSize(2048);   // default is too small: a whole heartbeat line arrives at once
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);   // don't stall the UI when nobody reads USB serial
    Lcd.begin();
    Keys.begin();
  }
  void update() { Keys.update(); }
};

inline WsBoard M5;
