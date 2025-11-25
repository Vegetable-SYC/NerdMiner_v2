#define USER_SETUP_ID 208

#define ILI9341_DRIVER // ILI9341 display driver

#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// For a parallel TFT connection, the pins are defined for the 8-bit data bus
// For SPI, the pins are for MOSI, MISO, SCLK, CS, DC, RST.
// User provided pins: MOSI-13, MISO-12, SCK-14, RS-2, CS-15

#define TFT_MOSI 11
#define TFT_MISO 13
#define TFT_SCLK 12
#define TFT_CS   10  // Chip select control pin
#define TFT_DC   46  // Data Command control pin
#define TFT_RST  -1  // Reset pin (connected to RST, or -1 if not used)
#define TFT_BL   45
#ifndef TFT_BL
#define TFT_BL   -1  // LED back-light (connected to BL, or -1 if not used)
#endif

#define SPI_FREQUENCY  27000000 // 27 MHz SPI frequency

#define LOAD_GLCD   // Font 1. Original Adafruit 8 pixel font needs ~1820 bytes in FLASH
#define LOAD_FONT2  // Font 2. Small 16 pixel high font, needs ~3534 bytes in FLASH, 96 characters
#define LOAD_FONT4  // Font 4. Medium 26 pixel high font, needs ~5848 bytes in FLASH, 96 characters
#define LOAD_FONT6  // Font 6. Large 48 pixel font, needs ~2666 bytes in FLASH, only characters 1234567890:-.apm
#define LOAD_FONT7  // Font 7. 7 segment 48 pixel font, needs ~2438 bytes in FLASH, only characters 1234567890:.
#define LOAD_FONT8  // Font 8. Large 75 pixel font needs ~3256 bytes in FLASH, only characters 1234567890:-.
#define LOAD_GFXFF  // FreeFonts. Include access to the 48 Adafruit_GFX free fonts FF1 to FF48 and custom fonts

#define SMOOTH_FONT
