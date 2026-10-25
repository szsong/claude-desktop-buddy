#pragma once
// Waveshare 1.69" boards: ESP32-S3-Touch-LCD-1.69 and ESP32-C6-Touch-LCD-1.69
// (the non-touch C6 variant fits too). Both pair a ST7789V2 240x280 panel
// with a QMI8658 IMU (@0x6B), PCF85063 RTC (@0x51) and CST816 touch on I2C,
// a BOOT key, a PWR key and a battery latch; only the pins differ.
// Presents the subset of the M5StickCPlus API the firmware uses, so the UI
// code doesn't need to know which board it's on.
//
//              S3    C6
//   LCD SCK     6     1
//   LCD MOSI    7     2
//   LCD DC      4     3
//   LCD RST     8     4
//   LCD CS      5     5
//   LCD BL     15     6
//   I2C SDA    11     8
//   I2C SCL    10     7
//   BOOT (A)    0     9
//   PWR  (B)   40    18    active-low
//   PWR latch  41    15    hold high to stay on when running on battery
//   Battery     1     0    ADC through a 1:3 divider
//   Buzzer     42     -    (C6 has an ES8311 codec instead; not wired up yet)
//
// The S3 pins above are the V2.1 board. The first S3 revision wires PWR,
// latch and buzzer to GPIO36, 35 and 33 instead, which are also the octal
// PSRAM's data lines: pressing PWR with PSRAM running crashes the chip. That
// revision (BOARD_WS_S3_V1) is built with PSRAM off so those pins are free.
//
// The UI uses the whole 240x280 panel; layouts derive from BOARD_UI_W/H.

#include <Arduino.h>
#include <Wire.h>
#include <LovyanGFX.hpp>
#include <esp_mac.h>

#define BOARD_LED_PIN -1
#define BOARD_UI_W 240
#define BOARD_UI_H 280
#define BOARD_BUDDY_SCALE 3
#define BOARD_CLOCK_BUDDY_SCALE 2

// M5StickCPlus colour names used by the UI.
#define RED   0xF800
#define GREEN 0x07E0

namespace ws169 {
#if defined(BOARD_WS_S3_TOUCH_LCD_169)
constexpr int PIN_SCK = 6, PIN_MOSI = 7, PIN_DC = 4, PIN_RST = 8, PIN_CS = 5, PIN_BL = 15;
constexpr int PIN_SCL = 10, PIN_SDA = 11;
#if defined(BOARD_WS_S3_V1)
constexpr int PIN_BOOT = 0, PIN_PWR = 36, PIN_BAT_EN = 35, PIN_BAT_ADC = 1;
constexpr int PIN_BUZZER = 33;
#else
constexpr int PIN_BOOT = 0, PIN_PWR = 40, PIN_BAT_EN = 41, PIN_BAT_ADC = 1;
constexpr int PIN_BUZZER = 42;
#endif
constexpr int PIN_TP_RST = 13;
#else
constexpr int PIN_SCK = 1, PIN_MOSI = 2, PIN_DC = 3, PIN_RST = 4, PIN_CS = 5, PIN_BL = 6;
constexpr int PIN_SCL = 7, PIN_SDA = 8;
constexpr int PIN_BOOT = 9, PIN_PWR = 18, PIN_BAT_EN = 15, PIN_BAT_ADC = 0;
constexpr int PIN_BUZZER = -1;
constexpr int PIN_TP_RST = -1;
#endif
constexpr int PANEL_W = 240, PANEL_H = 280, PANEL_RAM_Y = 20;
constexpr int UI_W = BOARD_UI_W, UI_H = BOARD_UI_H;
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
 protected:
  uint8_t  _pin;
  bool     _now = false, _prev = false;
  uint32_t _since = 0;
  void set(bool down) {
    _prev = _now;
    _now = down;
    if (_now != _prev) _since = millis();
  }
 public:
  explicit WsButton(uint8_t pin) : _pin(pin) {}
  void begin() { pinMode(_pin, INPUT_PULLUP); }
  // `also` ORs in a second input (the touch screen for BtnB).
  void read(bool also = false) {
    bool key = digitalRead(_pin) == LOW;
#if defined(BOARD_DEBUG_KEYS)
    static bool last[49];
    if (key != last[_pin]) Serial.printf("[key] gpio%u %s\n", _pin, key ? "down" : "up");
    last[_pin] = key;
#endif
    set(key || also);
  }
  bool isPressed()   const { return _now; }
  bool wasPressed()  const { return _now && !_prev; }
  bool wasReleased() const { return !_now && _prev; }
  bool pressedFor(uint32_t ms) const { return _now && millis() - _since >= ms; }
};

