#ifndef __ESP32_TFT_S3_BOARD_H__
#define __ESP32_TFT_S3_BOARD_H__

// Define any specific pin mappings or configurations for TFT_ESP32_S3_Board here if needed
// For now, this file can be mostly empty as pins are defined in platformio.ini and TFT_eSPI User_Setup
#define RGB_LED_PIN 42
#ifndef RGB_LED_ORDER
#define RGB_LED_ORDER RGB
#endif
#endif // __ESP32_TFT_S3_BOARD_H__
