#pragma once
// Forward declaration of the drawing-surface type. TFT_eSPI is a class on
// M5StickC Plus; on LovyanGFX boards it is an alias for lgfx::LovyanGFX.
#if defined(BOARD_WS_LCD_169) || defined(BOARD_WS_LCD_147)
namespace lgfx { inline namespace v1 { class LovyanGFX; } }
using TFT_eSPI = lgfx::LovyanGFX;
#else
class TFT_eSPI;
#endif
