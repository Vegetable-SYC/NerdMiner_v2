#ifndef TFT_ESP32_S3_ST7789_DISPLAY_H_
#define TFT_ESP32_S3_ST7789_DISPLAY_H_

#include "displayDriver.h"

// TFT_eSPI library
#include <TFT_eSPI.h>

// FastLED library for WS2812
#include <FastLED.h>

// SD Card library - assuming standard ESP-IDF/Arduino core integration
#include <SD_MMC.h>

extern DisplayDriver tftEsp32S3St7789DisplayDriver;

#endif // TFT_ESP32_S3_ST7789_DISPLAY_H_
