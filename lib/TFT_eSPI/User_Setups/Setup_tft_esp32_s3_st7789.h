#define USER_SETUP_ID 209 // A new unique ID

#define ST7789_DRIVER // ST7789 display driver

#define TFT_WIDTH  240
#define TFT_HEIGHT 320

#define TFT_MOSI 20
#define TFT_SCLK 21
#define TFT_CS   -1
#define TFT_DC   0
#define TFT_RST  -1
#ifndef TFT_BL
#define TFT_BL   -1
#endif

#define SPI_FREQUENCY  40000000 // ST7789 can handle higher speed

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF

#define SMOOTH_FONT
