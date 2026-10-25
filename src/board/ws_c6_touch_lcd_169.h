#pragma once
// Waveshare ESP32-C6-Touch-LCD-1.69 (also fits the non-touch ESP32-C6-LCD-1.69).
// Presents the subset of the M5StickCPlus API the firmware uses, so the UI
// code doesn't need to know which board it's on.
//
//   LCD   ST7789V2 240x280, SPI: SCK 1, MOSI 2, DC 3, RST 4, CS 5, BL 6
//   I2C   SDA 8, SCL 7: QMI8658 IMU @0x6B, PCF85063 RTC @0x51, CST816 touch
//   Keys  BOOT GPIO9 (BtnA), PWR GPIO18 (BtnB), both active-low
//   Power BAT_EN GPIO15 must be held high to stay on when running on battery;
//         battery sense on GPIO0 through a 1:3 divider
//
// The UI was laid out for the M5StickC Plus's 135x240 panel. For now the
// panel window is shrunk to a centred 135x240 region so every screen
// renders unchanged; using the full 240x280 comes later.

#include <Arduino.h>
#include <Wire.h>
#include <LovyanGFX.hpp>
#include <esp_mac.h>

#define BOARD_LED_PIN -1

// M5StickCPlus colour names used by the UI.
#define RED   0xF800
#define GREEN 0x07E0

namespace ws169 {
constexpr int PIN_SCK = 1, PIN_MOSI = 2, PIN_DC = 3, PIN_RST = 4, PIN_CS = 5, PIN_BL = 6;
constexpr int PIN_SCL = 7, PIN_SDA = 8;
constexpr int PIN_BOOT = 9, PIN_PWR = 18, PIN_BAT_EN = 15, PIN_BAT_ADC = 0;
constexpr int PANEL_W = 240, PANEL_H = 280, PANEL_RAM_Y = 20;
constexpr int UI_W = 135, UI_H = 240;
}

// UI code passes both the LCD and sprites around as TFT_eSPI*.
using TFT_eSPI    = lgfx::LovyanGFX;
using TFT_eSprite = lgfx::LGFX_Sprite;

class WsLcd : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel;
  lgfx::Bus_SPI      _bus;
  lgfx::Light_PWM    _light;

  void setWindow(int w, int h) {
    auto c = _panel.config();
    c.panel_width  = w;
    c.panel_height = h;
    c.offset_x = (ws169::PANEL_W - w) / 2;
    c.offset_y = ws169::PANEL_RAM_Y + (ws169::PANEL_H - h) / 2;
    _panel.config(c);
  }

 public:
  WsLcd() {
    auto b = _bus.config();
    b.spi_host   = SPI2_HOST;
    b.spi_mode   = 0;
    b.freq_write = 40000000;
    b.pin_sclk   = ws169::PIN_SCK;
    b.pin_mosi   = ws169::PIN_MOSI;
    b.pin_miso   = -1;
    b.pin_dc     = ws169::PIN_DC;
    _bus.config(b);
    _panel.setBus(&_bus);

    auto c = _panel.config();
    c.pin_cs   = ws169::PIN_CS;
    c.pin_rst  = ws169::PIN_RST;
    c.pin_busy = -1;
    c.memory_width  = 240;
    c.memory_height = 320;
    c.invert   = true;
    c.readable = false;
    c.bus_shared = false;
    _panel.config(c);
    setWindow(ws169::PANEL_W, ws169::PANEL_H);

    auto l = _light.config();
    l.pin_bl = ws169::PIN_BL;
    l.freq   = 5000;
    l.pwm_channel = 0;
    _light.config(l);
    _panel.setLight(&_light);
    setPanel(&_panel);
  }

  void begin() {
    init();
    fillScreen(TFT_BLACK);          // clear the border outside the UI window
    setWindow(ws169::UI_W, ws169::UI_H);
    setRotation(0);
  }
};

// Polled button with the M5 Button API subset the firmware uses.
class WsButton {
  uint8_t  _pin;
  bool     _now = false, _prev = false;
  uint32_t _since = 0;
 public:
  explicit WsButton(uint8_t pin) : _pin(pin) {}
  void begin() { pinMode(_pin, INPUT_PULLUP); }
  void read() {
    _prev = _now;
    _now = digitalRead(_pin) == LOW;
    if (_now != _prev) _since = millis();
  }
  bool isPressed()   const { return _now; }
  bool wasPressed()  const { return _now && !_prev; }
  bool wasReleased() const { return !_now && _prev; }
  bool pressedFor(uint32_t ms) const { return _now && millis() - _since >= ms; }
};

