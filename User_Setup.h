#define ST7735_DRIVER
#define TFT_CS   PIN_D8  // Chip select control pin D8
#define TFT_DC   PIN_D1  // Data Command control pin
#define TFT_RST  PIN_D4  // Set TFT_RST to -1 if the display RESET is connected to NodeMCU RST or 3.3V

#define LOAD_GLCD   // Font 1. Original Adafruit 8 pixel font needs ~1820 bytes in FLASH
#define SPI_FREQUENCY  27000000 // Actually sets it to 26.67MHz = 80/3
