// GxEPD2 driver for WFT0371CZ78 / GDEY037Z03 (UC8253 controller)
// 3.7" BWR e-paper display, 240x416, Black/White/Red
//
// Based on GxEPD2_290_Z13c by Jean-Marc Zingg
// UC8253 init sequence from GoodDisplay EPD.cpp for GDEY037Z03

#ifndef _GxEPD2_371_Z03_H_
#define _GxEPD2_371_Z03_H_

#include <GxEPD2_EPD.h>

class GxEPD2_371_Z03 : public GxEPD2_EPD
{
  public:
    // attributes
    static const uint16_t WIDTH = 240;
    static const uint16_t WIDTH_VISIBLE = WIDTH;
    static const uint16_t HEIGHT = 416;
    static const GxEPD2::Panel panel = (GxEPD2::Panel)200;
    static const bool hasColor = true;
    static const bool hasPartialUpdate = true;
    static const bool usePartialUpdateWindow = false;
    static const bool hasFastPartialUpdate = false;
    static const uint16_t power_on_time = 200;   // ms
    static const uint16_t power_off_time = 100;  // ms
    static const uint16_t full_refresh_time = 20000;   // ms
    static const uint16_t partial_refresh_time = 20000; // ms

    // constructor
    GxEPD2_371_Z03(int16_t cs, int16_t dc, int16_t rst, int16_t busy);

    // methods (virtual)
    void clearScreen(uint8_t value = 0xFF);
    void clearScreen(uint8_t black_value, uint8_t color_value);
    void writeScreenBuffer(uint8_t value = 0xFF);
    void writeScreenBuffer(uint8_t black_value, uint8_t color_value);
    // write to controller memory, without screen refresh; x and w should be multiple of 8
    void writeImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h,
                    bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part,
                        int16_t w_bitmap, int16_t h_bitmap,
                        int16_t x, int16_t y, int16_t w, int16_t h,
                        bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImage(const uint8_t* black, const uint8_t* color,
                    int16_t x, int16_t y, int16_t w, int16_t h,
                    bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImageToPrevious(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h,
                              bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImageToCurrent(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h,
                             bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImagePart(const uint8_t* black, const uint8_t* color,
                        int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                        int16_t x, int16_t y, int16_t w, int16_t h,
                        bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImagePartToPrevious(const uint8_t bitmap[], int16_t x_part, int16_t y_part,
                                  int16_t w_bitmap, int16_t h_bitmap,
                                  int16_t x, int16_t y, int16_t w, int16_t h,
                                  bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImagePartToCurrent(const uint8_t bitmap[], int16_t x_part, int16_t y_part,
                                 int16_t w_bitmap, int16_t h_bitmap,
                                 int16_t x, int16_t y, int16_t w, int16_t h,
                                 bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeNative(const uint8_t* data1, const uint8_t* data2,
                     int16_t x, int16_t y, int16_t w, int16_t h,
                     bool invert = false, bool mirror_y = false, bool pgm = false);
    // write to controller memory, with screen refresh; x and w should be multiple of 8
    void drawImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h,
                   bool invert = false, bool mirror_y = false, bool pgm = false);
    void drawImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part,
                       int16_t w_bitmap, int16_t h_bitmap,
                       int16_t x, int16_t y, int16_t w, int16_t h,
                       bool invert = false, bool mirror_y = false, bool pgm = false);
    void drawImage(const uint8_t* black, const uint8_t* color,
                   int16_t x, int16_t y, int16_t w, int16_t h,
                   bool invert = false, bool mirror_y = false, bool pgm = false);
    void drawImagePart(const uint8_t* black, const uint8_t* color,
                       int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                       int16_t x, int16_t y, int16_t w, int16_t h,
                       bool invert = false, bool mirror_y = false, bool pgm = false);
    void drawNative(const uint8_t* data1, const uint8_t* data2,
                    int16_t x, int16_t y, int16_t w, int16_t h,
                    bool invert = false, bool mirror_y = false, bool pgm = false);
    void refresh(bool partial_update_mode = false);
    void refresh(int16_t x, int16_t y, int16_t w, int16_t h);
    void refresh_bw(int16_t x, int16_t y, int16_t w, int16_t h); // stub
    void powerOff();
    void hibernate();

  private:
    void _writeImage(uint8_t command, const uint8_t* bitmap,
                     int16_t x, int16_t y, int16_t w, int16_t h,
                     bool invert, bool mirror_y, bool pgm);
    void _writeImagePart(uint8_t command, const uint8_t* bitmap,
                         int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                         int16_t x, int16_t y, int16_t w, int16_t h,
                         bool invert, bool mirror_y, bool pgm);
    void _setPartialRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
    void _PowerOn();
    void _PowerOff();
    void _InitDisplay();
    void _Init_Full();
    void _Init_Part();
    void _Update_Full();
    void _Update_Part();
};

#endif
