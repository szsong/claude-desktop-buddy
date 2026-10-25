#pragma once
// Board selection. Each board header provides an M5StickCPlus-shaped `M5`
// object (Lcd, BtnA/BtnB, Axp, Imu, Rtc, Beep) plus TFT_eSPI/TFT_eSprite
// types, so the UI code stays board-agnostic. It also sets the UI canvas
// size (BOARD_UI_W x BOARD_UI_H, portrait) and the home-screen buddy scale.
#if defined(BOARD_WS_LCD_169)
#include "board/waveshare_lcd_169.h"
#else
#include <M5StickCPlus.h>
#define BOARD_LED_PIN 10   // red LED, active-low
#define BOARD_UI_W 135
#define BOARD_UI_H 240
#define BOARD_BUDDY_SCALE 2
#define BOARD_CLOCK_BUDDY_SCALE 1   // buddy above the portrait clock
#endif
