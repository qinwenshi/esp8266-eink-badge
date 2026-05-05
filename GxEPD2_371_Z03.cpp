// GxEPD2 driver for WFT0371CZ78 / GDEY037Z03 (UC8253 controller)
// 3.7" BWR e-paper display, 240x416, Black/White/Red
//
// Based on GxEPD2_290_Z13c by Jean-Marc Zingg
// UC8253 init sequence from GoodDisplay EPD.cpp for GDEY037Z03

#include "GxEPD2_371_Z03.h"

GxEPD2_371_Z03::GxEPD2_371_Z03(int16_t cs, int16_t dc, int16_t rst, int16_t busy)
  : GxEPD2_EPD(cs, dc, rst, busy, LOW, 20000000, WIDTH, HEIGHT, (GxEPD2::Panel)200,
               hasColor, hasPartialUpdate, hasFastPartialUpdate)
{
}

void GxEPD2_371_Z03::clearScreen(uint8_t value)
{
  clearScreen(value, 0xFF);
}

void GxEPD2_371_Z03::clearScreen(uint8_t black_value, uint8_t color_value)
{
  _initial_write = false;
  _Init_Part();
  _writeCommand(0x91); // partial in
  _setPartialRamArea(0, 0, WIDTH, HEIGHT);
  _writeCommand(0x10);
  for (uint32_t i = 0; i < uint32_t(WIDTH) * uint32_t(HEIGHT) / 8; i++)
  {
    _writeData(black_value);
  }
  _writeCommand(0x13);
  for (uint32_t i = 0; i < uint32_t(WIDTH) * uint32_t(HEIGHT) / 8; i++)
  {
    _writeData(color_value);
  }
  _Update_Part();
  _writeCommand(0x92); // partial out
}

void GxEPD2_371_Z03::writeScreenBuffer(uint8_t value)
{
  writeScreenBuffer(value, 0xFF);
}

void GxEPD2_371_Z03::writeScreenBuffer(uint8_t black_value, uint8_t color_value)
{
  _initial_write = false;
  _Init_Part();
  _writeCommand(0x91); // partial in
  _setPartialRamArea(0, 0, WIDTH, HEIGHT);
  _writeCommand(0x10);
  for (uint32_t i = 0; i < uint32_t(WIDTH) * uint32_t(HEIGHT) / 8; i++)
  {
    _writeData(black_value);
  }
  _writeCommand(0x13);
  for (uint32_t i = 0; i < uint32_t(WIDTH) * uint32_t(HEIGHT) / 8; i++)
  {
    _writeData(color_value);
  }
  _writeCommand(0x92); // partial out
}

void GxEPD2_371_Z03::writeImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h,
                                 bool invert, bool mirror_y, bool pgm)
{
  writeImage(bitmap, NULL, x, y, w, h, invert, mirror_y, pgm);
}

void GxEPD2_371_Z03::writeImage(const uint8_t* black, const uint8_t* color,
                                 int16_t x, int16_t y, int16_t w, int16_t h,
                                 bool invert, bool mirror_y, bool pgm)
{
  _writeImage(0x10, black, x, y, w, h, invert, mirror_y, pgm);
  _writeImage(0x13, color, x, y, w, h, invert, mirror_y, pgm);
}

