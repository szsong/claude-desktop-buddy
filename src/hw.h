#pragma once
// Board selection. Each board header provides an M5StickCPlus-shaped `M5`
// object (Lcd, BtnA/BtnB, Axp, Imu, Rtc, Beep) plus TFT_eSPI/TFT_eSprite
// types, so the UI code stays board-agnostic.
#if defined(BOARD_WS_C6_TOUCH_LCD_169)
#include "board/ws_c6_touch_lcd_169.h"
#else
#include <M5StickCPlus.h>
#define BOARD_LED_PIN 10   // red LED, active-low
#endif