// Stand-in for the M5StickC Plus AXP192: backlight, battery, power key.
class WsPower {
  uint8_t _pct = 100;
  bool    _on = true;
  WsLcd*  _lcd;
 public:
  explicit WsPower(WsLcd* lcd) : _lcd(lcd) {}
  void begin() {
    pinMode(ws169::PIN_BAT_EN, OUTPUT);
    digitalWrite(ws169::PIN_BAT_EN, HIGH);
    analogSetPinAttenuation(ws169::PIN_BAT_ADC, ADC_11db);
  }
  void ScreenBreath(uint8_t pct) {
    _pct = pct > 100 ? 100 : pct;
    if (_on) _lcd->setBrightness(_pct * 255 / 100);
  }
  // LDO2 powers the M5 backlight; here it means panel on/off.
  void SetLDO2(bool on) {
    _on = on;
    if (on) { _lcd->wakeup(); _lcd->setBrightness(_pct * 255 / 100); }
    else    { _lcd->setBrightness(0); _lcd->sleep(); }
  }
  float GetBatVoltage() { return analogReadMilliVolts(ws169::PIN_BAT_ADC) * 3 / 1000.0f; }
  // No VBUS sense on this board. A battery sitting at charge-termination
  // voltage (or no battery, where the charger output floats high) is the
  // best available hint that USB is connected.
  float GetVBusVoltage() { return GetBatVoltage() > 4.1f ? 5.0f : 0.0f; }
  float GetBatCurrent() { return 0; }
  float GetTempInAXP192() { return temperatureRead(); }
  // Dropping BAT_EN cuts power when running on battery. On USB the board
  // stays powered, so fall back to deep sleep (BOOT or reset wakes it).
  void PowerOff() {
    SetLDO2(false);
    digitalWrite(ws169::PIN_BAT_EN, LOW);
    delay(500);
    esp_deep_sleep_start();
  }
  // The PWR key is BtnB here, so there is no separate AXP-style power key.
  uint8_t GetBtnPress() { return 0; }
};

// QMI8658 accelerometer, read directly over I2C.
class WsImu {
  static constexpr uint8_t ADDR = 0x6B;
  bool _ok = false;
  void wr(uint8_t r, uint8_t v) { Wire.beginTransmission(ADDR); Wire.write(r); Wire.write(v); Wire.endTransmission(); }
 public:
  void Init() {
    Wire.beginTransmission(ADDR); Wire.write(0x00); Wire.endTransmission(false);
    _ok = Wire.requestFrom(ADDR, (uint8_t)1) == 1 && Wire.read() == 0x05;  // WHO_AM_I
    if (!_ok) return;
    wr(0x02, 0x40);   // CTRL1: register auto-increment
    wr(0x03, 0x16);   // CTRL2: accel ±4g, ~125 Hz
    wr(0x08, 0x01);   // CTRL7: accel on
  }
  void getAccelData(float* ax, float* ay, float* az) {
    *ax = 0; *ay = 0; *az = 1;
    if (!_ok) return;
    Wire.beginTransmission(ADDR); Wire.write(0x35); Wire.endTransmission(false);
    if (Wire.requestFrom(ADDR, (uint8_t)6) != 6) return;
    int16_t v[3];
    for (int i = 0; i < 3; i++) { uint8_t lo = Wire.read(), hi = Wire.read(); v[i] = (int16_t)(hi << 8 | lo); }
    *ax = v[0] / 8192.0f; *ay = v[1] / 8192.0f; *az = v[2] / 8192.0f;
  }
};

// Same layout as the M5StickCPlus RTC structs (data.h brace-initialises them).
struct RTC_TimeTypeDef { uint8_t Hours, Minutes, Seconds; };
struct RTC_DateTypeDef { uint8_t WeekDay, Month, Date; uint16_t Year; };

// PCF85063 real-time clock.
class WsRtc {
  static constexpr uint8_t ADDR = 0x51;
  static uint8_t bcd2(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
  static uint8_t tobcd(uint8_t v) { return (v / 10) << 4 | (v % 10); }
  bool rd(uint8_t reg, uint8_t* buf, uint8_t n) {
    Wire.beginTransmission(ADDR); Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(ADDR, n) != n) return false;
    for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
    return true;
  }
 public:
  void GetTime(RTC_TimeTypeDef* t) {
    uint8_t b[3];
    if (!rd(0x04, b, 3)) { *t = {0, 0, 0}; return; }
    *t = { bcd2(b[2] & 0x3F), bcd2(b[1] & 0x7F), bcd2(b[0] & 0x7F) };
  }
  void GetDate(RTC_DateTypeDef* d) {
    uint8_t b[4];
    if (!rd(0x07, b, 4)) { *d = {0, 1, 1, 2000}; return; }
    *d = { (uint8_t)(b[1] & 0x07), bcd2(b[2] & 0x1F), bcd2(b[0] & 0x3F), (uint16_t)(2000 + bcd2(b[3])) };
  }
  void SetTime(RTC_TimeTypeDef* t) {
    Wire.beginTransmission(ADDR); Wire.write(0x04);
    Wire.write(tobcd(t->Seconds)); Wire.write(tobcd(t->Minutes)); Wire.write(tobcd(t->Hours));
    Wire.endTransmission();
  }
  void SetDate(RTC_DateTypeDef* d) {
    Wire.beginTransmission(ADDR); Wire.write(0x07);
    Wire.write(tobcd(d->Date)); Wire.write(d->WeekDay & 0x07);
    Wire.write(tobcd(d->Month)); Wire.write(tobcd(d->Year % 100));
    Wire.endTransmission();
  }
};

// The board's speaker goes through an ES8311 codec; silent until that's wired up.
struct WsBeep {
  void begin() {}
  void update() {}
  void tone(uint16_t, uint16_t) {}
};

class WsBoard {
 public:
  WsLcd    Lcd;
  WsButton BtnA{ws169::PIN_BOOT};
  WsButton BtnB{ws169::PIN_PWR};
  WsPower  Axp{&Lcd};
  WsImu    Imu;
  WsRtc    Rtc;
  WsBeep   Beep;

  void begin() {
    Axp.begin();                    // latch battery power first
    Serial.begin(115200);
    Wire.begin(ws169::PIN_SDA, ws169::PIN_SCL);
    Lcd.begin();
    BtnA.begin();
    BtnB.begin();
  }
  void update() { BtnA.read(); BtnB.read(); }
};

inline WsBoard M5;