void GxEPD2_371_Z03::_writeImage(uint8_t command, const uint8_t* bitmap,
                                  int16_t x, int16_t y, int16_t w, int16_t h,
                                  bool invert, bool mirror_y, bool pgm)
{
  if (_initial_write) writeScreenBuffer();
  delay(1);
  int16_t wb = (w + 7) / 8; // width bytes, bitmaps are padded
  x -= x % 8; // byte boundary
  w = wb * 8; // byte boundary
  int16_t x1 = x < 0 ? 0 : x;
  int16_t y1 = y < 0 ? 0 : y;
  int16_t w1 = x + w < int16_t(WIDTH)  ? w : int16_t(WIDTH)  - x;
  int16_t h1 = y + h < int16_t(HEIGHT) ? h : int16_t(HEIGHT) - y;
  int16_t dx = x1 - x;
  int16_t dy = y1 - y;
  w1 -= dx;
  h1 -= dy;
  if ((w1 <= 0) || (h1 <= 0)) return;
  _Init_Part();
  _writeCommand(0x91); // partial in
  _setPartialRamArea(x1, y1, w1, h1);
  _writeCommand(command);
  for (int16_t i = 0; i < h1; i++)
  {
    for (int16_t j = 0; j < w1 / 8; j++)
    {
      uint8_t data = 0xFF;
      if (bitmap)
      {
        int16_t idx = mirror_y ? j + dx / 8 + ((h - 1 - (i + dy))) * wb : j + dx / 8 + (i + dy) * wb;
        if (pgm)
        {
#if defined(__AVR) || defined(ESP8266) || defined(ESP32)
          data = pgm_read_byte(&bitmap[idx]);
#else
          data = bitmap[idx];
#endif
        }
        else
        {
          data = bitmap[idx];
        }
        if (invert) data = ~data;
      }
      _writeData(data);
    }
  }
  _writeCommand(0x92); // partial out
  delay(1);
}