// CST816T touch controller (@0x15). The whole screen acts as one button,
// merged into BtnB: a tap does what the M5's side button does. It shares the I2C bus with the IMU and RTC.
class WsTouch : public WsButton {
  static constexpr uint8_t ADDR = 0x15;
  bool _ok = false;
 public:
  WsTouch() : WsButton(0) {}
  void begin() {
    if (ws169::PIN_TP_RST >= 0) {
      pinMode(ws169::PIN_TP_RST, OUTPUT);
      digitalWrite(ws169::PIN_TP_RST, LOW);  delay(10);
      digitalWrite(ws169::PIN_TP_RST, HIGH); delay(50);
    }
    Wire.beginTransmission(ADDR);
    _ok = Wire.endTransmission() == 0;
    if (_ok) {
      // Keep it awake: in auto-sleep it stops answering I2C between touches.
      Wire.beginTransmission(ADDR); Wire.write(0xFE); Wire.write(0x01); Wire.endTransmission();
    }
    Serial.printf("[touch] cst816 %s\n", _ok ? "found" : "not found");
  }
  void read() {
    if (!_ok) return;
    Wire.beginTransmission(ADDR); Wire.write(0x02);
    bool down = false;
    if (Wire.endTransmission(false) == 0 && Wire.requestFrom(ADDR, (uint8_t)1) == 1)
      down = (Wire.read() & 0x0F) > 0;   // number of fingers
#if defined(BOARD_DEBUG_KEYS)
    if (down != _now) Serial.printf("[touch] %s\n", down ? "down" : "up");
#endif
    set(down);
  }
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
    // Match the M5StickC Plus frame the UI expects (screen up = +Z). On
    // this board the QMI8658 reads -Z with the screen facing up, which made
    // the buddy think it was face-down and nap whenever it lay on a desk.
    *ax = v[0] / 8192.0f; *ay = v[1] / 8192.0f; *az = -v[2] / 8192.0f;
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

// Piezo buzzer on the S3. The C6's speaker goes through an ES8311 codec and
// stays silent until that's wired up.
struct WsBeep {
  void begin() { if (ws169::PIN_BUZZER >= 0) pinMode(ws169::PIN_BUZZER, OUTPUT); }
  void update() {}
  void tone(uint16_t freq, uint16_t dur) {
    if (ws169::PIN_BUZZER >= 0) ::tone(ws169::PIN_BUZZER, freq, dur);
  }
};

class WsBoard {
 public:
  WsLcd    Lcd;
  WsButton BtnA{ws169::PIN_BOOT};
  WsButton BtnB{ws169::PIN_PWR};
  WsTouch  Touch;
  WsPower  Axp{&Lcd};
  WsImu    Imu;
  WsRtc    Rtc;
  WsBeep   Beep;

  void begin() {
    Axp.begin();                    // latch battery power first
    Serial.begin(115200);
    // USB serial blocks on every print once its buffer fills and the host
    // isn't reading (plugged in, no monitor open), which stalls the UI.
    Serial.setTxTimeoutMs(0);
    Wire.begin(ws169::PIN_SDA, ws169::PIN_SCL);
    Lcd.begin();
    BtnA.begin();
    BtnB.begin();
    Touch.begin();
  }
  void update() { Touch.read(); BtnA.read(); BtnB.read(Touch.isPressed()); }
};

inline WsBoard M5;