void GxEPD2_371_Z03::writeImageToPrevious(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h,
                                           bool invert, bool mirror_y, bool pgm)
{
  _writeImage(0x10, bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void GxEPD2_371_Z03::writeImageToCurrent(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h,
                                          bool invert, bool mirror_y, bool pgm)
{
  _writeImage(0x13, bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void GxEPD2_371_Z03::writeImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part,
                                     int16_t w_bitmap, int16_t h_bitmap,
                                     int16_t x, int16_t y, int16_t w, int16_t h,
                                     bool invert, bool mirror_y, bool pgm)
{
  writeImagePart(bitmap, NULL, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void GxEPD2_371_Z03::writeImagePart(const uint8_t* black, const uint8_t* color,
                                     int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                                     int16_t x, int16_t y, int16_t w, int16_t h,
                                     bool invert, bool mirror_y, bool pgm)
{
  _writeImagePart(0x10, black, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
  _writeImagePart(0x13, color, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void GxEPD2_371_Z03::_writeImagePart(uint8_t command, const uint8_t* bitmap,
                                      int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                                      int16_t x, int16_t y, int16_t w, int16_t h,
                                      bool invert, bool mirror_y, bool pgm)
{
  if (_initial_write) writeScreenBuffer();
  delay(1);
  if ((w_bitmap < 0) || (h_bitmap < 0) || (w < 0) || (h < 0)) return;
  if ((x_part < 0) || (x_part >= w_bitmap)) return;
  if ((y_part < 0) || (y_part >= h_bitmap)) return;
  int16_t wb_bitmap = (w_bitmap + 7) / 8;
  x_part -= x_part % 8;
  w = w_bitmap - x_part < w ? w_bitmap - x_part : w;
  h = h_bitmap - y_part < h ? h_bitmap - y_part : h;
  x -= x % 8;
  w = 8 * ((w + 7) / 8);
  int16_t x1 = x < 0 ? 0 : x;
  int16_t y1 = y < 0 ? 0 : y;
  int16_t w1 = x + w < int16_t(WIDTH)  ? w : int16_t(WIDTH)  - x;
  int16_t h1 = y + h < int16_t(HEIGHT) ? h : int16_t(HEIGHT) - y;
  int16_t dx = x1 - x;
  int16_t dy = y1 - y;
  w1 -= dx;
  h1 -= dy;
  if ((w1 <= 0) || (h1 <= 0)) return;
  if (!_using_partial_mode) _Init_Part();
  _writeCommand(0x91); // partial in
  _setPartialRamArea(x1, y1, w1, h1);
  _writeCommand(command);
  for (int16_t i = 0; i < h1; i++)
  {
    for (int16_t j = 0; j < w1 / 8; j++)
    {
      uint8_t data;
      int16_t idx = mirror_y
        ? x_part / 8 + j + dx / 8 + ((h_bitmap - 1 - (y_part + i + dy))) * wb_bitmap
        : x_part / 8 + j + dx / 8 + (y_part + i + dy) * wb_bitmap;
      if (pgm)
      {
#if defined(__AVR) || defined(ESP8266) || defined(ESP32)
        data = pgm_read_byte(&bitmap[idx]);
#else
        data = bitmap[idx];
#endif
      }
      else
      {
        data = bitmap[idx];
      }
      if (invert) data = ~data;
      _writeData(data);
    }
  }
  _writeCommand(0x92); // partial out
  delay(1);
}

void GxEPD2_371_Z03::writeImagePartToPrevious(const uint8_t bitmap[], int16_t x_part, int16_t y_part,
                                               int16_t w_bitmap, int16_t h_bitmap,
                                               int16_t x, int16_t y, int16_t w, int16_t h,
                                               bool invert, bool mirror_y, bool pgm)
{
  _writeImagePart(0x10, bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void GxEPD2_371_Z03::writeImagePartToCurrent(const uint8_t bitmap[], int16_t x_part, int16_t y_part,
                                              int16_t w_bitmap, int16_t h_bitmap,
                                              int16_t x, int16_t y, int16_t w, int16_t h,
                                              bool invert, bool mirror_y, bool pgm)
{
  _writeImagePart(0x13, bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void GxEPD2_371_Z03::writeNative(const uint8_t* data1, const uint8_t* data2,
                                  int16_t x, int16_t y, int16_t w, int16_t h,
                                  bool invert, bool mirror_y, bool pgm)
{
  if (data1)
  {
    writeImage(data1, x, y, w, h, invert, mirror_y, pgm);
  }
}

void GxEPD2_371_Z03::drawImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h,
                                bool invert, bool mirror_y, bool pgm)
{
  writeImage(bitmap, x, y, w, h, invert, mirror_y, pgm);
  refresh(x, y, w, h);
}

void GxEPD2_371_Z03::drawImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part,
                                    int16_t w_bitmap, int16_t h_bitmap,
                                    int16_t x, int16_t y, int16_t w, int16_t h,
                                    bool invert, bool mirror_y, bool pgm)
{
  writeImagePart(bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
  refresh(x, y, w, h);
}

void GxEPD2_371_Z03::drawImage(const uint8_t* black, const uint8_t* color,
                                int16_t x, int16_t y, int16_t w, int16_t h,
                                bool invert, bool mirror_y, bool pgm)
{
  writeImage(black, color, x, y, w, h, invert, mirror_y, pgm);
  refresh(x, y, w, h);
}

void GxEPD2_371_Z03::drawImagePart(const uint8_t* black, const uint8_t* color,
                                    int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                                    int16_t x, int16_t y, int16_t w, int16_t h,
                                    bool invert, bool mirror_y, bool pgm)
{
  writeImagePart(black, color, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
  refresh(x, y, w, h);
}

void GxEPD2_371_Z03::drawNative(const uint8_t* data1, const uint8_t* data2,
                                 int16_t x, int16_t y, int16_t w, int16_t h,
                                 bool invert, bool mirror_y, bool pgm)
{
  writeNative(data1, data2, x, y, w, h, invert, mirror_y, pgm);
  refresh(x, y, w, h);
}

void GxEPD2_371_Z03::refresh(bool partial_update_mode)
{
  if (partial_update_mode) refresh(0, 0, WIDTH, HEIGHT);
  else
  {
    _Update_Full();
    _PowerOff();
  }
}

void GxEPD2_371_Z03::refresh(int16_t x, int16_t y, int16_t w, int16_t h)
{
  int16_t w1 = x < 0 ? w + x : w;
  int16_t h1 = y < 0 ? h + y : h;
  int16_t x1 = x < 0 ? 0 : x;
  int16_t y1 = y < 0 ? 0 : y;
  w1 = x1 + w1 < int16_t(WIDTH)  ? w1 : int16_t(WIDTH)  - x1;
  h1 = y1 + h1 < int16_t(HEIGHT) ? h1 : int16_t(HEIGHT) - y1;
  if ((w1 <= 0) || (h1 <= 0)) return;
  w1 += x1 % 8;
  if (w1 % 8 > 0) w1 += 8 - w1 % 8;
  x1 -= x1 % 8;
  _Init_Part();
  _setPartialRamArea(x1, y1, w1, h1);
  _Update_Part();
  _PowerOff();
}

void GxEPD2_371_Z03::powerOff()
{
  _PowerOff();
}

void GxEPD2_371_Z03::hibernate()
{
  _PowerOff();
  if (_rst >= 0)
  {
    _writeCommand(0x07); // deep sleep
    _writeData(0xA5);    // check code
    _hibernating = true;
  }
}

void GxEPD2_371_Z03::refresh_bw(int16_t x, int16_t y, int16_t w, int16_t h)
{
  // stub: UC8253 BWR does not support BW-only differential partial update
  (void)x; (void)y; (void)w; (void)h;
}

void GxEPD2_371_Z03::_setPartialRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
  uint16_t xe = x + w - 1;
  uint16_t ye = y + h - 1;
  x  &= 0xFFF8; // byte boundary (align down)
  xe |= 0x0007; // byte boundary inclusive (align up)
  _writeCommand(0x90); // partial window
  _writeData(x  & 0xFF); // x_start (WIDTH=240 < 256, high byte always 0)
  _writeData(xe & 0xFF); // x_end
  _writeData(y  >> 8);   // y_start high
  _writeData(y  & 0xFF); // y_start low
  _writeData(ye >> 8);   // y_end high
  _writeData(ye & 0xFF); // y_end low
  _writeData(0x01);
}

void GxEPD2_371_Z03::_PowerOn()
{
  // Power-on command is sent inside _InitDisplay(); just mark state.
  _power_is_on = true;
}

void GxEPD2_371_Z03::_PowerOff()
{
  _writeCommand(0x02); // power off
  _waitWhileBusy("_PowerOff", power_off_time);
  _power_is_on = false;
  _using_partial_mode = false;
}

void GxEPD2_371_Z03::_InitDisplay()
{
  if (_hibernating) _reset();
  // UC8253 full-refresh power-on sequence (from GoodDisplay GDEY037Z03/EPD.cpp)
  _writeCommand(0x04); // power on
  _waitWhileBusy("_InitDisplay", power_on_time);
}

void GxEPD2_371_Z03::_Init_Full()
{
  _InitDisplay();
  _PowerOn();
}

void GxEPD2_371_Z03::_Init_Part()
{
  _reset();
  // Panel setting: partial mode, BWR, internal registers
  _writeCommand(0x00); _writeData(0xd7);
  _writeCommand(0x04); // power on
  _waitWhileBusy("_Init_Part", power_on_time);
  _writeCommand(0xE0); _writeData(0x02);
  _writeCommand(0xE5); _writeData(0x6E);
  _writeCommand(0x50); _writeData(0x17); // VCOM interval
  _power_is_on = true;
  _using_partial_mode = true;
}

void GxEPD2_371_Z03::_Update_Full()
{
  _writeCommand(0x12); // display refresh
  delay(1);
  _waitWhileBusy("_Update_Full", full_refresh_time);
}

void GxEPD2_371_Z03::_Update_Part()
{
  _writeCommand(0x12); // display refresh
  delay(1);
  _waitWhileBusy("_Update_Part", partial_refresh_time);
}
